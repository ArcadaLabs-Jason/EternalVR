using System;
using System.IO;
using System.Linq;
using System.Text;
using EternalVR.Launcher.Core.Game;
using EternalVR.Launcher.Core.Safety;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>Crash, power-loss and edge cases of the snapshot, the marker and the save backups.</summary>
    public class SnapshotEdgeCaseTests
    {
        private static readonly string[] Forced = { "r_hdrDisplay", "r_dof" };

        [Fact]
        public void ConfigCreatedDuringTheSessionLosesTheForcedKeys()
        {
            using (var t = new TempDir())
            {
                // Before the session only the .cfg exists; the game creates the .local during the session.
                t.Write("saved/base/DOOMEternalConfig.cfg", "configVersion 9\n");
                var locs = GameLayout.FindSettingsLocations(t.Combine("saved"), null, null);
                var snap = SettingsSnapshot.Take(t.Combine("snaps"), "s1", locs);
                t.Write("saved/base/DOOMEternalConfig.local", "r_mode \"30\"\nr_hdrDisplay \"0\"\nr_dof \"0\"\n");

                var report = SettingsSnapshot.Restore(snap, Forced);

                Assert.Equal("r_mode \"30\"\n", t.Read("saved/base/DOOMEternalConfig.local"));
                Assert.Contains(report.Restored, r => r.Contains("DOOMEternalConfig.local"));
                Assert.True(File.Exists(Path.Combine(snap, "replaced", "saved-games", "base", "DOOMEternalConfig.local")));
                Assert.Empty(SettingsSnapshot.Restore(snap, Forced).Restored);
            }
        }

        [Fact]
        public void SavedGamesFolderMissingBeforeTheSessionIsStillCovered()
        {
            using (var t = new TempDir())
            {
                t.Write("steam/userdata/111/782330/remote/PROFILE/profile.bin", "p");
                var locs = GameLayout.FindSettingsLocations(t.Combine("saved"), t.Combine("steam"), "111");
                Assert.Equal(new[] { "saved-games", "steam-111" }, locs.Select(l => l.Name));

                var snap = SettingsSnapshot.Take(t.Combine("snaps"), "s1", locs);
                t.Write("saved/base/DOOMEternalConfig.cfg", "r_dof \"0\"\nr_swapInterval \"1\"\n");
                t.Write("saved/user/config.json", "{}");
                var report = SettingsSnapshot.Restore(snap, Forced);

                Assert.Equal("r_swapInterval \"1\"\n", t.Read("saved/base/DOOMEternalConfig.cfg"));
                Assert.Contains(report.ChangedNotRestored, c => c.Contains("config.json"));
            }
        }

        [Fact]
        public void NoSteamLocationAndNoSavedGamesFolderStillFindsNothing()
        {
            using (var t = new TempDir())
                Assert.Empty(GameLayout.FindSettingsLocations(t.Combine("saved"), t.Combine("steam"), "111"));
        }

        [Fact]
        public void RestoreKeepsEveryOtherByteOfTheFile()
        {
            using (var t = new TempDir())
            {
                // A UTF-8 byte order mark and a byte that is not valid UTF-8 (0xE9, Latin-1) must survive.
                var original = Concat(new byte[] { 0xEF, 0xBB, 0xBF }, Ascii("bind \"x\" \"say caf"), new byte[] { 0xE9 }, Ascii("\"\r\nr_hdrDisplay \"1\"\r\n"));
                var path = t.Combine("saved", "base", "DOOMEternalConfig.local");
                Directory.CreateDirectory(Path.GetDirectoryName(path));
                File.WriteAllBytes(path, original);
                var snap = SettingsSnapshot.Take(t.Combine("snaps"), "s1", GameLayout.FindSettingsLocations(t.Combine("saved"), null, null));

                var session = Concat(new byte[] { 0xEF, 0xBB, 0xBF }, Ascii("bind \"x\" \"say caf"), new byte[] { 0xE9 }, Ascii("\"\r\nr_hdrDisplay \"0\"\r\n"));
                File.WriteAllBytes(path, session);
                SettingsSnapshot.Restore(snap, Forced);

                Assert.Equal(original, File.ReadAllBytes(path));
            }
        }

        [Fact]
        public void ReadOnlyCopiesDoNotBlockPruning()
        {
            using (var t = new TempDir())
            {
                var cfg = t.Write("saved/base/DOOMEternalConfig.cfg", "r_dof \"1\"\n");
                File.SetAttributes(cfg, FileAttributes.ReadOnly);
                var locs = GameLayout.FindSettingsLocations(t.Combine("saved"), null, null);
                SettingsSnapshot.Take(t.Combine("snaps"), "20260101-000000", locs);
                SettingsSnapshot.Take(t.Combine("snaps"), "20260102-000000", locs);
                SettingsSnapshot.Prune(t.Combine("snaps"), 1, null);
                Assert.Equal(new[] { "20260102-000000" }, Directory.GetDirectories(t.Combine("snaps")).Select(Path.GetFileName));
                File.SetAttributes(cfg, FileAttributes.Normal);
            }
        }

        [Fact]
        public void AStaleReadOnlyTemporaryFileDoesNotBlockACopy()
        {
            using (var t = new TempDir())
            {
                var src = t.Write("a.txt", "new");
                var dst = t.Write("b.txt", "old");
                var tmp = t.Write("b.txt.evr-tmp", "left by a crash");
                File.SetAttributes(tmp, FileAttributes.ReadOnly);
                FileUtil.CopyVerified(src, dst);
                Assert.Equal("new", t.Read("b.txt"));
                Assert.False(File.Exists(tmp));
            }
        }

        private static byte[] Ascii(string s) => Encoding.ASCII.GetBytes(s);
        private static byte[] Concat(params byte[][] parts) => parts.SelectMany(p => p).ToArray();
    }

    public class MarkerEdgeCaseTests
    {
        [Fact]
        public void AFinishedRestoreNeverDeletesANewerSessionsMarker()
        {
            using (var t = new TempDir())
            {
                var path = t.Combine("SESSION_PENDING");
                new SessionMarker { State = SessionState.Running, SessionId = "newer" }.Write(path);
                Assert.False(SessionMarker.DeleteIfOwned(path, "older"));
                Assert.True(File.Exists(path));
                Assert.True(SessionMarker.DeleteIfOwned(path, "newer"));
                Assert.False(File.Exists(path));
                Assert.True(SessionMarker.DeleteIfOwned(path, "newer")); // already gone
            }
        }

        [Fact]
        public void ADamagedMarkerStillFindsItsSnapshot()
        {
            using (var t = new TempDir())
            {
                t.Write("saved/base/DOOMEternalConfig.cfg", "r_dof \"1\"\n");
                var locs = GameLayout.FindSettingsLocations(t.Combine("saved"), null, null);
                var snaps = t.Combine("snaps");
                var older = SettingsSnapshot.Take(snaps, "20260101-000000", locs);
                var own = SettingsSnapshot.Take(snaps, "20260102-000000", locs);
                var dry = SettingsSnapshot.Take(snaps, "20260103-000000", locs);
                File.WriteAllText(Path.Combine(dry, SettingsSnapshot.DryRunFile), "dry");

                Assert.Equal(own, new SessionMarker { SnapshotDir = own }.ResolveSnapshotDir(snaps));
                // The snapshot line was lost: the session's own folder.
                Assert.Equal(own, new SessionMarker { SessionId = "20260102-000000" }.ResolveSnapshotDir(snaps));
                // An empty marker (a power loss): the newest complete snapshot that is not a dry run's.
                File.WriteAllText(t.Combine("SESSION_PENDING"), string.Empty);
                var empty = SessionMarker.Read(t.Combine("SESSION_PENDING"));
                Assert.Equal(own, empty.ResolveSnapshotDir(snaps));
                // A marker that names a session whose snapshot never completed has nothing to restore.
                Assert.Null(new SessionMarker { SessionId = "20260104-000000" }.ResolveSnapshotDir(snaps));
                Assert.NotNull(older);
            }
        }
    }

    public class SaveBackupEdgeCaseTests
    {
        [Fact]
        public void NoSavesMakesNoBackupAndKeepsTheRealOnes()
        {
            using (var t = new TempDir())
            {
                t.Write("steam/userdata/111/782330/remote/GAME-AUTOSAVE0/game.details", "d1");
                t.Write("steam/userdata/222/782330/remote/PROFILE/profile.bin", "p");
                var withSaves = GameLayout.FindSettingsLocations(null, t.Combine("steam"), "111");
                var without = GameLayout.FindSettingsLocations(null, t.Combine("steam"), "222");
                for (int i = 1; i <= 5; i++) SaveBackups.Create(t.Combine("backups"), $"2026010{i}-000000", withSaves);

                // Five launches with an account that has no saves must not push the real backups out.
                for (int i = 1; i <= 5; i++)
                {
                    Assert.Null(SaveBackups.Create(t.Combine("backups"), $"2026020{i}-000000", without));
                    SaveBackups.Rotate(t.Combine("backups"));
                }
                Assert.Equal(5, SaveBackups.List(t.Combine("backups")).Count);
                Assert.All(SaveBackups.List(t.Combine("backups")), d => Assert.StartsWith("202601", Path.GetFileName(d)));
                Assert.False(Directory.Exists(t.Combine("backups", "20260201-000000")));
            }
        }

        [Fact]
        public void ReadOnlySavesRotate()
        {
            using (var t = new TempDir())
            {
                var save = t.Write("steam/userdata/111/782330/remote/GAME-AUTOSAVE0/game.details", "d1");
                File.SetAttributes(save, FileAttributes.ReadOnly);
                var locs = GameLayout.FindSettingsLocations(null, t.Combine("steam"), "111");
                for (int i = 1; i <= 7; i++) SaveBackups.Create(t.Combine("backups"), $"2026010{i}-000000", locs);
                SaveBackups.Rotate(t.Combine("backups"));
                Assert.Equal(SaveBackups.Keep, SaveBackups.List(t.Combine("backups")).Count);
                File.SetAttributes(save, FileAttributes.Normal);
            }
        }

        [Fact]
        public void ABackupWithABadEntryChangesNothing()
        {
            using (var t = new TempDir())
            {
                t.Write("steam/userdata/111/782330/remote/GAME-AUTOSAVE0/game.details", "d1");
                var locs = GameLayout.FindSettingsLocations(null, t.Combine("steam"), "111");
                var dir = SaveBackups.Create(t.Combine("backups"), "20260101-000000", locs);
                t.Write("steam/userdata/111/782330/remote/GAME-AUTOSAVE0/extra.sav", "current");

                // An entry naming a location the backup does not list: refused before any file is touched.
                var stray = t.Write("backups/20260101-000000/steam-999/GAME-AUTOSAVE0/game.details", "x");
                File.AppendAllText(Path.Combine(dir, "SHA256SUMS"), KnownBuilds.Sha256OfFile(stray) + "  steam-999/GAME-AUTOSAVE0/game.details\n");
                var e = Assert.Throws<IOException>(() => SaveBackups.Restore(t.Combine("backups"), dir, new DateTime(2026, 1, 2)));
                Assert.Contains("does not list", e.Message);
                Assert.Equal("current", t.Read("steam/userdata/111/782330/remote/GAME-AUTOSAVE0/extra.sav"));
                Assert.False(Directory.Exists(t.Combine("backups", "pre-restore-20260102-000000")));
            }
        }

        [Fact]
        public void ABackupEntryCannotPointOutsideItsLocation()
        {
            using (var t = new TempDir())
            {
                t.Write("steam/userdata/111/782330/remote/GAME-AUTOSAVE0/game.details", "d1");
                var locs = GameLayout.FindSettingsLocations(null, t.Combine("steam"), "111");
                var dir = SaveBackups.Create(t.Combine("backups"), "20260101-000000", locs);
                var stray = t.Write("backups/20260101-000000/outside.txt", "x");
                File.AppendAllText(Path.Combine(dir, "SHA256SUMS"), KnownBuilds.Sha256OfFile(stray) + "  steam-111/../outside.txt\n");
                Assert.Empty(SaveBackups.Verify(dir)); // the entry verifies; the path check must catch it
                var e = Assert.Throws<IOException>(() => SaveBackups.Restore(t.Combine("backups"), dir, new DateTime(2026, 1, 2)));
                Assert.Contains("outside its location", e.Message);
            }
        }
    }
}
