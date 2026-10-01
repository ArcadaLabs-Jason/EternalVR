using System;
using System.IO;
using System.Linq;
using EternalVR.Launcher.Core.Report;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>The player's own controller maps in a report (the controls folder in the data folder).</summary>
    public class ControlsReportTests
    {
        private const string Map = "[controller]\nprofile = \"/interaction_profiles/valve/index_controller\"\n\n[map.right]\n\"right.primary.press\" = \"jump\"\n";

        /// <summary>
        /// <see cref="ReportTests.Setup"/> plus a controls folder: a README and the built-in copies in each set, a map of the
        /// player's own with no profile and in the profile "Night Shift", and files the launcher and the layer do not read.
        /// </summary>
        private static ReportInputs SetupWithControls(TempDir t)
        {
            var inputs = ReportTests.Setup(t);
            t.Write(@"data\controls\README.txt", "Your EternalVR controls\n");
            t.Write(@"data\controls\defaults\valve_index.toml", "built-in");
            t.Write(@"data\controls\defaults\oculus_touch.toml", "built-in");
            t.Write(@"data\controls\oculus_touch.toml", Map + "# copied from C:\\Users\\Tester\\Desktop\\mine.toml\n");
            t.Write(@"data\controls\profiles\Night Shift\README.txt", "Your EternalVR controls for the VR settings profile Night Shift\n");
            t.Write(@"data\controls\profiles\Night Shift\defaults\valve_index.toml", "built-in");
            t.Write(@"data\controls\profiles\Night Shift\valve_index.toml", Map);
            t.Write(@"data\controls\profiles\Night Shift\notes.txt", "not a map");
            t.Write(@"data\controls\profiles\Night Shift\.toml", "not a map name");
            t.Write(@"data\controls\profiles\Night Shift\old\pico4.toml", "below the set");
            t.Write(@"data\controls\profiles\.evr-tmp-Night Shift\valve_index.toml", "a copy in progress");
            return inputs;
        }

        [Fact]
        public void ThePlayersOwnMapsAreInTheZipWithTheirPaths()
        {
            using (var t = new TempDir())
            {
                var report = ReportBuilder.Build(SetupWithControls(t));
                var entries = ReportTests.Unzip(report.Zip);
                var names = entries.Keys.ToList();
                int layer = names.IndexOf("layer/VK_LAYER_ETERNALVR.json");
                Assert.Equal(new[] { "controls/oculus_touch.toml", "controls/profiles/Night Shift/valve_index.toml", "windows-events.txt" },
                    names.Skip(layer + 1).Take(3));
                Assert.Equal(2, names.Count(n => n.StartsWith("controls/", StringComparison.Ordinal)));
                Assert.Equal(Map, entries["controls/profiles/Night Shift/valve_index.toml"]);
                Assert.Contains(@"# copied from %USERPROFILE%\Desktop\mine.toml", entries["controls/oculus_touch.toml"]);
                Assert.DoesNotContain(entries.Values, v => v.Contains("built-in") || v.Contains("not a map") || v.Contains("below the set") || v.Contains("in progress"));

                var contents = entries["report-contents.txt"];
                Assert.Contains("  controls/profiles/Night Shift/valve_index.toml  ", contents);
                Assert.DoesNotContain("Controls:", contents);
                Assert.Contains("controls/oculus_touch.toml  (", report.Describe());

                var maps = ReportBuilder.ControlsPlayerMaps(t.Combine("data", "controls"));
                Assert.Equal(new[] { t.Combine("data", "controls", "oculus_touch.toml"), t.Combine("data", "controls", "profiles", "Night Shift", "valve_index.toml") }, maps);
            }
        }

        [Fact]
        public void AProfileNamedAfterThePlayerIsRedactedInTheZipPath()
        {
            using (var t = new TempDir())
            {
                var inputs = SetupWithControls(t);
                inputs.GameSavedGamesDirs = new[] { t.Combine("saved") };
                t.Write(@"saved\base\qconsole.log", "idSignInManager::TriggerLocalUserSignInEvent - User 'TestPilot' signed in - 1234567890\n");
                t.Write(@"data\controls\profiles\TestPilot\valve_index.toml", Map);
                t.Write(@"data\controls\profiles\tester\pico4.toml", Map); // the Windows user name, in another case
                t.Write(@"data\launcher.ini", "schema_version = 2\nprofile = TestPilot\n");
                t.Write(@"data\logs\20260926-100000-2\eternalvr-20260926-100000-2-100.log",
                    "controllers: controller data 'C:\\Users\\Tester\\AppData\\Local\\EternalVR\\controls\\profiles\\TestPilot\\valve_index.toml' replaces the built-in data of its profile\n");

                var report = ReportBuilder.Build(inputs);
                var entries = ReportTests.Unzip(report.Zip);
                var controls = entries.Keys.Where(n => n.StartsWith("controls/", StringComparison.Ordinal)).ToList();
                Assert.Equal(new[]
                {
                    "controls/oculus_touch.toml", "controls/profiles/Night Shift/valve_index.toml",
                    "controls/profiles/[user]/pico4.toml", "controls/profiles/[player]/valve_index.toml",
                }, controls);
                Assert.Equal(controls, report.Files.Select(f => f.ZipPath).Where(n => n.StartsWith("controls/", StringComparison.Ordinal)));
                foreach (var kv in entries)
                {
                    Assert.DoesNotContain("TestPilot", kv.Key, StringComparison.OrdinalIgnoreCase);
                    Assert.DoesNotContain("TestPilot", kv.Value, StringComparison.OrdinalIgnoreCase);
                    Assert.DoesNotContain("Tester", kv.Key, StringComparison.OrdinalIgnoreCase);
                }
                Assert.Contains("profile = <player>", entries["launcher.ini"]);
                Assert.Contains(@"'%USERPROFILE%\AppData\Local\EternalVR\controls\profiles\<player>\valve_index.toml'",
                    entries["sessions/20260926-100000-2/eternalvr-20260926-100000-2-100.log"]);
                Assert.Contains("  controls/profiles/[player]/valve_index.toml  ", entries["report-contents.txt"]);
            }
        }

        [Fact]
        public void NoMapsOfYourOwnIsSaid()
        {
            using (var t = new TempDir())
            {
                var inputs = ReportTests.Setup(t);
                var report = ReportBuilder.Build(inputs); // no controls folder at all
                Assert.Contains("\nControls: no maps of your own (the built-in controls are used)\n", report.Files[0].Text);
                Assert.DoesNotContain(report.Files, f => f.ZipPath.StartsWith("controls/", StringComparison.Ordinal));

                t.Write(@"data\controls\README.txt", "Your EternalVR controls\n");
                t.Write(@"data\controls\defaults\valve_index.toml", "built-in");
                t.Write(@"data\controls\profiles\Night Shift\README.txt", "Your EternalVR controls for the VR settings profile Night Shift\n");
                t.Write(@"data\controls\profiles\Night Shift\defaults\valve_index.toml", "built-in");
                report = ReportBuilder.Build(inputs); // the launcher's own files only
                Assert.Contains("\nControls: no maps of your own (the built-in controls are used)\n", report.Files[0].Text);
                Assert.DoesNotContain(report.Files, f => f.ZipPath.StartsWith("controls/", StringComparison.Ordinal));
                Assert.Empty(ReportBuilder.ControlsPlayerMaps(t.Combine("data", "controls")));
                Assert.Empty(ReportBuilder.ControlsPlayerMaps(t.Combine("missing")));
                Assert.Empty(ReportBuilder.ControlsPlayerMaps(null));
            }
        }

        [Fact]
        public void ALongMapKeepsItsHeadAndTail()
        {
            using (var t = new TempDir())
            {
                var inputs = ReportTests.Setup(t);
                t.Write(@"data\controls\valve_index.toml", "first line\n" + string.Concat(Enumerable.Repeat(new string('#', 99) + "\n", 2000)) + "last line\n");
                var map = ReportBuilder.Build(inputs).Files.Single(f => f.ZipPath == "controls/valve_index.toml");
                Assert.True(map.Truncated);
                Assert.True(map.Bytes <= 128 * 1024 + 200);
                Assert.StartsWith("first line\n", map.Text);
                Assert.EndsWith("last line\n", map.Text);
            }
        }

        [Fact]
        public void PlaceholdersInAZipPathAreSafeFileNames()
        {
            Assert.Equal("controls/profiles/[player]/valve_index.toml", ReportBuilder.ZipSafe("controls/profiles/<player>/valve_index.toml"));
            Assert.Equal("controls/profiles/%USERPROFILE%/x.toml", ReportBuilder.ZipSafe("controls/profiles/%USERPROFILE%/x.toml"));
            Assert.DoesNotContain(ReportBuilder.ZipSafe("<user> <computer> <steamid>"), c => Array.IndexOf(Path.GetInvalidFileNameChars(), c) >= 0);
        }
    }
}
