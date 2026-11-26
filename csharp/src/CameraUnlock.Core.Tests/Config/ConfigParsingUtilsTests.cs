using System;
using System.Collections.Generic;
using System.IO;
using Xunit;
using CameraUnlock.Core.Config;

namespace CameraUnlock.Core.Tests.Config
{
    public class ConfigParsingUtilsTests
    {
        [Fact]
        public void TryParseColor_Float01_ParsedAsIs()
        {
            Assert.True(ConfigParsingUtils.TryParseColor("1.0,0.5,0.25", out float[] rgba));
            Assert.Equal(1.0f, rgba[0], precision: 5);
            Assert.Equal(0.5f, rgba[1], precision: 5);
            Assert.Equal(0.25f, rgba[2], precision: 5);
            Assert.Equal(1.0f, rgba[3], precision: 5);
        }

        [Fact]
        public void TryParseColor_Byte0to255_ScaledTo01()
        {
            Assert.True(ConfigParsingUtils.TryParseColor("255,128,64", out float[] rgba));
            Assert.Equal(1.0f, rgba[0], precision: 3);
            Assert.Equal(128f / 255f, rgba[1], precision: 3);
            Assert.Equal(64f / 255f, rgba[2], precision: 3);
            Assert.Equal(1.0f, rgba[3], precision: 5);
        }

        [Fact]
        public void TryParseColor_WithAlpha_ParsesAllFour()
        {
            Assert.True(ConfigParsingUtils.TryParseColor("0.1,0.2,0.3,0.4", out float[] rgba));
            Assert.Equal(0.1f, rgba[0], precision: 5);
            Assert.Equal(0.4f, rgba[3], precision: 5);
        }

        [Fact]
        public void TryParseColor_InvalidAlpha_ReturnsFalse()
        {
            // Regression: previously silently defaulted alpha to 1.0 when the
            // alpha component was provided but unparseable, hiding config typos.
            Assert.False(ConfigParsingUtils.TryParseColor("1.0,0.5,0.25,abc", out _));
        }

        [Fact]
        public void TryParseColor_TooFewComponents_ReturnsFalse()
        {
            Assert.False(ConfigParsingUtils.TryParseColor("1.0,0.5", out _));
        }

        [Fact]
        public void TryParseColor_Empty_ReturnsFalse()
        {
            Assert.False(ConfigParsingUtils.TryParseColor("", out _));
            Assert.False(ConfigParsingUtils.TryParseColor((string)null!, out _));
        }

        [Fact]
        public void TryParseColor_MaxBasedDetection_TreatsLargestAsScaleHint()
        {
            // 200 makes it clear the user is in 0-255 mode; the small companions
            // get scaled the same way for consistency.
            Assert.True(ConfigParsingUtils.TryParseColor("0,0,200", out float[] rgba));
            Assert.Equal(0f, rgba[0], precision: 5);
            Assert.Equal(0f, rgba[1], precision: 5);
            Assert.Equal(200f / 255f, rgba[2], precision: 3);
        }

        [Fact]
        public void TryParseColor_ClampsNegativeIn01Mode()
        {
            // Pure 0-1 input with a negative component: 0-1 mode is preserved
            // (max is 1.0, not above), and the negative clamps to 0.
            Assert.True(ConfigParsingUtils.TryParseColor("-0.5,0.5,1.0", out float[] rgba));
            Assert.Equal(0f, rgba[0], precision: 5);
            Assert.Equal(0.5f, rgba[1], precision: 5);
            Assert.Equal(1.0f, rgba[2], precision: 5);
        }

        [Fact]
        public void TryParseColor_AlphaScaledIndependently()
        {
            // Regression: alpha=255 with 0-1 RGB used to scale RGB by 1/255 too.
            // Now RGB stays in 0-1 mode (max=1.0) and only alpha is divided by 255.
            Assert.True(ConfigParsingUtils.TryParseColor("1.0,0.5,0.25,255", out float[] rgba));
            Assert.Equal(1.0f, rgba[0], precision: 5);
            Assert.Equal(0.5f, rgba[1], precision: 5);
            Assert.Equal(0.25f, rgba[2], precision: 5);
            Assert.Equal(1.0f, rgba[3], precision: 5);
        }

        private static string WriteIni(string content)
        {
            string path = Path.Combine(Path.GetTempPath(), Path.GetRandomFileName() + ".ini");
            File.WriteAllText(path, content);
            return path;
        }

        // Regression: the dictionary is keyed OrdinalIgnoreCase, so the second line used to
        // overwrite the first. "UdpPort = 5555" then "udpport = seventy" left the receiver on
        // the 4242 default with nothing logged, while the C++ parser - which appends every
        // line and never dedups - kept 5555. A config file is a system boundary, and a key
        // set twice is an authoring error rather than something to pick a winner for.
        [Fact]
        public void ParseIniFile_DuplicateKey_Throws()
        {
            string path = WriteIni("UdpPort = 5555\nudpport = seventy\n");
            try
            {
                var ex = Assert.Throws<FormatException>(() => ConfigParsingUtils.ParseIniFile(path));
                Assert.Contains("udpport", ex.Message);
                Assert.Contains("line 2", ex.Message);
                Assert.Contains("line 1", ex.Message);
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Fact]
        public void ParseIniFile_DistinctKeys_AreAllKept()
        {
            string path = WriteIni("UdpPort = 5555\nLocalSmoothing = 0.2\n");
            try
            {
                Dictionary<string, string> values = ConfigParsingUtils.ParseIniFile(path);
                Assert.Equal("5555", values["udpport"]);
                Assert.Equal("0.2", values["LocalSmoothing"]);
            }
            finally
            {
                File.Delete(path);
            }
        }

        // Regression: StartsWith(string) is culture-sensitive and reads an ignorable leading
        // character as absent, so a line opening with U+00AD SOFT HYPHEN was a section header
        // here and a key line in the C++ twin, which compares trimmed[0]. Ordinal tests put
        // the two halves back on one answer.
        [Fact]
        public void ParseIniFile_LeadingSoftHyphen_IsNotASectionHeader()
        {
            string path = WriteIni("\u00AD[General]=1\nUdpPort=5555\n");
            try
            {
                Dictionary<string, string> values = ConfigParsingUtils.ParseIniFile(path);
                Assert.Equal("1", values["\u00AD[General]"]);
                Assert.Equal("5555", values["udpport"]);
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Fact]
        public void ParseIniFile_LeadingSoftHyphen_IsNotAComment()
        {
            string path = WriteIni("\u00AD#NotAComment=1\n");
            try
            {
                Dictionary<string, string> values = ConfigParsingUtils.ParseIniFile(path);
                Assert.Equal("1", values["\u00AD#NotAComment"]);
            }
            finally
            {
                File.Delete(path);
            }
        }

        // Same culture-sensitivity on the quote strip: the pair test passed for a value whose
        // first character was the soft hyphen, and Substring(1, Length - 2) then ate the hyphen
        // and the closing quote, handing the caller a mangled value.
        [Fact]
        public void ParseIniFile_QuoteStrip_OnlyFiresOnRealSurroundingQuotes()
        {
            string path = WriteIni("ToggleKey = \u00AD\"F10\"\n");
            try
            {
                Dictionary<string, string> values = ConfigParsingUtils.ParseIniFile(path);
                Assert.Equal("\u00AD\"F10\"", values["ToggleKey"]);
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Fact]
        public void ParseIniFile_QuoteStrip_StillStripsRealQuotes()
        {
            string path = WriteIni("ToggleKey = \"F10\"\n");
            try
            {
                Dictionary<string, string> values = ConfigParsingUtils.ParseIniFile(path);
                Assert.Equal("F10", values["ToggleKey"]);
            }
            finally
            {
                File.Delete(path);
            }
        }
    }
}
