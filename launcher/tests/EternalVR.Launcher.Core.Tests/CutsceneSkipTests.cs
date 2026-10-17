using System.IO;
using System.Text.RegularExpressions;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>
    /// "Skip cutscenes" (<see cref="LauncherSettings.SkipCinematics"/>) is off by default under the key skip_cutscenes. Every
    /// earlier launcher wrote skip_cinematics = 1, its default, into launcher.ini, and that line is not read: these tests
    /// cover a player's files from such a launcher, with a named profile.
    /// </summary>
    public class CutsceneSkipTests
    {
        [Fact]
        public void SkipCutscenesIsOffByDefaultAndAnOldSkipLineIsNotRead()
        {
            // Every launcher before skip_cutscenes wrote skip_cinematics = 1 (its default) into the file: the cutscenes play
            // once for everyone, and the old line goes on the next save.
            var old = LauncherSettings.Parse("schema_version = 2\nskip_cinematics = 1\nworld_scale = 1.10\n");
            Assert.False(old.SkipCinematics);
            Assert.DoesNotContain("skip_cinematics", old.Serialize());
            Assert.Contains("skip_cutscenes = 0", old.Serialize());
            Assert.True(LauncherSettings.Parse("schema_version = 2\nskip_cutscenes = 1\n").SkipCinematics);
            Assert.True(LauncherSettings.Parse("schema_version = 2\nskip_cinematics = 0\nskip_cutscenes = 1\n").SkipCinematics);
        }

        [Fact]
        public void AnOldLauncherIniKeepsItsProfileListAndPlaysTheCutscenes()
        {
            using (var dir = new TempDir())
            {
                var store = new ProfileStore(dir.Path);
                store.Save("Riley", new LauncherSettings { Turn = TurnMode.Snap });
                // launcher.ini as the earlier launcher wrote it: its skip line, and the check value of its own lines.
                var ini = new LauncherSettings { Profile = "Riley", Turn = TurnMode.Snap, Vignette = VignetteMode.Strong };
                ProfileEdits.MarkUnsaved(ini, store.Load("Riley", ini), null);
                var text = ini.Serialize().Replace("skip_cutscenes = 0", "skip_cinematics = 1");
                var check = ProfileEdits.Fingerprint(LauncherSettings.ReadMap(text));
                text = Regex.Replace(text, "profile_check = [0-9a-f]+", "profile_check = " + check);
                Assert.Contains("skip_cinematics = 1", text);

                var read = LauncherSettings.Parse(text);
                Assert.True(ProfileEdits.ListHolds(read));
                var used = ProfileEdits.InUse(read, store.Load("Riley", new LauncherSettings()));
                Assert.False(used.SkipCinematics);
                Assert.Equal(TurnMode.Snap, used.Turn);
                Assert.Equal(VignetteMode.Strong, used.Vignette);
                Assert.DoesNotContain("skip_cinematics", used.Serialize());
            }
        }

        [Fact]
        public void AProfileFilesOldSkipLineIsNotReadAndATickedSkipIsKept()
        {
            using (var dir = new TempDir())
            {
                var store = new ProfileStore(dir.Path);
                store.Save("Riley", new LauncherSettings { Turn = TurnMode.Snap });
                File.AppendAllText(dir.Combine("Riley.ini"), "skip_cinematics = 1\n");
                Assert.False(store.Load("Riley", new LauncherSettings()).SkipCinematics);
                Assert.DoesNotContain("skip_cinematics", ProfileEdits.ProfileText(store.Load("Riley", new LauncherSettings())));

                store.Save("Sam", new LauncherSettings { SkipCinematics = true });
                Assert.Contains("skip_cutscenes = 1", File.ReadAllText(dir.Combine("Sam.ini")));
                Assert.True(store.Load("Sam", new LauncherSettings()).SkipCinematics);
            }
        }
    }
}
