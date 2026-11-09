using System.Collections.Generic;
using System.IO;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>
    /// The one-time reset of 0.1.87's Index hand angles (<see cref="IndexAngleMigration"/>, DECISIONS T-225): exactly -20 / -40
    /// goes to 0 / 0 in launcher.ini and in each profile, once; the marker stops it running again; any other pair is kept.
    /// </summary>
    public class IndexAngleMigrationTests
    {
        private static LauncherSettings Ini(double weapon, double off)
        {
            var s = new LauncherSettings { WeaponHandAngle = weapon, OffHandAngle = off };
            // As read from the file at start: an older launcher's file has no marker line.
            var written = s.Serialize().Replace("index_angle_reset = 0\r\n", string.Empty).Replace("index_angle_reset = 0\n", string.Empty);
            Assert.DoesNotContain("index_angle_reset", written);
            return LauncherSettings.Parse(written);
        }

        [Fact]
        public void TheOldIndexPairIsResetOnceWithTheMarkerAndANotice()
        {
            var ini = Ini(-20, -40);
            Assert.False(ini.IndexAngleReset);
            var first = IndexAngleMigration.Run(ini, null, null, null);
            Assert.True(first.Ran);
            Assert.True(first.LauncherIni);
            Assert.Equal(0, ini.WeaponHandAngle, 6);
            Assert.Equal(0, ini.OffHandAngle, 6);
            Assert.True(ini.IndexAngleReset);
            var notice = IndexAngleMigration.Notice(first);
            Assert.Contains("fits Valve Index controllers by itself", notice);
            Assert.Contains("back at 0", notice);
            Assert.Contains("SteamVR, set those back to 0 too", notice);
            Assert.Contains("set the angles again", notice);
            Assert.DoesNotContain("—", notice);
            // Saved and read again (the next start): the marker is there and nothing runs, nor is anything shown.
            var next = LauncherSettings.Parse(ini.Serialize());
            Assert.Contains("index_angle_reset = 1", ini.Serialize());
            Assert.True(next.IndexAngleReset);
            next.WeaponHandAngle = -20;
            next.OffHandAngle = -40; // set again on purpose, on top of the fit
            var second = IndexAngleMigration.Run(next, null, null, null);
            Assert.False(second.Ran);
            Assert.Null(IndexAngleMigration.Notice(second));
            Assert.Equal(-20, next.WeaponHandAngle, 6);
            Assert.Equal(-40, next.OffHandAngle, 6);
        }

        [Fact]
        public void AnyOtherPairIsLeftAloneButMarked()
        {
            foreach (var (w, o) in new[] { (0.0, 0.0), (-20.0, 0.0), (0.0, -40.0), (-40.0, -20.0), (-25.0, -40.0), (-20.0, -45.0), (15.0, -10.0) })
            {
                var ini = Ini(w, o);
                var r = IndexAngleMigration.Run(ini, null, null, null);
                Assert.True(r.Ran);
                Assert.False(r.LauncherIni);
                Assert.Null(IndexAngleMigration.Notice(r));
                Assert.Equal(w, ini.WeaponHandAngle, 6);
                Assert.Equal(o, ini.OffHandAngle, 6);
                Assert.True(ini.IndexAngleReset);
            }
        }

        [Fact]
        public void ProfilesWithTheOldPairAreResetTooAndTheUnsavedListStillHolds()
        {
            using (var dir = new TempDir())
            {
                var store = new ProfileStore(dir.Path);
                store.Save("Index", new LauncherSettings { WeaponHandAngle = -20, OffHandAngle = -40, Turn = TurnMode.Snap });
                store.Save("Touch", new LauncherSettings { WeaponHandAngle = -10, OffHandAngle = 0 });
                // launcher.ini on the Index profile, as the launcher wrote it: the angles saved into the profile, the turn not.
                var ini = new LauncherSettings { Profile = "Index", WeaponHandAngle = -20, OffHandAngle = -40, Turn = TurnMode.Smooth };
                ProfileEdits.MarkUnsaved(ini, store.Load("Index", ini), null);
                ini = LauncherSettings.Parse(ini.Serialize());
                Assert.True(ProfileEdits.ListHolds(ini));
                Assert.Contains("turn", ini.ProfileUnsaved);

                var r = IndexAngleMigration.Run(ini, store, null, null);
                Assert.True(r.LauncherIni);
                Assert.Equal(new List<string> { "Index" }, r.Profiles);
                Assert.Contains("the profile Index", IndexAngleMigration.Notice(r));
                Assert.Equal(0, store.Load("Index", ini).WeaponHandAngle, 6);
                Assert.Equal(0, store.Load("Index", ini).OffHandAngle, 6);
                Assert.Equal(TurnMode.Snap, store.Load("Index", ini).Turn);
                Assert.Equal(-10, store.Load("Touch", ini).WeaponHandAngle, 6);
                Assert.DoesNotContain("index_angle_reset", File.ReadAllText(dir.Combine("Index.ini")));
                // Saved and read at the next start: the list of unsaved changes still holds (the turn), the angles are 0.
                var read = LauncherSettings.Parse(ini.Serialize());
                Assert.True(ProfileEdits.ListHolds(read));
                var inUse = ProfileEdits.InUse(read, store.Load("Index", read));
                Assert.Equal(TurnMode.Smooth, inUse.Turn);
                Assert.Equal(0, inUse.WeaponHandAngle, 6);
                Assert.True(inUse.IndexAngleReset);
                Assert.False(IndexAngleMigration.Run(read, store, null, null).Ran);
            }
        }

        [Fact]
        public void TheMarkerIsThisPcsAndKeptByResetsAndProfileSwitches()
        {
            var s = new LauncherSettings { IndexAngleReset = true, Profile = "Riley" };
            Assert.Contains("index_angle_reset", SettingKeys.Machine);
            Assert.Empty(ProfileEdits.Changed(s, SettingsPresets.Recommended.Values()));
            Assert.DoesNotContain("index_angle_reset", ProfileEdits.ProfileText(s));
            Assert.True(ProfileEdits.ResetAll(s, SettingsPresets.Recommended.Values()).IndexAngleReset);
            Assert.True(s.WithDefaults().IndexAngleReset);
            Assert.Empty(LauncherSettings.Parse("schema_version = 2\nindex_angle_reset = 1\n").UnknownKeys);
            using (var dir = new TempDir())
            {
                var store = new ProfileStore(dir.Path);
                store.Save("Riley", s);
                Assert.True(store.Load("Riley", new LauncherSettings { IndexAngleReset = true }).IndexAngleReset);
                Assert.False(store.Load("Riley", new LauncherSettings()).IndexAngleReset);
            }
        }
    }
}
