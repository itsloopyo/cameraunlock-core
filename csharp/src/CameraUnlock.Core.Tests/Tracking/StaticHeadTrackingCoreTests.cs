// Test files may dereference nullable properties
#pragma warning disable CS8602 // Dereference of a possibly null reference

using System;
using Xunit;
using CameraUnlock.Core.Config;
using CameraUnlock.Core.Data;
using CameraUnlock.Core.Math;
using CameraUnlock.Core.Tracking;

namespace CameraUnlock.Core.Tests.Tracking
{
    public class StaticHeadTrackingCoreTests : IDisposable
    {
        private readonly TestConfig _config;

        public StaticHeadTrackingCoreTests()
        {
            _config = new TestConfig();
        }

        public void Dispose()
        {
            // Ensure clean state between tests
            StaticHeadTrackingCore.Shutdown();
        }

        [Fact]
        public void IsInitialized_BeforeInitialize_ReturnsFalse()
        {
            Assert.False(StaticHeadTrackingCore.IsInitialized);
        }

        [Fact]
        public void Initialize_TakesBothSmoothingParametersFromConfig()
        {
            _config.LocalSmoothing = 0.2f;
            _config.RemoteSmoothing = 0.6f;

            StaticHeadTrackingCore.Initialize(_config);

            Assert.Equal(0.2f, StaticHeadTrackingCore.LocalSmoothing);
            Assert.Equal(0.6f, StaticHeadTrackingCore.RemoteSmoothing);
            Assert.Equal(0.2f, StaticHeadTrackingCore.Processor.LocalSmoothing);
            Assert.Equal(0.6f, StaticHeadTrackingCore.Processor.RemoteSmoothing);
        }

        [Fact]
        public void GetProcessedPose_RefreshesConnectionFlag_WithoutUpdate()
        {
            // Update() early-returns when the receiver is not receiving, so the flag copy
            // cannot live there alone: GetProcessedPose runs the processor on its own and
            // must refresh the flag itself, or a mod calling it directly smooths with a
            // stale connection and silently gets the wrong parameter.
            StaticHeadTrackingCore.Initialize(_config);
            StaticHeadTrackingCore.Processor.IsRemoteConnection = true;

            StaticHeadTrackingCore.GetProcessedPose(1f / 60f);

            // No packets have arrived, so the receiver reports a local connection and the
            // poisoned flag must have been overwritten rather than left standing.
            Assert.False(StaticHeadTrackingCore.Receiver.IsRemoteConnection);
            Assert.False(StaticHeadTrackingCore.Processor.IsRemoteConnection);
        }

        [Fact]
        public void Initialize_SetsIsInitializedTrue()
        {
            StaticHeadTrackingCore.Initialize(_config);

            Assert.True(StaticHeadTrackingCore.IsInitialized);
        }

        [Fact]
        public void Initialize_WhenAlreadyInitialized_DoesNotThrow()
        {
            StaticHeadTrackingCore.Initialize(_config);

            // Should not throw, just log and return
            StaticHeadTrackingCore.Initialize(_config);

            Assert.True(StaticHeadTrackingCore.IsInitialized);
        }

        [Fact]
        public void Initialize_SetsEnabledFromConfig()
        {
            _config.EnableOnStartup = true;
            StaticHeadTrackingCore.Initialize(_config);

            Assert.True(StaticHeadTrackingCore.IsEnabled);
        }

        [Fact]
        public void Initialize_WithDisabledConfig_SetsEnabledFalse()
        {
            _config.EnableOnStartup = false;
            StaticHeadTrackingCore.Initialize(_config);

            Assert.False(StaticHeadTrackingCore.IsEnabled);
        }

        [Fact]
        public void Shutdown_ClearsInitializedState()
        {
            StaticHeadTrackingCore.Initialize(_config);

            StaticHeadTrackingCore.Shutdown();

            Assert.False(StaticHeadTrackingCore.IsInitialized);
        }

        [Fact]
        public void Shutdown_ClearsReceiver()
        {
            StaticHeadTrackingCore.Initialize(_config);

            StaticHeadTrackingCore.Shutdown();

            Assert.Null(StaticHeadTrackingCore.Receiver);
        }

        [Fact]
        public void Shutdown_ClearsProcessor()
        {
            StaticHeadTrackingCore.Initialize(_config);

            StaticHeadTrackingCore.Shutdown();

            Assert.Null(StaticHeadTrackingCore.Processor);
        }

        [Fact]
        public void Shutdown_ClearsConfig()
        {
            StaticHeadTrackingCore.Initialize(_config);

            StaticHeadTrackingCore.Shutdown();

            Assert.Null(StaticHeadTrackingCore.Config);
        }

        [Fact]
        public void Shutdown_ResetsSmoothingToDefaults()
        {
            // The two values belong to the session that is ending. Left populated, the
            // static getters keep reporting the previous session's config after shutdown
            // and a caller reading them before the next Initialize() gets stale values
            // that look perfectly plausible.
            _config.LocalSmoothing = 0.4f;
            _config.RemoteSmoothing = 0.6f;
            StaticHeadTrackingCore.Initialize(_config);
            Assert.Equal(0.4f, StaticHeadTrackingCore.LocalSmoothing);

            StaticHeadTrackingCore.Shutdown();

            Assert.Equal(SmoothingUtils.DefaultLocalSmoothing, StaticHeadTrackingCore.LocalSmoothing);
            Assert.Equal(SmoothingUtils.DefaultRemoteSmoothing, StaticHeadTrackingCore.RemoteSmoothing);
        }

        [Fact]
        public void Initialize_OverwritesSmoothingSetBeforehand()
        {
            // Pins the documented contract: config is the source of truth at startup, so
            // a value set BEFORE Initialize does not survive it. The comment on the fields
            // used to claim the opposite.
            StaticHeadTrackingCore.LocalSmoothing = 0.9f;
            StaticHeadTrackingCore.RemoteSmoothing = 0.95f;

            _config.LocalSmoothing = 0.4f;
            _config.RemoteSmoothing = 0.6f;
            StaticHeadTrackingCore.Initialize(_config);

            Assert.Equal(0.4f, StaticHeadTrackingCore.LocalSmoothing);
            Assert.Equal(0.6f, StaticHeadTrackingCore.RemoteSmoothing);
            Assert.Equal(0.4f, StaticHeadTrackingCore.Processor.LocalSmoothing);
            Assert.Equal(0.6f, StaticHeadTrackingCore.Processor.RemoteSmoothing);
        }

        [Fact]
        public void SmoothingSetAfterInitialize_ReachesTheProcessor()
        {
            StaticHeadTrackingCore.Initialize(_config);

            StaticHeadTrackingCore.LocalSmoothing = 0.9f;
            StaticHeadTrackingCore.RemoteSmoothing = 0.95f;

            Assert.Equal(0.9f, StaticHeadTrackingCore.Processor.LocalSmoothing);
            Assert.Equal(0.95f, StaticHeadTrackingCore.Processor.RemoteSmoothing);
        }

        [Fact]
        public void GetProcessedPose_WhenNotInitialized_ReturnsZeroPose()
        {
            TrackingPose pose = StaticHeadTrackingCore.GetProcessedPose(1f / 60f);

            Assert.Equal(0f, pose.Yaw);
            Assert.Equal(0f, pose.Pitch);
            Assert.Equal(0f, pose.Roll);
        }

        [Fact]
        public void GetProcessedRotation_WhenNotInitialized_ReturnsZeros()
        {
            StaticHeadTrackingCore.GetProcessedRotation(1f / 60f, out float yaw, out float pitch, out float roll);

            Assert.Equal(0f, yaw);
            Assert.Equal(0f, pitch);
            Assert.Equal(0f, roll);
        }

        [Fact]
        public void IsEnabled_CanBeSet()
        {
            StaticHeadTrackingCore.Initialize(_config);

            StaticHeadTrackingCore.IsEnabled = false;
            Assert.False(StaticHeadTrackingCore.IsEnabled);

            StaticHeadTrackingCore.IsEnabled = true;
            Assert.True(StaticHeadTrackingCore.IsEnabled);
        }

        [Fact]
        public void Toggle_InvertsEnabledState()
        {
            _config.EnableOnStartup = true;
            StaticHeadTrackingCore.Initialize(_config);
            Assert.True(StaticHeadTrackingCore.IsEnabled);

            bool result = StaticHeadTrackingCore.Toggle();

            Assert.False(result);
            Assert.False(StaticHeadTrackingCore.IsEnabled);
        }

        [Fact]
        public void Toggle_ReturnsNewState()
        {
            _config.EnableOnStartup = false;
            StaticHeadTrackingCore.Initialize(_config);

            bool result = StaticHeadTrackingCore.Toggle();

            Assert.True(result);
        }

        [Fact]
        public void Update_WhenNotInitialized_ReturnsFalse()
        {
            bool result = StaticHeadTrackingCore.Update(1f / 60f);

            Assert.False(result);
        }

        [Fact]
        public void Update_WhenDisabled_ReturnsFalse()
        {
            StaticHeadTrackingCore.Initialize(_config);
            StaticHeadTrackingCore.IsEnabled = false;

            bool result = StaticHeadTrackingCore.Update(1f / 60f);

            Assert.False(result);
        }

        [Fact]
        public void IsReceiving_WhenNotInitialized_ReturnsFalse()
        {
            Assert.False(StaticHeadTrackingCore.IsReceiving);
        }

        [Fact]
        public void IsRemoteConnection_WhenNotInitialized_ReturnsFalse()
        {
            Assert.False(StaticHeadTrackingCore.IsRemoteConnection);
        }

        [Fact]
        public void Processor_AfterInitialize_IsNotNull()
        {
            StaticHeadTrackingCore.Initialize(_config);

            Assert.NotNull(StaticHeadTrackingCore.Processor);
        }

        [Fact]
        public void Receiver_AfterInitialize_IsNotNull()
        {
            StaticHeadTrackingCore.Initialize(_config);

            Assert.NotNull(StaticHeadTrackingCore.Receiver);
        }

        [Fact]
        public void Config_AfterInitialize_IsSet()
        {
            StaticHeadTrackingCore.Initialize(_config);

            Assert.Same(_config, StaticHeadTrackingCore.Config);
        }

        [Fact]
        public void Processor_UsesSensitivityFromConfig()
        {
            _config.Sensitivity = new SensitivitySettings(2f, 0.5f, 1.5f, false, false, false);
            StaticHeadTrackingCore.Initialize(_config);

            Assert.Equal(_config.Sensitivity, StaticHeadTrackingCore.Processor.Sensitivity);
        }

        [Fact]
        public void Reset_DoesNotThrow_WhenNotInitialized()
        {
            // Should not throw
            StaticHeadTrackingCore.Reset();
        }

        [Fact]
        public void Recenter_DoesNotThrow_WhenNotInitialized()
        {
            // Should not throw
            StaticHeadTrackingCore.Recenter();
        }

        [Fact]
        public void GetAimScreenOffset_CalculatesCorrectly()
        {
            StaticHeadTrackingCore.GetAimScreenOffset(
                10f, 5f, 0f,
                90f, 60f,
                1920f, 1080f,
                out float offsetX, out float offsetY);

            // With 90 degree horizontal FOV, 10 degree yaw should give roughly 1920 * (10/90) offset
            Assert.True(offsetX != 0f);
            Assert.True(offsetY != 0f);
        }

        [Fact]
        public void GetAimScreenPosition_ReturnsOffsetFromCenter()
        {
            StaticHeadTrackingCore.GetAimScreenPosition(
                0f, 0f, 0f,
                90f, 60f,
                1920f, 1080f,
                out float posX, out float posY);

            // At zero angles, position should be at screen center
            Assert.Equal(960f, posX, precision: 1);
            Assert.Equal(540f, posY, precision: 1);
        }

        /// <summary>
        /// Test configuration implementation for unit tests.
        /// </summary>
        private class TestConfig : IHeadTrackingConfig
        {
            public int UdpPort { get; set; } = 4242;
            public bool EnableOnStartup { get; set; } = true;
            public SensitivitySettings Sensitivity { get; set; } = SensitivitySettings.Default;
            public string RecenterKeyName { get; set; } = "Home";
            public string ToggleKeyName { get; set; } = "End";
            public bool AimDecouplingEnabled { get; set; } = true;
            public bool ShowDecoupledReticle { get; set; } = true;
            public float[] ReticleColorRgba { get; set; } = new[] { 1f, 1f, 1f, 1f };
            public float LocalSmoothing { get; set; } = 0f;
            public float RemoteSmoothing { get; set; } = 0.15f;
        }
    }
}
