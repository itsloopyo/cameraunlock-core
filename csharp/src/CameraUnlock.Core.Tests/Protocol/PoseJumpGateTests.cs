using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Runtime.CompilerServices;
using System.Text;
using System.Threading;
using CameraUnlock.Core.Protocol;
using Xunit;

namespace CameraUnlock.Core.Tests.Protocol
{
    /// <summary>
    /// <see cref="PoseJumpGate"/> against data/fixtures/pose-jump-gate/cases.tsv, which
    /// cpp/tests/pose_jump_gate_tests.cpp runs through the C++ gate, and
    /// <see cref="OpenTrackReceiver"/> with the gate in its receive loop.
    /// </summary>
    public class PoseJumpGateTests : IDisposable
    {
        private const int TestPort = 14280;

        private OpenTrackReceiver? _receiver;
        private UdpClient? _sender;

        public void Dispose()
        {
            _receiver?.Dispose();
            _sender?.Close();
        }

        private static string FixturePath([CallerFilePath] string sourceFile = "")
        {
            DirectoryInfo? dir = new DirectoryInfo(Path.GetDirectoryName(sourceFile)!);
            while (dir != null && !File.Exists(Path.Combine(dir.FullName, "data", "fixtures", "pose-jump-gate", "cases.tsv")))
            {
                dir = dir.Parent;
            }
            if (dir == null)
            {
                throw new InvalidOperationException("no data/fixtures/pose-jump-gate/cases.tsv above " + sourceFile);
            }
            return Path.Combine(dir.FullName, "data", "fixtures", "pose-jump-gate", "cases.tsv");
        }

        [Fact]
        public void Fixture_EveryCasePublishesWhatItSays()
        {
            var gate = new PoseJumpGate();
            string? name = null;
            var answers = new StringBuilder();
            var expected = new StringBuilder();
            var failures = new List<string>();
            int cases = 0;

            void Finish()
            {
                if (name == null) return;
                cases++;
                if (answers.ToString() != expected.ToString())
                {
                    failures.Add(name + ": published " + answers + ", expected " + expected);
                }
            }

            foreach (string line in File.ReadAllLines(FixturePath()))
            {
                if (line.Length == 0 || line[0] == '#') continue;
                string[] fields = line.Split('\t');
                if (fields[0] == "case")
                {
                    Finish();
                    name = fields[1];
                    answers.Length = 0;
                    expected.Length = 0;
                    gate.Reset();
                    continue;
                }

                float yaw = float.Parse(fields[1], CultureInfo.InvariantCulture);
                float pitch = float.Parse(fields[2], CultureInfo.InvariantCulture);
                float roll = float.Parse(fields[3], CultureInfo.InvariantCulture);
                bool published = true;
                if (fields[0] == "announce")
                {
                    gate.Announce(yaw, pitch, roll);
                }
                else if (fields[0] == "pose")
                {
                    published = gate.Accept(yaw, pitch, roll);
                }
                else
                {
                    throw new InvalidOperationException("unknown row '" + fields[0] + "' in the fixture");
                }
                answers.Append(published ? '1' : '0');
                expected.Append(fields[4]);
            }
            Finish();

            Assert.True(failures.Count == 0, string.Join("\n", failures));
            Assert.Equal(15, cases);
        }

        [Fact]
        public void Receiver_TrackerRepeatingCentreAfterLosingTheHead_KeepsTheLastPose()
        {
            StartReceiver();

            Send(20.0, 0.0, 0.0);
            Assert.True(WaitForYaw(20f), "the first pose was not published");
            Send(20.5, 0.0, 0.0);
            Assert.True(WaitForYaw(20.5f), "a small step was not published");

            for (int i = 0; i < 5; i++) Send(0.0, 0.0, 0.0);
            Assert.True(WaitFor(() => _receiver!.FrozenPacketCount == 5), "the five lost-head packets were not all refused");
            Assert.Equal(20.5f, _receiver!.GetLatestPose().Yaw);

            Send(21.0, 0.0, 0.0);
            Assert.True(WaitForYaw(21f), "the head coming back was not published");
        }

        [Fact]
        public void Receiver_RefusedPackets_DoNotKeepTheDataFresh()
        {
            StartReceiver();

            Send(20.0, 0.0, 0.0);
            Assert.True(WaitForYaw(20f), "the first pose was not published");

            var elapsed = Stopwatch.StartNew();
            while (elapsed.ElapsedMilliseconds < 400)
            {
                Send(0.0, 0.0, 0.0);
                Thread.Sleep(20);
            }

            Assert.False(_receiver!.IsDataFresh(300));
            Assert.Equal(20f, _receiver.GetLatestPose().Yaw);
        }

        [Fact]
        public void Receiver_FastTurn_IsDelayedOnePacketAndThenFollowed()
        {
            StartReceiver();

            Send(0.0, 0.0, 0.0);
            Assert.True(WaitFor(() => _receiver!.IsDataFresh()), "the first pose was not published");
            Send(10.0, 0.0, 0.0);
            Assert.True(WaitFor(() => _receiver!.FrozenPacketCount == 1), "the first large step was not held");
            Assert.Equal(0f, _receiver!.GetLatestPose().Yaw);

            Send(20.0, 0.0, 0.0);
            Assert.True(WaitForYaw(20f), "the step after the held one was not published");
            Send(30.0, 0.0, 0.0);
            Assert.True(WaitForYaw(30f), "a continuing fast turn was held again");
            Assert.Equal(1L, _receiver.FrozenPacketCount);
        }

        [Fact]
        public void Receiver_CenterPressInTheTrailer_JumpsPastTheGateInOnePacket()
        {
            StartReceiver();

            Send(20.0, 0.0, 0.0);
            Assert.True(WaitForYaw(20f), "the off-centre pose was not published");

            Send(0.0, 0.0, 0.0, recenterCounter: 1);
            Assert.True(WaitForYaw(0f), "the trailered pose was held by the gate");
            Assert.Equal(0L, _receiver!.FrozenPacketCount);
            Assert.False(_receiver.TryConsumeRecenterRequest());

            // The rest of the burst carries the same counter and the same pose.
            Send(0.0, 0.0, 0.0, recenterCounter: 1);
            Send(0.5, 0.0, 0.0);
            Assert.True(WaitForYaw(0.5f), "the pose after the burst was not published");
            Assert.Equal(0L, _receiver.FrozenPacketCount);
        }

        [Fact]
        public void Receiver_StartAfterStop_ForgetsTheLastStream()
        {
            StartReceiver();
            Send(20.0, 0.0, 0.0);
            Assert.True(WaitForYaw(20f), "the first pose was not published");
            Send(0.0, 0.0, 0.0);
            Assert.True(WaitFor(() => _receiver!.FrozenPacketCount == 1), "the jump was not held");

            _receiver!.Stop();
            Assert.True(_receiver.Start(TestPort));
            Assert.Equal(0L, _receiver.FrozenPacketCount);

            Send(-40.0, 0.0, 0.0);
            Assert.True(WaitForYaw(-40f), "the first pose of the new stream was held against the old one");
        }

        private void StartReceiver()
        {
            _receiver = new OpenTrackReceiver();
            Assert.True(_receiver.Start(TestPort));
            _sender = new UdpClient();
        }

        // One socket for the whole stream, so the datagrams arrive in the order sent.
        private void Send(double yaw, double pitch, double roll, byte? recenterCounter = null)
        {
            byte[] packet = new byte[recenterCounter.HasValue ? OpenTrackPacket.PacketSizeWithTrailer : OpenTrackPacket.MinPacketSize];
            Array.Copy(BitConverter.GetBytes(yaw), 0, packet, OpenTrackPacket.YawOffset, 8);
            Array.Copy(BitConverter.GetBytes(pitch), 0, packet, OpenTrackPacket.PitchOffset, 8);
            Array.Copy(BitConverter.GetBytes(roll), 0, packet, OpenTrackPacket.RollOffset, 8);
            if (recenterCounter.HasValue)
            {
                packet[OpenTrackPacket.TrailerOffset] = OpenTrackPacket.TrailerMagic0;
                packet[OpenTrackPacket.TrailerOffset + 1] = OpenTrackPacket.TrailerMagic1;
                packet[OpenTrackPacket.TrailerOffset + 2] = OpenTrackPacket.TrailerMagic2;
                packet[OpenTrackPacket.TrailerOffset + 3] = OpenTrackPacket.TrailerMagic3;
                packet[OpenTrackPacket.TrailerOffset + 4] = OpenTrackPacket.TrailerVersion;
                packet[OpenTrackPacket.RecenterCounterOffset] = recenterCounter.Value;
            }
            _sender!.Send(packet, packet.Length, new IPEndPoint(IPAddress.Loopback, TestPort));
        }

        private bool WaitForYaw(float yaw)
        {
            return WaitFor(() => _receiver!.GetLatestPose().Yaw == yaw && _receiver.IsDataFresh());
        }

        private static bool WaitFor(Func<bool> condition)
        {
            var elapsed = Stopwatch.StartNew();
            while (elapsed.ElapsedMilliseconds < 2000)
            {
                if (condition()) return true;
                Thread.Sleep(5);
            }
            return condition();
        }
    }
}
