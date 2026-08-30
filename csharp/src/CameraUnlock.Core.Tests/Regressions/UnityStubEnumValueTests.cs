using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Runtime.CompilerServices;
using System.Text.RegularExpressions;
using Xunit;

namespace CameraUnlock.Core.Tests.Regressions
{
    /// <summary>
    /// csharp/stubs/UnityStubs.cs is a reference assembly: an enum member compiled against it
    /// is inlined into the mod as its integer value, so the numbers ARE the contract and a
    /// wrong one ships silently in the built DLL. TextureFormat and RuntimePlatform were both
    /// written as bare 0..n sequences and every member of both was wrong.
    ///
    /// The expected values below were read out of the shipped enums with
    /// System.Reflection.Metadata, and are unanimous across eight games spanning Unity 5.3 to
    /// Unity 6: The Painscreek Killings, Crawl, Tacoma, Subnautica, Outer Wilds, Hardspace
    /// Shipbreaker, Dorfromantik and Valheim. Nothing here is from memory.
    ///
    /// The test reads the stub SOURCE rather than a built assembly because the stub targets
    /// net35/net472/net48 and this suite is net6.0, so there is no build of it this process
    /// can load. That is the point anyway: what has to be pinned is the declaration.
    /// </summary>
    public class UnityStubEnumValueTests
    {
        private static string RepoRoot([CallerFilePath] string sourceFile = "")
        {
            DirectoryInfo? dir = new DirectoryInfo(Path.GetDirectoryName(sourceFile)!);
            while (dir != null && !File.Exists(Path.Combine(dir.FullName, "csharp", "stubs", "UnityStubs.cs")))
            {
                dir = dir.Parent;
            }
            if (dir == null)
            {
                throw new InvalidOperationException("no csharp/stubs/UnityStubs.cs above " + sourceFile);
            }
            return dir.FullName;
        }

        private static string StubSource()
        {
            return File.ReadAllText(Path.Combine(RepoRoot(), "csharp", "stubs", "UnityStubs.cs"));
        }

        /// <summary>
        /// Pulls "Name = 12" pairs out of one enum declaration. A member without an explicit
        /// value is reported as null, which every assertion below treats as a failure: an
        /// implicit value is what produced both defects, because it shifts the moment anyone
        /// inserts a member above it.
        /// </summary>
        private static Dictionary<string, int?> ReadEnum(string source, string enumName)
        {
            Match declaration = Regex.Match(
                source,
                @"\benum\s+" + Regex.Escape(enumName) + @"\s*\{(?<body>[^}]*)\}");
            Assert.True(declaration.Success, "no enum " + enumName + " in csharp/stubs/UnityStubs.cs");

            var members = new Dictionary<string, int?>();
            foreach (string raw in declaration.Groups["body"].Value.Split(','))
            {
                string member = raw.Trim();
                if (member.Length == 0) continue;

                string[] halves = member.Split('=');
                string name = halves[0].Trim();
                if (halves.Length == 1)
                {
                    members[name] = null;
                    continue;
                }
                members[name] = int.Parse(halves[1].Trim(), CultureInfo.InvariantCulture);
            }
            return members;
        }

        private static void AssertMembers(string enumName, Dictionary<string, int> expected)
        {
            Dictionary<string, int?> declared = ReadEnum(StubSource(), enumName);

            foreach (KeyValuePair<string, int> want in expected)
            {
                Assert.True(declared.ContainsKey(want.Key), enumName + " no longer declares " + want.Key);
                Assert.True(
                    declared[want.Key].HasValue,
                    enumName + "." + want.Key + " has no explicit value. Every member needs one, "
                        + "or inserting a member above it silently renumbers it.");
                Assert.True(
                    declared[want.Key] == want.Value,
                    enumName + "." + want.Key + " is " + declared[want.Key] + " in the stub and "
                        + want.Value + " in every shipped Unity checked.");
            }

            foreach (KeyValuePair<string, int?> member in declared)
            {
                Assert.True(
                    expected.ContainsKey(member.Key),
                    enumName + "." + member.Key + " was added to the stub without a pinned value. "
                        + "Read it off a shipped assembly and add it here.");
            }
        }

        [Fact]
        public void TextureFormat_MatchesTheShippedEnum()
        {
            AssertMembers("TextureFormat", new Dictionary<string, int>
            {
                { "Alpha8", 1 },
                { "ARGB4444", 2 },
                { "RGB24", 3 },
                { "RGBA32", 4 },
                { "ARGB32", 5 },
                { "RGB565", 7 },
                { "R16", 9 },
                { "DXT1", 10 },
                { "DXT5", 12 },
            });
        }

        [Fact]
        public void RuntimePlatform_MatchesTheShippedEnum()
        {
            AssertMembers("RuntimePlatform", new Dictionary<string, int>
            {
                { "OSXEditor", 0 },
                { "OSXPlayer", 1 },
                { "WindowsPlayer", 2 },
                { "WindowsEditor", 7 },
                { "IPhonePlayer", 8 },
                { "Android", 11 },
                { "LinuxPlayer", 13 },
                { "WebGLPlayer", 17 },
            });
        }

        [Fact]
        public void HideFlags_MatchesTheShippedEnum()
        {
            AssertMembers("HideFlags", new Dictionary<string, int>
            {
                { "None", 0 },
                { "HideInHierarchy", 1 },
                { "HideInInspector", 2 },
                { "DontSaveInEditor", 4 },
                { "NotEditable", 8 },
                { "DontSaveInBuild", 16 },
                { "DontUnloadUnusedAsset", 32 },
                { "DontSave", 52 },
                { "HideAndDontSave", 61 },
            });
        }

        [Fact]
        public void CameraClearFlags_MatchesTheShippedEnum()
        {
            AssertMembers("CameraClearFlags", new Dictionary<string, int>
            {
                { "Skybox", 1 },
                { "Color", 2 },
                { "SolidColor", 2 },
                { "Depth", 3 },
                { "Nothing", 4 },
            });
        }

        [Fact]
        public void CameraType_MatchesTheShippedEnum()
        {
            AssertMembers("CameraType", new Dictionary<string, int>
            {
                { "Game", 1 },
                { "SceneView", 2 },
                { "Preview", 4 },
                { "VR", 8 },
                { "Reflection", 16 },
            });
        }

        [Fact]
        public void QueryTriggerInteraction_MatchesTheShippedEnum()
        {
            AssertMembers("QueryTriggerInteraction", new Dictionary<string, int>
            {
                { "UseGlobal", 0 },
                { "Ignore", 1 },
                { "Collide", 2 },
            });
        }

        [Fact]
        public void ShadowCastingMode_MatchesTheShippedEnum()
        {
            AssertMembers("ShadowCastingMode", new Dictionary<string, int>
            {
                { "Off", 0 },
                { "On", 1 },
                { "TwoSided", 2 },
                { "ShadowsOnly", 3 },
            });
        }

        [Fact]
        public void MotionVectorGenerationMode_MatchesTheShippedEnum()
        {
            AssertMembers("MotionVectorGenerationMode", new Dictionary<string, int>
            {
                { "Camera", 0 },
                { "Object", 1 },
                { "ForceNoMotion", 2 },
            });
        }

        [Fact]
        public void FindObjectsSortMode_MatchesTheShippedEnum()
        {
            AssertMembers("FindObjectsSortMode", new Dictionary<string, int>
            {
                { "None", 0 },
                { "InstanceID", 1 },
            });
        }

        /// <summary>
        /// The consts are inlined the same way the enum members are. Physics's three came off
        /// a shipped UnityEngine.PhysicsModule.dll: IgnoreRaycastLayer 4, DefaultRaycastLayers
        /// -5, AllLayers -1. GL's came off the same CoreModule as the enums above.
        /// </summary>
        [Theory]
        [InlineData("IgnoreRaycastLayer", "1 << 2")]
        [InlineData("DefaultRaycastLayers", "~(1 << 2)")]
        [InlineData("AllLayers", "~0")]
        [InlineData("LINES", "1")]
        [InlineData("TRIANGLES", "4")]
        [InlineData("QUADS", "7")]
        public void InlinedConsts_KeepTheirShippedValue(string name, string expression)
        {
            Assert.Contains(name + " = " + expression + ";", StubSource());
        }
    }
}
