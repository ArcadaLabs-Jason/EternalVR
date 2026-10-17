using System;
using System.IO;
using System.Linq;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>Renaming a VR settings profile with its controls (<see cref="ProfileRename"/>, ROADMAP M8).</summary>
    public class ProfileRenameTests
    {
        /// <summary>A launcher that stops between two steps (the tests stop it with this).</summary>
        private sealed class Stopped : Exception { }

        private sealed class Setup
        {
            public ProfileStore Store;
            public ControlSets Controls;
            public ProfileRename Rename;
        }

        /// <summary>Profile <c>riley</c> with its own controls (a map of the player's and the README) and launcher.ini naming it.</summary>
        private static Setup Make(TempDir t)
        {
            var s = new Setup
            {
                Store = new ProfileStore(t.Combine("profiles")),
                Controls = new ControlSets(t.Combine("controls")),
            };
            s.Rename = new ProfileRename(s.Store, s.Controls);
            var settings = new LauncherSettings { Turn = TurnMode.Snap };
            s.Store.Save("riley", settings);
            t.Write("controls/profiles/riley/oculus_touch.toml", "riley's touch");
            t.Write("controls/profiles/riley/" + ControlsFolder.ReadmeName, ControlsFolder.ReadmeFor("riley"));
            t.Write("controls/oculus_touch.toml", "the shared touch");
            return s;
        }

        private static void AssertRenamed(TempDir t, Setup s, string to)
        {
            Assert.Equal(new[] { to }, s.Store.Names());
            Assert.Equal(TurnMode.Snap, s.Store.Load(to, new LauncherSettings()).Turn);
            Assert.True(s.Controls.HasOwn(to));
            Assert.Equal("riley's touch", File.ReadAllText(Path.Combine(s.Controls.Of(to).Dir, "oculus_touch.toml")));
            Assert.Equal(ControlsFolder.ReadmeFor(to), File.ReadAllText(s.Controls.Of(to).ReadmeFile));
            Assert.Equal(new[] { to }, Directory.GetDirectories(s.Controls.ProfilesDir).Select(Path.GetFileName));
            Assert.Equal("the shared touch", t.Read("controls/oculus_touch.toml"));
        }

        [Fact]
        public void TheFileAndTheControlsFolderTakeTheNewName()
        {
            using (var t = new TempDir())
            {
                var s = Make(t);
                var p = s.Rename.Start("riley", " Evening ");
                Assert.Equal("riley", p.From);
                Assert.Equal("Evening", p.To);
                // Until launcher.ini names the new profile, the journal says what is under way.
                Assert.True(File.Exists(s.Rename.JournalFile));
                s.Rename.Done();
                Assert.False(File.Exists(s.Rename.JournalFile));
                AssertRenamed(t, s, "Evening");
                // The journal is not a profile, and nothing is left at the old name.
                Assert.False(s.Store.Exists("riley"));
                Assert.Null(s.Rename.ReadPending());
            }
        }

        [Fact]
        public void AChangeOfCaseOnlyIsARename()
        {
            using (var t = new TempDir())
            {
                var s = Make(t);
                Assert.Null(s.Rename.WhyNot("riley", "Riley"));
                s.Rename.Start("riley", "Riley");
                s.Rename.Done();
                AssertRenamed(t, s, "Riley");
                Assert.Equal(new[] { "Riley.ini" }, Directory.GetFiles(s.Store.Dir).Select(Path.GetFileName));
            }
        }

        [Fact]
        public void ANameInUseOrNotAllowedIsRefusedAndNothingMoves()
        {
            using (var t = new TempDir())
            {
                var s = Make(t);
                s.Store.Save("Evening", new LauncherSettings());
                Assert.Contains("exists already", s.Rename.WhyNot("riley", "evening"));
                Assert.Contains("cannot be used", s.Rename.WhyNot("riley", "a/b"));
                Assert.NotNull(s.Rename.WhyNot("", "Evening"));
                Assert.Throws<ArgumentException>(() => s.Rename.Start("riley", "EVENING"));
                Assert.False(File.Exists(s.Rename.JournalFile));
                Assert.Equal(new[] { "Evening", "riley" }, s.Store.Names());
                Assert.True(s.Controls.HasOwn("riley"));
            }
        }

        [Theory]
        [InlineData("file")]
        [InlineData("controls")]
        public void ALauncherStoppedHalfWayFinishesTheRenameAtItsNextStart(string step)
        {
            using (var t = new TempDir())
            {
                var s = Make(t);
                var ini = new LauncherSettings { Profile = "riley" };
                Assert.Throws<Stopped>(() => s.Rename.Start("riley", "Evening", done => { if (done == step) throw new Stopped(); }));
                if (step == "file")
                {
                    // Without the journal the profile would now be Evening with no controls of its own: the next start
                    // would give it a copy of the shared ones and riley's would be lost in a folder no profile names.
                    Assert.Equal(new[] { "Evening" }, s.Store.Names());
                    Assert.False(s.Controls.HasOwn("Evening"));
                }
                // The next start, with launcher.ini as it was.
                var next = new ProfileRename(new ProfileStore(t.Combine("profiles")), new ControlSets(t.Combine("controls")));
                var p = next.Recover(ini);
                Assert.Equal("Evening", p.To);
                Assert.Equal("Evening", ini.Profile);
                next.Done();
                AssertRenamed(t, s, "Evening");
                Assert.Null(next.Recover(ini));
            }
        }

        [Fact]
        public void AFailedMovePutsEverythingBackAndKeepsAFolderLeftAtTheNewName()
        {
            using (var t = new TempDir())
            {
                var s = Make(t);
                // A deleted profile's controls folder left behind at the new name.
                t.Write("controls/profiles/Evening/old.toml", "left behind");
                var e = Assert.Throws<IOException>(() => s.Rename.Start("riley", "Evening", done => { if (done == "file") throw new IOException("in use"); }));
                Assert.Equal("in use", e.Message);
                Assert.False(File.Exists(s.Rename.JournalFile));
                Assert.Equal(new[] { "riley" }, s.Store.Names());
                Assert.Equal("riley's touch", t.Read("controls/profiles/riley/oculus_touch.toml"));
                Assert.Equal("left behind", t.Read("controls/profiles/Evening/old.toml"));
                // A rename that goes through replaces that leftover folder, as a new profile of that name does.
                s.Rename.Start("riley", "Evening");
                s.Rename.Done();
                AssertRenamed(t, s, "Evening");
                Assert.False(File.Exists(t.Combine("controls", "profiles", "Evening", "old.toml")));
            }
        }

        /// <summary>No entry of the rename is left at a temporary name in the profiles or the controls folder.</summary>
        private static void AssertNothingHalfMoved(TempDir t)
        {
            foreach (var dir in new[] { t.Combine("profiles"), t.Combine("controls", "profiles") })
                Assert.DoesNotContain(Directory.GetFileSystemEntries(dir).Select(Path.GetFileName), n => n.StartsWith(".evr-rename-", StringComparison.Ordinal));
        }

        [Theory]
        [InlineData("file half")]
        [InlineData("controls half")]
        public void AMoveThatFailsBetweenItsTwoStepsIsPutBack(string step)
        {
            using (var t = new TempDir())
            {
                var s = Make(t);
                // The second step of the move (from the temporary name to the new one) is the one that fails.
                Assert.Throws<IOException>(() => s.Rename.Start("riley", "Evening", done => { if (done == step) throw new IOException("in use"); }));
                AssertNothingHalfMoved(t);
                Assert.Equal(new[] { "riley" }, s.Store.Names());
                Assert.Equal(TurnMode.Snap, s.Store.Load("riley", new LauncherSettings()).Turn);
                Assert.Equal("riley's touch", t.Read("controls/profiles/riley/oculus_touch.toml"));
                Assert.True(s.Controls.HasOwn("riley"));
                Assert.False(File.Exists(s.Rename.JournalFile));
            }
        }

        [Fact]
        public void APutBackThatCannotFinishKeepsTheJournalAndTheNextStartFinishesTheRename()
        {
            using (var t = new TempDir())
            {
                var s = Make(t);
                var ini = new LauncherSettings { Profile = "riley" };
                // The move fails between its steps, and something now sits at the old name, so the controls cannot go back.
                Assert.Throws<IOException>(() => s.Rename.Start("riley", "Evening", done =>
                {
                    if (done != "controls half") return;
                    Directory.CreateDirectory(t.Combine("controls", "profiles", "riley"));
                    throw new IOException("in use");
                }));
                Assert.True(File.Exists(s.Rename.JournalFile));
                Assert.Equal("riley's touch", t.Read("controls/profiles/.evr-rename-Evening/oculus_touch.toml"));
                var next = new ProfileRename(new ProfileStore(t.Combine("profiles")), new ControlSets(t.Combine("controls")));
                Assert.Equal("Evening", next.Recover(ini).To);
                next.Done();
                Assert.Equal("Evening", ini.Profile);
                Assert.Equal(new[] { "Evening" }, s.Store.Names());
                Assert.Equal("riley's touch", t.Read("controls/profiles/Evening/oculus_touch.toml"));
                AssertNothingHalfMoved(t);
            }
        }

        [Fact]
        public void AFolderLeftAtTheNewNameIsKeptWhenTheMoveThatWouldReplaceItFails()
        {
            using (var t = new TempDir())
            {
                var s = Make(t);
                t.Write("controls/profiles/Evening/old.toml", "left behind");
                Assert.Throws<IOException>(() => s.Rename.Start("riley", "Evening", done => { if (done == "controls half") throw new IOException("in use"); }));
                AssertNothingHalfMoved(t);
                Assert.Equal("left behind", t.Read("controls/profiles/Evening/old.toml"));
                Assert.Equal("riley's touch", t.Read("controls/profiles/riley/oculus_touch.toml"));
            }
        }

        [Fact]
        public void AReadmeThatCannotBeWrittenDoesNotStopTheRename()
        {
            using (var t = new TempDir())
            {
                var s = Make(t);
                var readme = t.Combine("controls", "profiles", "riley", ControlsFolder.ReadmeName);
                File.SetAttributes(readme, FileAttributes.ReadOnly);
                try
                {
                    s.Rename.Start("riley", "Evening");
                    s.Rename.Done();
                    Assert.Equal(new[] { "Evening" }, s.Store.Names());
                    Assert.Equal("riley's touch", t.Read("controls/profiles/Evening/oculus_touch.toml"));
                    AssertNothingHalfMoved(t);
                }
                finally
                {
                    foreach (var f in Directory.GetFiles(t.Combine("controls"), ControlsFolder.ReadmeName, SearchOption.AllDirectories))
                        File.SetAttributes(f, FileAttributes.Normal);
                }
            }
        }

        [Fact]
        public void AProfileWithoutControlsOfItsOwnIsRenamedToo()
        {
            using (var t = new TempDir())
            {
                var store = new ProfileStore(t.Combine("profiles"));
                var controls = new ControlSets(t.Combine("controls"));
                store.Save("Old one", new LauncherSettings { Vignette = VignetteMode.Light });
                var rename = new ProfileRename(store, controls);
                rename.Start("Old one", "New one");
                rename.Done();
                Assert.Equal(new[] { "New one" }, store.Names());
                Assert.Equal(VignetteMode.Light, store.Load("New one", new LauncherSettings()).Vignette);
                Assert.False(controls.HasOwn("New one"));
            }
        }
    }
}
