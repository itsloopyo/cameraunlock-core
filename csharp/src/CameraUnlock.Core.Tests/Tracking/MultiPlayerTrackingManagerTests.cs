// Null-argument constructor tests intentionally pass null literals
#pragma warning disable CS8625

using System;
using System.Net;
using System.Net.Sockets;
using System.Threading;
using Xunit;
using CameraUnlock.Core.Data;
using CameraUnlock.Core.Tracking;

namespace CameraUnlock.Core.Tests.Tracking
{
    public class MultiPlayerTrackingManagerTests : IDisposable
    {
        private const int BasePort = 14250;
        private const float FrameTime = 1f / 60f;

        private readonly MultiPlayerTrackingManager _manager;

        public MultiPlayerTrackingManagerTests()
        {
            _manager = new MultiPlayerTrackingManager(new[] { BasePort, BasePort + 1 });
        }

        public void Dispose()
        {
            _manager.Dispose();
        }

        [Fact]
        public void Constructor_NullPorts_Throws()
        {
            Assert.Throws<ArgumentNullException>(() => new MultiPlayerTrackingManager(null));
        }

        [Fact]
        public void Constructor_EmptyPorts_Throws()
        {
            Assert.Throws<ArgumentException>(() => new MultiPlayerTrackingManager(new int[0]));
        }

        [Fact]
        public void PlayerCount_MatchesPorts()
        {
            Assert.Equal(2, _manager.PlayerCount);
        }

        [Fact]
        public void Mode_Default_IsRotationAndPosition()
        {
            Assert.Equal(TrackingMode.RotationAndPosition, _manager.Mode);
        }

        [Fact]
        public void Mode_PropagatesToAllSessions()
        {
            _manager.Mode = TrackingMode.RotationOnly;

            Assert.Equal(TrackingMode.RotationOnly, _manager.GetSession(0).Mode);
            Assert.Equal(TrackingMode.RotationOnly, _manager.GetSession(1).Mode);
        }

        [Fact]
        public void CycleMode_AdvancesThroughAllModesAndWraps()
        {
            Assert.Equal(TrackingMode.RotationOnly, _manager.CycleMode());
            Assert.Equal(TrackingMode.PositionOnly, _manager.CycleMode());
            Assert.Equal(TrackingMode.RotationAndPosition, _manager.CycleMode());

            Assert.Equal(TrackingMode.RotationAndPosition, _manager.GetSession(1).Mode);
        }

        [Fact]
        public void Update_NoData_NoPlayerHasPose()
        {
            _manager.Update(FrameTime);

            Assert.False(_manager.HasPose(0));
            Assert.False(_manager.HasPose(1));
        }

        [Fact]
        public void IsAnyReceiving_NoData_IsFalse()
        {
            Assert.False(_manager.IsAnyReceiving);
        }

        [Fact]
        public void GetConnectionStatus_NoData_ReportsNoPlayers()
        {
            Assert.Equal("No players connected", _manager.GetConnectionStatus());
        }

        [Fact]
        public void Start_OnlyPlayerOneSendsData_OnlyPlayerOneHasPose()
        {
            _manager.ApplyStabilizationFrames(int.MaxValue);
            _manager.Start();
            SendTestPacket(BasePort, yaw: 30.0, pitch: 10.0, roll: 0.0);
            WaitForData(0);

            _manager.Update(FrameTime);

            Assert.True(_manager.HasPose(0));
            Assert.False(_manager.HasPose(1));
            Assert.True(_manager.IsReceiving(0));
            Assert.False(_manager.IsReceiving(1));
            Assert.True(_manager.IsAnyReceiving);
            Assert.Equal("Players 1 connected", _manager.GetConnectionStatus());
            Assert.True(_manager.GetSession(0).Rotation.Yaw > 0f);
        }

        [Fact]
        public void Reset_ClearsPoses()
        {
            _manager.ApplyStabilizationFrames(int.MaxValue);
            _manager.Start();
            SendTestPacket(BasePort, yaw: 30.0, pitch: 0.0, roll: 0.0);
            WaitForData(0);
            _manager.Update(FrameTime);

            _manager.Reset();

            Assert.False(_manager.HasPose(0));
        }

        [Fact]
        public void ApplySensitivity_ScalesProcessedRotation()
        {
            _manager.ApplyStabilizationFrames(int.MaxValue);
            _manager.ApplySmoothing(0f, 0f);
            _manager.Start();
            SendTestPacket(BasePort, yaw: 20.0, pitch: 0.0, roll: 0.0);
            WaitForData(0);

            // Settle smoothing at sensitivity 1, capture yaw, then double sensitivity.
            for (int i = 0; i < 60; i++) _manager.Update(FrameTime);
            float baseYaw = _manager.GetSession(0).Rotation.Yaw;

            _manager.ApplySensitivity(new SensitivitySettings(2f, 2f, 2f));
            SendTestPacket(BasePort, yaw: 20.0, pitch: 0.0, roll: 0.0);
            WaitForData(0);
            for (int i = 0; i < 60; i++) _manager.Update(FrameTime);
            float scaledYaw = _manager.GetSession(0).Rotation.Yaw;

            Assert.True(System.Math.Abs(scaledYaw - 2f * baseYaw) < 1f,
                $"Expected ~{2f * baseYaw}, got {scaledYaw}");
        }

        [Fact]
        public void ApplySmoothing_SetsBothParametersOnEverySession()
        {
            _manager.ApplySmoothing(0.25f, 0.75f);

            for (int i = 0; i < _manager.PlayerCount; i++)
            {
                Assert.Equal(0.25f, _manager.GetSession(i).LocalSmoothing);
                Assert.Equal(0.75f, _manager.GetSession(i).RemoteSmoothing);
            }
        }

        // A config-reload handler naturally applies smoothing then position settings, which
        // was the order that used to silently reset position smoothing to the struct's
        // values. Both orders must now land identically.
        private static PositionSettings ProbeSettings()
        {
            return PositionSettings.Symmetric(2f, 2f, 2f, 0.31f, 0.21f, 0.41f, 0.11f, 0f, 0f);
        }

        private void AssertSmoothingSurvivedSettings()
        {
            Assert.Equal(0.25f, _manager.LocalSmoothing);
            Assert.Equal(0.75f, _manager.RemoteSmoothing);
            for (int i = 0; i < _manager.PlayerCount; i++)
            {
                Assert.Equal(0.25f, _manager.GetSession(i).LocalSmoothing);
                Assert.Equal(0.75f, _manager.GetSession(i).RemoteSmoothing);
                Assert.Equal(0.25f, _manager.GetSession(i).PositionSettings.LocalSmoothing);
                Assert.Equal(0.75f, _manager.GetSession(i).PositionSettings.RemoteSmoothing);
                Assert.Equal(0.41f, _manager.GetSession(i).PositionSettings.LimitZ);
                Assert.Equal(0.11f, _manager.GetSession(i).PositionSettings.LimitZBack);
            }
        }

        [Fact]
        public void ApplySmoothing_ThenApplyPositionSettings_SmoothingSurvives()
        {
            _manager.ApplySmoothing(0.25f, 0.75f);
            _manager.ApplyPositionSettings(ProbeSettings());

            AssertSmoothingSurvivedSettings();
        }

        [Fact]
        public void ApplyPositionSettings_ThenApplySmoothing_SmoothingSurvives()
        {
            _manager.ApplyPositionSettings(ProbeSettings());
            _manager.ApplySmoothing(0.25f, 0.75f);

            AssertSmoothingSurvivedSettings();
        }

        [Fact]
        public void ApplyPositionSettings_Default_DoesNotResetSmoothing()
        {
            // The exact reported reproduction: ApplySmoothing then ApplyPositionSettings
            // with the Default struct, which carries 0.0 / 0.15.
            _manager.ApplySmoothing(0.25f, 0.75f);
            _manager.ApplyPositionSettings(PositionSettings.Default);

            for (int i = 0; i < _manager.PlayerCount; i++)
            {
                Assert.Equal(0.25f, _manager.GetSession(i).PositionSettings.LocalSmoothing);
                Assert.Equal(0.75f, _manager.GetSession(i).PositionSettings.RemoteSmoothing);
            }
        }

        [Fact]
        public void IsRemoteConnection_IsPerPlayer_NotShared()
        {
            // Only player 1 gets data, and it arrives over loopback. Player 1 must
            // report a local connection; player 2, which never received anything,
            // must not have been touched by player 1's flag.
            _manager.ApplyStabilizationFrames(int.MaxValue);
            _manager.Start();
            SendTestPacket(BasePort, yaw: 5.0, pitch: 0.0, roll: 0.0);
            WaitForData(0);
            _manager.Update(FrameTime);

            Assert.True(_manager.IsReceiving(0));
            Assert.False(_manager.IsRemoteConnection(0), "A loopback sender is a local connection");
            Assert.False(_manager.IsRemoteConnection(1), "Player 2 received nothing and must not inherit player 1's flag");
        }

        [Fact]
        public void Dispose_ThenStart_Throws()
        {
            var manager = new MultiPlayerTrackingManager(new[] { BasePort + 5 });
            manager.Dispose();

            Assert.Throws<ObjectDisposedException>(() => manager.Start());
        }

        private void WaitForData(int playerIndex)
        {
            for (int i = 0; i < 50; i++)
            {
                if (_manager.IsReceiving(playerIndex)) return;
                Thread.Sleep(10);
            }
            Assert.Fail($"Player {playerIndex} never reported fresh data");
        }

        /// <summary>
        /// Sends a test OpenTrack packet (48 bytes, 6 doubles: x, y, z, yaw, pitch, roll).
        /// </summary>
        private static void SendTestPacket(int port, double yaw, double pitch, double roll)
        {
            byte[] packet = new byte[48];

            Array.Copy(BitConverter.GetBytes(0.0), 0, packet, 0, 8);
            Array.Copy(BitConverter.GetBytes(0.0), 0, packet, 8, 8);
            Array.Copy(BitConverter.GetBytes(0.0), 0, packet, 16, 8);
            Array.Copy(BitConverter.GetBytes(yaw), 0, packet, 24, 8);
            Array.Copy(BitConverter.GetBytes(pitch), 0, packet, 32, 8);
            Array.Copy(BitConverter.GetBytes(roll), 0, packet, 40, 8);

            using (var client = new UdpClient())
            {
                client.Send(packet, packet.Length, new IPEndPoint(IPAddress.Loopback, port));
            }
        }
    }
}
