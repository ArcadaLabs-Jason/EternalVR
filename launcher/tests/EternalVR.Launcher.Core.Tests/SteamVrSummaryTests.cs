using System.Collections.Generic;
using System.IO;
using System.Linq;
using EternalVR.Launcher.Core.Report;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>The chosen SteamVR settings in system.txt (Steam's config\steamvr.vrsettings).</summary>
    public class SteamVrSummaryTests
    {
        private const string Settings = @"{
   ""LastKnown"" : {
      ""ActualHMDDriver"" : ""lighthouse"",
      ""HMDManufacturer"" : ""Valve Corporation"",
      ""HMDModel"" : ""Index"",
      ""HMDSerialNumber"" : ""LHR-TEST0001""
   },
   ""driver_lighthouse"" : {
      ""preferredRefreshRate"" : 120,
      ""PowerManagedBaseStations2"" : ""TESTSTATION-01""
   },
   ""steam.app.782330"" : {
      ""motionSmoothingOverride"" : 1,
      ""supersampleScale"" : 0.8,
      ""worldScale"" : 1.25,
      ""lastPairedSerial"" : ""TEST-SERIAL-02""
   },
   ""steam.app.620980"" : {
      ""supersampleScale"" : 2
   },
   ""steamvr"" : {
      ""installID"" : ""1111222233334444555"",
      ""motionSmoothing"" : false,
      ""supersampleManualOverride"" : true,
      ""supersampleScale"" : 1.5,
      ""pairedDevices"" : { ""TEST-PAIRED-03"" : ""unknown"" }
   }
}";

        private static Dictionary<string, string> Lines(IReadOnlyList<KeyValuePair<string, string>> lines) =>
            lines.ToDictionary(kv => kv.Key, kv => kv.Value);

        [Fact]
        public void TheChosenSettingsAreSummarisedAndNothingElse()
        {
            var lines = SteamVrSummary.Summarize(Settings);
            Assert.Equal(new[]
            {
                new KeyValuePair<string, string>("steamvr headset", "Valve Corporation Index (driver lighthouse)"),
                new KeyValuePair<string, string>("steamvr supersampling", "manual override on, scale 1.5"),
                new KeyValuePair<string, string>("steamvr motion smoothing", "off"),
                new KeyValuePair<string, string>("steamvr supersample filtering", "not set"),
                new KeyValuePair<string, string>("steamvr refresh rate", "driver_lighthouse 120"),
                new KeyValuePair<string, string>("steamvr settings for DOOM Eternal (steam.app.782330)",
                    "motionSmoothingOverride 1, supersampleScale 0.8, worldScale 1.25"),
            }, lines);
            var all = string.Join("\n", lines.Select(kv => kv.Key + ": " + kv.Value));
            foreach (var secret in new[] { "LHR-TEST0001", "TESTSTATION", "TEST-SERIAL", "1111222233334444555", "TEST-PAIRED", "620980" })
                Assert.DoesNotContain(secret, all);
        }

        [Fact]
        public void AbsentKeysReadNotSet()
        {
            var lines = Lines(SteamVrSummary.Summarize(@"{ ""steamvr"" : { ""allowSupersampleFiltering"" : true }, ""LastKnown"" : { ""HMDModel"" : """" } }"));
            Assert.Equal("not set", lines["steamvr headset"]);
            Assert.Equal("manual override not set, scale not set", lines["steamvr supersampling"]);
            Assert.Equal("not set", lines["steamvr motion smoothing"]);
            Assert.Equal("on", lines["steamvr supersample filtering"]);
            Assert.Equal("not set", lines["steamvr refresh rate"]);
            Assert.Equal("none", lines["steamvr settings for DOOM Eternal"]);
            Assert.Equal("not set", Lines(SteamVrSummary.Summarize("{}"))["steamvr headset"]);
        }

        [Theory]
        [InlineData("{ \"steamvr\" : { \"supersampleScale\" : 1.5, }")]
        [InlineData("{ \"steamvr\" : ")]
        [InlineData("not json at all")]
        [InlineData("{ \"a\" : \"\\uZZZZ\" }")]
        [InlineData("{ \"a\" : 1.2.3 }")]
        [InlineData("")]
        [InlineData(null)]
        public void AMalformedFileSaysItCouldNotBeRead(string text)
        {
            var lines = SteamVrSummary.Summarize(text);
            var line = Assert.Single(lines);
            Assert.Equal("steamvr settings", line.Key);
            Assert.StartsWith("could not be read (", line.Value);
        }

        [Fact]
        public void NotAnObjectSaysItCouldNotBeRead()
        {
            Assert.Equal("could not be read (not a JSON object)", Assert.Single(SteamVrSummary.Summarize("[1, 2]")).Value);
        }

        [Fact]
        public void ReadFindsTheFileInSteamsFolderOrSaysWhy()
        {
            using (var t = new TempDir())
            {
                Assert.Equal("Steam's folder not found", Assert.Single(SteamVrSummary.Read(null)).Value);
                Assert.Equal(@"none (no " + Path.Combine("config", "steamvr.vrsettings") + " in Steam's folder)", Assert.Single(SteamVrSummary.Read(t.Combine("steam"))).Value);
                t.Write(@"steam\config\steamvr.vrsettings", Settings);
                Assert.Equal("manual override on, scale 1.5", Lines(SteamVrSummary.Read(t.Combine("steam")))["steamvr supersampling"]);
                t.Write(@"steam\config\steamvr.vrsettings", "{ broken");
                Assert.StartsWith("could not be read (FormatException", Assert.Single(SteamVrSummary.Read(t.Combine("steam"))).Value);
            }
        }

        [Fact]
        public void LongOrMultiLineValuesStayOnOneShortLine()
        {
            var lines = Lines(SteamVrSummary.Summarize("{ \"steam.app.782330\" : { \"note\" : \"" + new string('x', 200) + "\", \"two\" : \"a\\nb\", \"nested\" : { \"x\" : 1 }, \"blank\" : \"\" } }"));
            Assert.Equal("blank (empty), nested (not a single value), note " + new string('x', SteamVrSummary.MaxValueLength) + "..., two a b",
                lines["steamvr settings for DOOM Eternal (steam.app.782330)"]);
        }
    }
}
