using System;
using System.Collections.Generic;
using System.IO;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>
    /// The rename journal at start (<see cref="ProfileRename.FinishAtStart"/>): a rename it names is finished, one it cannot
    /// finish keeps the profiles locked until a start can, and a journal that names no rename locks nothing.
    /// </summary>
    public class ProfileRenameJournalTests
    {
        private static ProfileRename At(TempDir t) =>
            new ProfileRename(new ProfileStore(t.Combine("profiles")), new ControlSets(t.Combine("controls")));

        private static bool Finish(ProfileRename r, LauncherSettings ini, List<string> log, Action save = null) =>
            r.FinishAtStart(ini, save ?? (() => { }), m => log.Add("info: " + m), m => log.Add("warn: " + m));

        /// <summary>Rename riley to Evening stopped after the file moved, with the maps between the two steps of their move.</summary>
        private static void Stopped(TempDir t)
        {
            new ProfileStore(t.Combine("profiles")).Save("Evening", new LauncherSettings { Turn = TurnMode.Snap });
            t.Write("controls/profiles/.evr-rename-Evening/oculus_touch.toml", "riley's touch");
            t.Write("profiles/.rename-pending", "riley\nEvening\n");
        }

        [Theory]
        [InlineData("riley\n")]
        [InlineData("")]
        [InlineData("riley\nEve|ning\n")]
        public void AJournalThatNamesNoRenameIsSetAsideAndLocksNothing(string text)
        {
            using (var t = new TempDir())
            {
                new ProfileStore(t.Combine("profiles")).Save("riley", new LauncherSettings());
                t.Write("profiles/.rename-pending.bad", "an older one\n");
                t.Write("profiles/.rename-pending", text);
                var r = At(t);
                Assert.False(r.Unfinished);
                Assert.Null(r.WhyNot("riley", "Morgan"));
                var log = new List<string>();
                var ini = new LauncherSettings { Profile = "riley" };
                Assert.False(Finish(r, ini, log));
                Assert.False(File.Exists(r.JournalFile));
                Assert.Equal(text, File.ReadAllText(r.BadJournalFile));
                Assert.Contains(log, l => l.StartsWith("warn: ", StringComparison.Ordinal) && l.Contains(".rename-pending.bad"));
                Assert.Equal("riley", ini.Profile);
                // The next start has nothing to do.
                var next = At(t);
                Assert.False(Finish(next, ini, new List<string>()));
                Assert.Null(next.WhyNot("riley", "Morgan"));
            }
        }

        [Fact]
        public void AJournalThatCannotBeReadKeepsTheLockAndIsLeftAsItIs()
        {
            using (var t = new TempDir())
            {
                t.Write("profiles/.rename-pending", "riley\nEvening\n");
                var r = At(t);
                using (new FileStream(r.JournalFile, FileMode.Open, FileAccess.Read, FileShare.None))
                {
                    Assert.True(r.Unfinished);
                    Assert.NotNull(r.WhyNot("riley", "Morgan"));
                    Assert.True(Finish(r, new LauncherSettings { Profile = "riley" }, new List<string>()));
                }
                Assert.False(File.Exists(r.BadJournalFile));
                Assert.Equal("riley\nEvening\n", t.Read("profiles/.rename-pending"));
            }
        }

        [Fact]
        public void TheStartFinishesARenameSavesLauncherIniAndUnlocks()
        {
            using (var t = new TempDir())
            {
                Stopped(t);
                var r = At(t);
                var log = new List<string>();
                var ini = new LauncherSettings { Profile = "riley" };
                int saves = 0;
                Assert.False(Finish(r, ini, log, () => saves++));
                Assert.Equal(1, saves);
                Assert.Equal("Evening", ini.Profile);
                Assert.False(File.Exists(r.JournalFile));
                Assert.Equal("riley's touch", t.Read("controls/profiles/Evening/oculus_touch.toml"));
                Assert.Contains(log, l => l.StartsWith("info: VR settings profile rename finished", StringComparison.Ordinal));
                Assert.Null(r.WhyNot("Evening", "Morgan"));
            }
        }

        [Fact]
        public void AStartThatCannotFinishTheRenameKeepsTheLockAndTheNextFinishesIt()
        {
            using (var t = new TempDir())
            {
                Stopped(t);
                var r = At(t);
                var log = new List<string>();
                var ini = new LauncherSettings { Profile = "riley" };
                // A file held open in the maps' folder: Windows does not move the folder.
                using (new FileStream(t.Combine("controls", "profiles", ".evr-rename-Evening", "oculus_touch.toml"), FileMode.Open, FileAccess.Read, FileShare.None))
                {
                    Assert.True(Finish(r, ini, log));
                    Assert.True(File.Exists(r.JournalFile));
                    Assert.Contains(log, l => l.StartsWith("warn: ", StringComparison.Ordinal) && l.Contains("tried again at the next start"));
                    Assert.NotNull(r.WhyNot("riley", "Morgan"));
                }
                var next = At(t);
                Assert.False(Finish(next, ini, new List<string>()));
                Assert.Equal("Evening", ini.Profile);
                Assert.False(File.Exists(next.JournalFile));
                Assert.Equal("riley's touch", t.Read("controls/profiles/Evening/oculus_touch.toml"));
            }
        }

        [Fact]
        public void ARenameFinishedAtStartKeepsLauncherInisUnsavedList()
        {
            using (var t = new TempDir())
            {
                var saver = CpuSaver.Parse(TestData.Read("cpu-saver.txt"));
                new ProfileStore(t.Combine("profiles")).Save("riley", new LauncherSettings());
                t.Write("profiles/.rename-pending", "riley\nEvening\n");
                var shown = new LauncherSettings { Profile = "riley", Turn = TurnMode.Snap };
                ProfileEdits.MarkUnsaved(shown, new LauncherSettings { Profile = "riley" }, saver);
                var ini = LauncherSettings.Parse(shown.Serialize());
                Assert.True(ProfileEdits.ListHolds(ini));
                Assert.False(Finish(At(t), ini, new List<string>()));
                // The same start reads the profile next (LauncherContext.TakeActiveProfile), from these very settings.
                Assert.True(ProfileEdits.ListHolds(ini));
                Assert.NotSame(ini, ProfileEdits.InUse(ini, new LauncherSettings { Profile = "Evening" }, saver));
                var back = LauncherSettings.Parse(ini.Serialize());
                Assert.Equal("Evening", back.Profile);
                Assert.True(ProfileEdits.ListHolds(back));
                Assert.Equal(new[] { "turn" }, back.ProfileUnsaved);
            }
        }

        [Fact]
        public void TheCheckValueCoversTheProfileAndKeepsItsFormat()
        {
            var map = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
            {
                ["schema_version"] = "2", ["game_dir"] = @"E:\Games\DOOMEternal", ["layer_dir"] = "", ["runtime"] = "system",
                ["dlss_dll_path"] = "", ["profile_unsaved"] = "turn", ["profile_check"] = "0123456789abcdef",
                ["Turn"] = " snap ", ["profile"] = "Riley",
            };
            // 16 hex digits of the SHA-256 of "profile=Riley\nturn=snap\n": a launcher.ini written by any version keeps its check.
            Assert.Equal("dfb6ab84d2ed100d", ProfileEdits.Fingerprint(map));
            map["profile"] = "Morgan";
            Assert.NotEqual("dfb6ab84d2ed100d", ProfileEdits.Fingerprint(map));
        }
    }
}
