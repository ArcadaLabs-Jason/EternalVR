using System;
using System.IO;
using System.Linq;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>
    /// "Get test builds" on the Advanced tab (<see cref="LauncherSettings.TestBuilds"/>, DECISIONS T-198): its key, default and
    /// round trip, and that it is this PC's like "Use the headset's graphics card": never in a profile, kept by Reset and by a
    /// profile switch.
    /// </summary>
    public class TestBuildsSettingTests
    {
        private static string[] Lines(string text) => text.Replace("\r\n", "\n").Split('\n');

        [Fact]
        public void OffByDefaultAndOffForAnOlderFile()
        {
            Assert.False(new LauncherSettings().TestBuilds);
            Assert.Contains("test_builds = 0", Lines(new LauncherSettings().Serialize()));
            // A file from a launcher without the setting has no such line: off.
            Assert.False(LauncherSettings.Parse("schema_version = 2\nturn = snap\nadapter_steer = 1\n").TestBuilds);
        }

        [Fact]
        public void ItRoundTripsAndIsAKnownKey()
        {
            var s = LauncherSettings.Parse("schema_version = 2\ntest_builds = 1\n");
            Assert.True(s.TestBuilds);
            // Known: read, not kept a second time as a key this launcher does not know.
            Assert.DoesNotContain(s.UnknownKeys, kv => kv.Key == "test_builds");
            var text = s.Serialize();
            Assert.Single(Lines(text), line => line.StartsWith("test_builds ", StringComparison.Ordinal));
            Assert.Contains("test_builds = 1", Lines(text));
            Assert.True(LauncherSettings.Parse(text).TestBuilds);
            Assert.True(LauncherSettings.Parse("schema_version = 2\ntest_builds = on\n").TestBuilds);
            Assert.False(LauncherSettings.Parse("schema_version = 2\ntest_builds = 0\n").TestBuilds);
            Assert.False(LauncherSettings.Parse("schema_version = 2\ntest_builds = maybe\n").TestBuilds);
            // The graphics card's switch, read and written beside it, is unchanged.
            var both = LauncherSettings.Parse("schema_version = 2\nadapter_steer = 1\ntest_builds = 0\n");
            Assert.True(both.AdapterSteer);
            Assert.False(both.TestBuilds);
            Assert.Contains("adapter_steer = 1", Lines(both.Serialize()));
        }

        [Fact]
        public void ItIsThisPcsAndNeverInAProfile()
        {
            var recommended = SettingsPresets.Recommended.Values();
            var s = new LauncherSettings { TestBuilds = true, Profile = "Riley" };
            Assert.Contains("test_builds", SettingKeys.Machine);
            Assert.Empty(SettingKeys.Of(Setting.TestBuilds));
            Assert.Empty(ProfileEdits.Changed(s, recommended));
            Assert.DoesNotContain("test_builds", ProfileEdits.ProfileText(s));
            // Reset to Recommended and the defaults keep it, as they keep the runtime.
            Assert.True(ProfileEdits.ResetAll(s, recommended).TestBuilds);
            Assert.True(s.WithDefaults().TestBuilds);
            using (var dir = new TempDir())
            {
                var store = new ProfileStore(dir.Path);
                store.Save("Riley", s);
                Assert.DoesNotContain("test_builds", File.ReadAllText(dir.Combine("Riley.ini")));
                // Switching to a profile keeps this PC's choice, either way.
                Assert.True(store.Load("Riley", new LauncherSettings { TestBuilds = true }).TestBuilds);
                Assert.False(store.Load("Riley", new LauncherSettings()).TestBuilds);
                // launcher.ini's value is the one in use at start with a profile.
                var ini = new LauncherSettings { Profile = "Riley", TestBuilds = true };
                ProfileEdits.MarkUnsaved(ini, store.Load("Riley", ini), null);
                var read = LauncherSettings.Parse(ini.Serialize());
                Assert.True(ProfileEdits.ListHolds(read));
                Assert.True(ProfileEdits.InUse(read, store.Load("Riley", new LauncherSettings())).TestBuilds);
            }
        }

        [Fact]
        public void ItIsTheLastAdvancedRowAndSaysWhatATestBuildIs()
        {
            Assert.True(Setting.TestBuilds > Setting.VrMode);
            Assert.Equal(Setting.TestBuilds, Enum.GetValues(typeof(Setting)).Cast<Setting>().Max());
            Assert.Null(SettingRules.WhyNot(Setting.TestBuilds, new LauncherSettings { Mode = VrMode.Mono, Controllers = false }));
            var text = SettingTexts.For(Setting.TestBuilds);
            Assert.Equal("Get test builds", text.Label);
            foreach (var words in new[] { "early versions of the next release", "a folder of its own", "your EternalVR folder stays as it is",
                         "Discord thread", "Nothing is downloaded or installed for you", "Off by default" })
                Assert.Contains(words, text.Tooltip);
            UiText.AssertNoAdvice(text.Tooltip, "Get test builds");
        }
    }
}
