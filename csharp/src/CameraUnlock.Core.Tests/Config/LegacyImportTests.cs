using System;
using System.Collections.Generic;
using CameraUnlock.Core.Config;
using Xunit;

namespace CameraUnlock.Core.Tests.Config
{
    /// <summary>
    /// The legacy import support: ImportResult's factories, the dropped-value lines, N2
    /// (LegacyNormalisations.FiniteOrDefault), N3 (LegacyNormalisations.KeyCodeToBindings), pose shaping (LegacyPoseShaping) and a LegacyImport
    /// over a config class. The C++ twin
    /// is cpp/tests/legacy_import_tests.cpp.
    /// </summary>
    public class LegacyImportTests
    {
        [Fact]
        public void TheFactoriesHoldEachStatusToItsFields()
        {
            ImportResult imported = ImportResult.Imported(new[] { new DroppedValue(DropRule.Reticle, "Reticle", "ShowReticle", "false") });
            Assert.Equal(ImportStatus.Imported, imported.Status);
            Assert.Equal("", imported.Reason);
            Assert.Single(imported.Dropped);

            ImportResult absent = ImportResult.Absent(new DroppedValue[0]);
            Assert.Equal(ImportStatus.Absent, absent.Status);
            Assert.Equal("", absent.Reason);
            Assert.Empty(absent.Dropped);

            ImportResult refused = ImportResult.Refused("FieldOfView=30 is outside 60 to 120");
            Assert.Equal(ImportStatus.Refused, refused.Status);
            Assert.Equal("FieldOfView=30 is outside 60 to 120", refused.Reason);
            Assert.Empty(refused.Dropped);

            ImportResult undecodable = ImportResult.Undecodable("the file is not UTF-8");
            Assert.Equal(ImportStatus.Undecodable, undecodable.Status);
            Assert.Equal("the file is not UTF-8", undecodable.Reason);
        }

        [Fact]
        public void TheFactoriesRefuseMissingParts()
        {
            Assert.StartsWith("a refused or undecodable import needs a reason",
                Assert.Throws<ArgumentException>(() => ImportResult.Refused("")).Message);
            Assert.Throws<ArgumentException>(() => ImportResult.Undecodable(""));
            Assert.Throws<ArgumentNullException>(() => ImportResult.Refused(null!));
            Assert.Throws<ArgumentNullException>(() => ImportResult.Imported(null!));
            Assert.Throws<ArgumentNullException>(() => ImportResult.Absent(new DroppedValue[] { null! }));
        }

        [Fact]
        public void TheDroppedListIsACopy()
        {
            var dropped = new List<DroppedValue> { new DroppedValue(DropRule.Reticle, "Reticle", "ShowReticle", "true") };
            ImportResult result = ImportResult.Imported(dropped);
            dropped.Clear();
            Assert.Single(result.Dropped);
        }

        [Fact]
        public void TheNumbersMatchTheCppEnums()
        {
            Assert.Equal(0, (int)ImportStatus.Imported);
            Assert.Equal(1, (int)ImportStatus.Refused);
            Assert.Equal(2, (int)ImportStatus.Undecodable);
            Assert.Equal(3, (int)ImportStatus.Absent);
            Assert.Equal(1, (int)DropRule.NonFiniteNumber);
            Assert.Equal(2, (int)DropRule.PoseShaping);
            Assert.Equal(3, (int)DropRule.Reticle);
            Assert.Equal(4, (int)DropRule.FollowsDefault);
            Assert.Equal(5, (int)DropRule.KeyCodeOutOfRange);
            Assert.Equal(6, (int)DropRule.ModifierKey);
        }

        [Fact]
        public void EachRuleHasItsLine()
        {
            Assert.Equal("not carried: [Smoothing] RemoteSmoothing=nan, it is not a finite number, so the default is used",
                new DroppedValue(DropRule.NonFiniteNumber, "Smoothing", "RemoteSmoothing", "nan").Describe());
            Assert.Equal("not carried: [Sensitivity] YawSensitivity=1.5, sensitivity, scales, deadzones, response curves and "
                + "axis inversion are set in the tracker now, not in this mod",
                new DroppedValue(DropRule.PoseShaping, "Sensitivity", "YawSensitivity", "1.5").Describe());
            Assert.Equal("not carried: [Reticle] ShowReticle=false, this mod no longer draws or toggles a reticle",
                new DroppedValue(DropRule.Reticle, "Reticle", "ShowReticle", "false").Describe());
            Assert.Equal("not carried: [Position] CollisionEnabled=false, this setting now follows the mod's default",
                new DroppedValue(DropRule.FollowsDefault, "Position", "CollisionEnabled", "false").Describe());
            Assert.Equal("not carried: [Hotkeys] ToggleKey=0x230, it is not a key code from 0x01 to 0xFE, so the action is unbound",
                new DroppedValue(DropRule.KeyCodeOutOfRange, "Hotkeys", "ToggleKey", "0x230").Describe());
            Assert.Equal("not carried: [Hotkeys] YawModeKey=LeftShift, it is a Ctrl, Shift or Alt key, which goes down before the "
                + "key of any chord made with it, so it is unbound",
                new DroppedValue(DropRule.ModifierKey, "Hotkeys", "YawModeKey", "LeftShift").Describe());
        }

        [Fact]
        public void ADroppedValueRefusesAnUnknownRuleAndNulls()
        {
            Assert.Throws<ArgumentOutOfRangeException>(() => new DroppedValue((DropRule)9, "A", "B", "C"));
            Assert.Throws<ArgumentOutOfRangeException>(() => new DroppedValue((DropRule)0, "A", "B", "C"));
            Assert.Throws<ArgumentOutOfRangeException>(() => new DroppedValue((DropRule)7, "A", "B", "C"));
            Assert.Throws<ArgumentNullException>(() => new DroppedValue(DropRule.Reticle, null!, "B", "C"));
            Assert.Throws<ArgumentNullException>(() => new DroppedValue(DropRule.Reticle, "A", null!, "C"));
            Assert.Throws<ArgumentNullException>(() => new DroppedValue(DropRule.Reticle, "A", "B", null!));
        }

        [Fact]
        public void N2KeepsAFiniteValue()
        {
            var dropped = new List<DroppedValue>();
            Assert.Equal(0.3f, LegacyNormalisations.FiniteOrDefault(0.3f, 0.15f, "Smoothing", "RemoteSmoothing", dropped));
            Assert.Equal(BitConverter.DoubleToInt64Bits(-0.0),
                BitConverter.DoubleToInt64Bits(LegacyNormalisations.FiniteOrDefault(-0.0, 0.5, "Camera", "Scale", dropped)));
            Assert.Equal(float.MaxValue, LegacyNormalisations.FiniteOrDefault(float.MaxValue, 0.15f, "A", "B", dropped));
            Assert.Empty(dropped);
        }

        [Fact]
        public void N2GivesTheDefaultForANonFiniteValueAndRecordsIt()
        {
            var dropped = new List<DroppedValue>();
            Assert.Equal(0.15f, LegacyNormalisations.FiniteOrDefault(float.NaN, 0.15f, "Smoothing", "RemoteSmoothing", dropped));
            Assert.Equal(0.2f, LegacyNormalisations.FiniteOrDefault(float.PositiveInfinity, 0.2f, "Position", "PositionLimitY", dropped));
            Assert.Equal(1.0f, LegacyNormalisations.FiniteOrDefault(float.NegativeInfinity, 1.0f, "Sensitivity", "YawSensitivity", dropped));
            Assert.Equal(0.5, LegacyNormalisations.FiniteOrDefault(double.NaN, 0.5, "Camera", "Scale", dropped));
            Assert.Equal(new[]
            {
                "NonFiniteNumber [Smoothing] RemoteSmoothing=nan",
                "NonFiniteNumber [Position] PositionLimitY=inf",
                "NonFiniteNumber [Sensitivity] YawSensitivity=-inf",
                "NonFiniteNumber [Camera] Scale=nan",
            }, dropped.ConvertAll(d => d.Rule + " [" + d.Section + "] " + d.Key + "=" + d.Value));
        }

        [Fact]
        public void N2RefusesANonFiniteDefaultAndNulls()
        {
            var dropped = new List<DroppedValue>();
            Assert.StartsWith("[Smoothing] RemoteSmoothing: the row default is not finite", Assert.Throws<ArgumentException>(
                () => LegacyNormalisations.FiniteOrDefault(1.0f, float.NaN, "Smoothing", "RemoteSmoothing", dropped)).Message);
            Assert.Throws<ArgumentException>(() => LegacyNormalisations.FiniteOrDefault(1.0, double.PositiveInfinity, "A", "B", dropped));
            Assert.Throws<ArgumentNullException>(() => LegacyNormalisations.FiniteOrDefault(1.0f, 0.0f, null!, "B", dropped));
            Assert.Throws<ArgumentNullException>(() => LegacyNormalisations.FiniteOrDefault(1.0f, 0.0f, "A", null!, dropped));
            Assert.Throws<ArgumentNullException>(() => LegacyNormalisations.FiniteOrDefault(1.0f, 0.0f, "A", "B", null!));
        }

        [Fact]
        public void N3KeepsAKeyAndLeavesNoneUnbound()
        {
            var dropped = new List<DroppedValue>();
            Assert.Equal("End", LegacyNormalisations.KeyCodeToBindings(279, "Hotkeys", "ToggleKey", dropped));
            Assert.Equal("F9", LegacyNormalisations.KeyCodeToBindings(290, "Hotkeys", "ToggleKey", dropped));
            Assert.Equal("LeftWindows", LegacyNormalisations.KeyCodeToBindings(311, "Hotkeys", "ToggleKey", dropped));
            Assert.Equal("", LegacyNormalisations.KeyCodeToBindings(0, "Hotkeys", "YawModeKey", dropped));
            Assert.Empty(dropped);
        }

        [Fact]
        public void N3UnbindsACtrlShiftOrAltKeyAndRecordsIt()
        {
            var dropped = new List<DroppedValue>();
            foreach (int code in new[] { 303, 304, 305, 306, 307, 308 })
            {
                Assert.Equal("", LegacyNormalisations.KeyCodeToBindings(code, "Hotkeys", "YawModeKey", dropped));
            }
            Assert.Equal(new[]
            {
                "ModifierKey [Hotkeys] YawModeKey=RightShift",
                "ModifierKey [Hotkeys] YawModeKey=LeftShift",
                "ModifierKey [Hotkeys] YawModeKey=RightControl",
                "ModifierKey [Hotkeys] YawModeKey=LeftControl",
                "ModifierKey [Hotkeys] YawModeKey=RightAlt",
                "ModifierKey [Hotkeys] YawModeKey=LeftAlt",
            }, dropped.ConvertAll(d => d.Rule + " [" + d.Section + "] " + d.Key + "=" + d.Value));
        }

        [Fact]
        public void N3RefusesAnUnnamedCodeAndNulls()
        {
            var dropped = new List<DroppedValue>();
            Assert.Throws<ArgumentException>(() => LegacyNormalisations.KeyCodeToBindings(999, "Hotkeys", "ToggleKey", dropped));
            Assert.Throws<ArgumentNullException>(() => LegacyNormalisations.KeyCodeToBindings(279, null!, "B", dropped));
            Assert.Throws<ArgumentNullException>(() => LegacyNormalisations.KeyCodeToBindings(279, "A", null!, dropped));
            Assert.Throws<ArgumentNullException>(() => LegacyNormalisations.KeyCodeToBindings(279, "A", "B", null!));
            Assert.Empty(dropped);
        }

        private static string PoseLine(PoseShapingValue p)
        {
            return "[" + p.Section + "] " + p.Key + "=" + p.Value + " shipped " + p.Shipped + (p.Folded ? " folded" : "");
        }

        [Fact]
        public void PoseShapingEqualToTheShippedValueIsFolded()
        {
            var pose = new List<PoseShapingValue>();
            var dropped = new List<DroppedValue>();
            LegacyPoseShaping.Record(true, true, "Tracking", "InvertRoll", pose, dropped);
            LegacyPoseShaping.Record(0.5f, 0.5f, "Tracking", "RollSensitivity", pose, dropped);
            LegacyPoseShaping.Record(-0.0f, 0.0f, "Tracking", "Deadzone", pose, dropped);
            Assert.Equal(new[]
            {
                "[Tracking] InvertRoll=true shipped true folded",
                "[Tracking] RollSensitivity=0.5 shipped 0.5 folded",
                "[Tracking] Deadzone=-0.0 shipped 0.0 folded",
            }, pose.ConvertAll(PoseLine));
            Assert.Empty(dropped);
        }

        [Fact]
        public void PoseShapingAPlayerChangedIsListedAndDropped()
        {
            var pose = new List<PoseShapingValue>();
            var dropped = new List<DroppedValue>();
            LegacyPoseShaping.Record(false, true, "Tracking", "InvertRoll", pose, dropped);
            LegacyPoseShaping.Record(2.0f, 1.0f, "Position", "SensitivityX", pose, dropped);
            LegacyPoseShaping.Record(0.15, 0.0, "Rotation", "YawDeadzone", pose, dropped);
            LegacyPoseShaping.Record(float.NaN, 1.0f, "Sensitivity", "YawSensitivity", pose, dropped);
            LegacyPoseShaping.Record(double.NegativeInfinity, 1.0, "Sensitivity", "PitchSensitivity", pose, dropped);
            Assert.Equal(new[]
            {
                "[Tracking] InvertRoll=false shipped true",
                "[Position] SensitivityX=2.0 shipped 1.0",
                "[Rotation] YawDeadzone=0.15 shipped 0.0",
                "[Sensitivity] YawSensitivity=nan shipped 1.0",
                "[Sensitivity] PitchSensitivity=-inf shipped 1.0",
            }, pose.ConvertAll(PoseLine));
            Assert.Equal(new[]
            {
                "PoseShaping [Tracking] InvertRoll=false",
                "PoseShaping [Position] SensitivityX=2.0",
                "PoseShaping [Rotation] YawDeadzone=0.15",
                "PoseShaping [Sensitivity] YawSensitivity=nan",
                "PoseShaping [Sensitivity] PitchSensitivity=-inf",
            }, dropped.ConvertAll(d => d.Rule + " [" + d.Section + "] " + d.Key + "=" + d.Value));
        }

        [Fact]
        public void PoseShapingRefusesANonFiniteShippedValueAndNulls()
        {
            var pose = new List<PoseShapingValue>();
            var dropped = new List<DroppedValue>();
            Assert.StartsWith("[Tracking] RollSensitivity: the shipped value is not finite", Assert.Throws<ArgumentException>(
                () => LegacyPoseShaping.Record(1.0f, float.NaN, "Tracking", "RollSensitivity", pose, dropped)).Message);
            Assert.Throws<ArgumentException>(() => LegacyPoseShaping.Record(1.0, double.PositiveInfinity, "A", "B", pose, dropped));
            Assert.Throws<ArgumentNullException>(() => LegacyPoseShaping.Record(true, false, null!, "B", pose, dropped));
            Assert.Throws<ArgumentNullException>(() => LegacyPoseShaping.Record(1.0f, 1.0f, "A", null!, pose, dropped));
            Assert.Throws<ArgumentNullException>(() => LegacyPoseShaping.Record(1.0f, 1.0f, "A", "B", null!, dropped));
            Assert.Throws<ArgumentNullException>(() => LegacyPoseShaping.Record(1.0, 2.0, "A", "B", pose, null!));
            Assert.Empty(pose);
            Assert.Empty(dropped);
        }

        [Fact]
        public void ImportedAndAbsentCarryThePoseShapingValues()
        {
            var value = new PoseShapingValue("Tracking", "InvertRoll", "true", "true", true);
            Assert.Single(ImportResult.Imported(new DroppedValue[0], new[] { value }).PoseShaping);
            Assert.Single(ImportResult.Absent(new DroppedValue[0], new[] { value }).PoseShaping);
            Assert.Empty(ImportResult.Imported(new DroppedValue[0]).PoseShaping);
            Assert.Empty(ImportResult.Refused("r").PoseShaping);
            Assert.Empty(ImportResult.Undecodable("u").PoseShaping);
            Assert.Throws<ArgumentNullException>(() => ImportResult.Imported(new DroppedValue[0], null!));
            Assert.Throws<ArgumentNullException>(() => ImportResult.Absent(new DroppedValue[0], new PoseShapingValue[] { null! }));
            Assert.Throws<ArgumentNullException>(() => new PoseShapingValue("A", "B", "C", null!, false));
        }

        private sealed class RuntimeConfig
        {
            public float RemoteSmoothing = 0.15f;
            public string ToggleKey = "End";
        }

        [Fact]
        public void AnImportRunsOnTheInputItIsHanded()
        {
            LegacyImportInput? seen = null;
            var import = new LegacyImport<RuntimeConfig>((input, config) =>
            {
                seen = input;
                var dropped = new List<DroppedValue>();
                config.RemoteSmoothing = LegacyNormalisations.FiniteOrDefault(float.NaN, new RuntimeConfig().RemoteSmoothing,
                    "Smoothing", "RemoteSmoothing", dropped);
                config.ToggleKey = "F9";
                return ImportResult.Imported(dropped);
            }, new[] { new LegacyKey("Smoothing", "RemoteSmoothing"), new LegacyKey("", "ToggleKey") });

            var target = new RuntimeConfig();
            ImportResult result = import.Run(new LegacyImportInput(@"C:\Game\BepInEx\config\a.cfg"), target);
            Assert.Equal(ImportStatus.Imported, result.Status);
            Assert.Single(result.Dropped);
            Assert.Equal(0.15f, target.RemoteSmoothing);
            Assert.Equal("F9", target.ToggleKey);
            Assert.Equal(@"C:\Game\BepInEx\config\a.cfg", seen!.Path);
            Assert.Equal(2, import.Keys.Count);
            Assert.Equal("", import.Keys[1].Section);
        }

        [Fact]
        public void TheImportPartsRefuseNullsAndEmpties()
        {
            Assert.Throws<ArgumentNullException>(() => new LegacyImport<RuntimeConfig>(null!, new LegacyKey[0]));
            Assert.Throws<ArgumentNullException>(() => new LegacyImport<RuntimeConfig>((i, c) => ImportResult.Absent(new DroppedValue[0]), null!));
            Assert.Throws<ArgumentNullException>(
                () => new LegacyImport<RuntimeConfig>((i, c) => ImportResult.Absent(new DroppedValue[0]), new LegacyKey[] { null! }));
            Assert.Throws<ArgumentException>(() => new LegacyKey("General", ""));
            Assert.Throws<ArgumentNullException>(() => new LegacyKey(null!, "Key"));
            Assert.Throws<ArgumentNullException>(() => new LegacyImportInput(null!));
            Assert.Throws<ArgumentException>(() => new LegacyImportInput(""));
        }
    }
}
