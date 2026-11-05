using System;
using System.Diagnostics;
using CameraUnlock.Core.Processing;
using CameraUnlock.Core.Tracking;
using Xunit;

namespace CameraUnlock.Core.Tests.Tracking
{
    /// <summary>
    /// The session's Accela stage, off by default, steps once per new packet between the
    /// receiver and the interpolators. LastRaw reports the pose it hands on.
    /// </summary>
    public class AccelaStageTests
    {
        private static readonly long Start = Stopwatch.Frequency;
        private static readonly long SixtyHz = Stopwatch.Frequency / 60;

        private static (FakeTrackingDataSource, HeadTrackingSession) Make()
        {
            var source = new FakeTrackingDataSource();
            var session = new HeadTrackingSession(source, new TrackingProcessor(), new PositionProcessor());
            return (source, session);
        }

        [Fact]
        public void OffChangesNothing()
        {
            var (a, plain) = Make();
            var (b, toggled) = Make();
            Assert.False(plain.AccelaOn);
            // Both see the same packets; the toggled one runs them through the stage first. Then
            // the stage goes off and both start over, so only the stage can differ.
            toggled.AccelaOn = true;
            for (int i = 0; i < 30; i++)
            {
                foreach (var s in new[] { a, b })
                {
                    s.Yaw = i;
                    s.PacketAt(Start / 2 + i * SixtyHz);
                }
                plain.Update(1f / 60f);
                toggled.Update(1f / 60f);
            }
            toggled.AccelaOn = false;
            plain.Reset();
            toggled.Reset();
            for (int i = 0; i < 120; i++)
            {
                foreach (var s in new[] { a, b })
                {
                    if (i % 3 == 0)
                    {
                        s.Yaw = 5f * (float)System.Math.Sin(i * 0.05);
                        s.PositionX = 0.02f * (float)System.Math.Cos(i * 0.07);
                        s.PacketAt(Start + i * SixtyHz);
                    }
                }
                plain.Update(1f / 144f);
                toggled.Update(1f / 144f);
                Assert.Equal(plain.Rotation.Yaw, toggled.Rotation.Yaw);
                Assert.Equal(plain.Rotation.Pitch, toggled.Rotation.Pitch);
                Assert.Equal(plain.Rotation.Roll, toggled.Rotation.Roll);
                Assert.Equal(plain.PositionOffset.X, toggled.PositionOffset.X);
                Assert.Equal(plain.PositionOffset.Y, toggled.PositionOffset.Y);
                Assert.Equal(plain.PositionOffset.Z, toggled.PositionOffset.Z);
            }
        }

        [Fact]
        public void EasesASmallStepOncePerPacket()
        {
            var (source, session) = Make();
            session.AccelaOn = true;
            source.PacketAt(Start);
            session.Update(0.007f);
            source.Yaw = 2f;
            source.PositionX = 1f;
            source.PacketAt(Start + SixtyHz);
            session.Update(0.007f);
            float first = session.LastRaw.Yaw;
            Assert.InRange(first, 1e-4f, 2f - 1e-4f);

            var (offSource, off) = Make();
            offSource.PacketAt(Start);
            off.Update(0.007f);
            offSource.Yaw = 2f;
            offSource.PositionX = 1f;
            offSource.PacketAt(Start + SixtyHz);
            off.Update(0.007f);
            Assert.Equal(2f, off.LastRaw.Yaw);
            session.Update(0.007f);
            Assert.Equal(first, session.LastRaw.Yaw);
        }

        [Fact]
        public void FollowsACentimetreLean()
        {
            var (source, session) = Make();
            var (offSource, off) = Make();
            session.AccelaOn = true;
            // The same packets in centimetres, through the filter on its own.
            var alone = new AccelaFilter();
            long t = Start;
            foreach (var s in new[] { source, offSource }) s.PacketAt(t);
            session.Update(1f / 60f);
            off.Update(1f / 60f);
            alone.Filter(new AccelaPose(0, 0, 0, 0, 0, 0), (double)t / Stopwatch.Frequency);
            double moved = 0;
            for (int i = 0; i < 120; i++)
            {
                t += SixtyHz;
                foreach (var s in new[] { source, offSource })
                {
                    s.PositionX = 0.01f;
                    s.PacketAt(t);
                }
                session.Update(1f / 60f);
                off.Update(1f / 60f);
                moved = alone.Filter(new AccelaPose(0, 0, 0, 1, 0, 0), (double)t / Stopwatch.Frequency).X;
            }
            // The share of the 1 cm lean the filter has moved. Read as 0.01 cm, the lean would
            // sit inside the position deadzone and never move.
            Assert.True(moved > 0.1 && moved < 1.0, moved.ToString());
            Assert.True(off.PositionOffset.X > 0f);
            double share = session.PositionOffset.X / off.PositionOffset.X;
            Assert.InRange(share, moved * 0.95, moved * 1.05);
        }

        [Fact]
        public void StartsFreshAfterATrackerRecenter()
        {
            var (source, session) = Make();
            session.AccelaOn = true;
            long t = Start;
            source.Yaw = 20f;
            for (int i = 0; i < 60; i++, t += SixtyHz)
            {
                source.PacketAt(t);
                session.Update(1f / 60f);
            }
            // The tracker zeroes its own output and signals.
            source.Yaw = 0f;
            source.RecenterRequested = true;
            source.PacketAt(t);
            session.Update(1f / 60f);
            Assert.Equal(0f, session.LastRaw.Yaw, 3);
        }

        [Fact]
        public void StartsFreshAfterFiveSecondsWithoutPackets()
        {
            var (source, session) = Make();
            session.AccelaOn = true;
            source.PacketAt(Start);
            session.Update(0.016f);
            source.Yaw = 10f;
            source.PacketAt(Start + (long)(HeadTrackingSession.AccelaRestartSeconds * Stopwatch.Frequency));
            session.Update(0.016f);
            Assert.Equal(10f, session.LastRaw.Yaw, 3);

            var (near, eased) = Make();
            eased.AccelaOn = true;
            near.PacketAt(Start);
            eased.Update(0.016f);
            near.Yaw = 10f;
            near.PacketAt(Start + (long)(HeadTrackingSession.AccelaRestartSeconds * Stopwatch.Frequency) - 1);
            eased.Update(0.016f);
            Assert.True(eased.LastRaw.Yaw < 9.9f);
        }
    }
}
