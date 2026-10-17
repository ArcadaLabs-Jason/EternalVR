using System;
using System.IO;
using System.Linq;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>
    /// A profile rename left unfinished after its put-back failed (<see cref="ProfileRename"/>): what the window can still do
    /// must not hide the controls, and the next start finishes it.
    /// </summary>
    public class ProfileRenameUnfinishedTests
    {
        private sealed class Left
        {
            public ProfileStore Store;
            public ControlSets Controls;
            public ProfileRename Rename;
        }

        /// <summary>
        /// The state a rename riley to Evening leaves when the controls folder's second step failed and so did its put-back:
        /// the file at Evening.ini, the maps at <c>controls\profiles\.evr-rename-Evening</c>, the journal kept.
        /// </summary>
        private static Left AfterFailedPutBack(TempDir t)
        {
            var left = new Left { Store = new ProfileStore(t.Combine("profiles")), Controls = new ControlSets(t.Combine("controls")) };
            left.Rename = new ProfileRename(left.Store, left.Controls);
            left.Store.Save("Evening", new LauncherSettings { Turn = TurnMode.Snap });
            t.Write("controls/profiles/.evr-rename-Evening/oculus_touch.toml", "riley's touch");
            t.Write("controls/profiles/.evr-rename-Evening/" + ControlsFolder.ReadmeName, ControlsFolder.ReadmeFor("riley"));
            t.Write("controls/oculus_touch.toml", "the shared touch");
            t.Write("profiles/.rename-pending", "riley\nEvening\n");
            return left;
        }

        private static ProfileRename NextStart(TempDir t) =>
            new ProfileRename(new ProfileStore(t.Combine("profiles")), new ControlSets(t.Combine("controls")));

        [Fact]
        public void AnotherRenameIsRefusedUntilTheNextStartFinishesThisOne()
        {
            using (var t = new TempDir())
            {
                var left = AfterFailedPutBack(t);
                // The window still names riley; a third name would overwrite the journal that names the maps' way home.
                Assert.Contains("not finished", left.Rename.WhyNot("riley", "Morgan"));
                Assert.Throws<ArgumentException>(() => left.Rename.Start("riley", "Morgan"));
                Assert.Equal("riley\nEvening\n", t.Read("profiles/.rename-pending"));
                var ini = new LauncherSettings { Profile = "riley" };
                var next = NextStart(t);
                Assert.Equal("Evening", next.Recover(ini).To);
                Assert.False(next.HalfMoved(new RenamePending("riley", "Evening")));
                next.Done();
                Assert.Equal("Evening", ini.Profile);
                Assert.Equal("riley's touch", t.Read("controls/profiles/Evening/oculus_touch.toml"));
                Assert.Null(next.WhyNot("Evening", "Morgan"));
            }
        }

        [Fact]
        public void NoControlsAreMadeAtANameAFolderIsStillMovingTo()
        {
            using (var t = new TempDir())
            {
                var left = AfterFailedPutBack(t);
                Assert.True(left.Controls.RenameUnderWay("Evening"));
                // Picking Evening, or the start's copy for profiles without controls, would make this folder.
                Assert.False(left.Controls.Adopt("Evening"));
                Assert.False(Directory.Exists(t.Combine("controls", "profiles", "Evening")));
                Assert.Throws<IOException>(() => left.Controls.StartFrom("Evening", string.Empty));
                Assert.False(Directory.Exists(t.Combine("controls", "profiles", "Evening")));
                NextStart(t).Recover(new LauncherSettings());
                Assert.Equal("riley's touch", t.Read("controls/profiles/Evening/oculus_touch.toml"));
                Assert.False(left.Controls.RenameUnderWay("Evening"));
            }
        }

        [Fact]
        public void WhatWasMadeAtTheNewNameMeanwhileIsSetAsideNotRemoved()
        {
            using (var t = new TempDir())
            {
                AfterFailedPutBack(t);
                // An older launcher's copy of the shared controls, made at the new name meanwhile.
                t.Write("controls/profiles/Evening/oculus_touch.toml", "the shared touch");
                var next = NextStart(t);
                next.Recover(new LauncherSettings());
                Assert.Equal("riley's touch", t.Read("controls/profiles/Evening/oculus_touch.toml"));
                Assert.Equal("the shared touch", t.Read("controls/profiles/.evr-kept-Evening-1/oculus_touch.toml"));
                Assert.Equal(new[] { t.Combine("controls", "profiles", ".evr-kept-Evening-1") }, next.KeptAside);
                Assert.False(next.HalfMoved(new RenamePending("riley", "Evening")));
            }
        }

        [Fact]
        public void ASaveOnTheOldNameAfterAFailedPutBackIsWhatTheProfileKeeps()
        {
            using (var t = new TempDir())
            {
                var left = AfterFailedPutBack(t);
                // The window, still on riley, saved: the settings saved last are riley's file now, newer than Evening.ini.
                left.Store.Save("riley", new LauncherSettings { Turn = TurnMode.Snap, TurnRate = 300 });
                var ini = new LauncherSettings { Profile = "riley" };
                NextStart(t).Recover(ini);
                Assert.Equal("Evening", ini.Profile);
                Assert.Equal(new[] { "Evening" }, left.Store.Names());
                Assert.Equal(300, left.Store.Load("Evening", ini).TurnRate);
                // The file that was at the new name is kept aside, not removed.
                Assert.True(File.Exists(t.Combine("profiles", ".evr-kept-Evening-1.ini")));
            }
        }

        [Fact]
        public void AFileAPutBackLeftAtTheOldNamesTemporaryNameIsFinishedForward()
        {
            using (var t = new TempDir())
            {
                var store = new ProfileStore(t.Combine("profiles"));
                store.Save("riley", new LauncherSettings { Turn = TurnMode.Snap });
                // The put-back's move of Evening.ini to riley.ini stopped at its temporary name; the controls reached Evening.
                File.Move(t.Combine("profiles", "riley.ini"), t.Combine("profiles", ".evr-rename-riley.ini"));
                t.Write("controls/profiles/Evening/oculus_touch.toml", "riley's touch");
                t.Write("profiles/.rename-pending", "riley\nEvening\n");
                var next = NextStart(t);
                var ini = new LauncherSettings { Profile = "riley" };
                next.Recover(ini);
                Assert.False(next.HalfMoved(new RenamePending("riley", "Evening")));
                Assert.Equal(new[] { "Evening" }, store.Names());
                Assert.Equal(TurnMode.Snap, store.Load("Evening", ini).Turn);
                Assert.DoesNotContain(Directory.GetFiles(t.Combine("profiles")).Select(Path.GetFileName), n => n.StartsWith(".evr-rename-", StringComparison.Ordinal));
            }
        }
    }
}
