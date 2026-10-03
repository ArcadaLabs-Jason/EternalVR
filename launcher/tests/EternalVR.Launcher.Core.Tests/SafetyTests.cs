using System;
using System.IO;
using System.Linq;
using EternalVR.Launcher.Core.Data;
using EternalVR.Launcher.Core.Game;
using EternalVR.Launcher.Core.Safety;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    public class SettingsSnapshotTests
    {
        private static readonly string[] Forced = { "r_hdrDisplay", "r_dof" };

        private static TempDir MakeTree(out System.Collections.Generic.IReadOnlyList<SettingsLocation> locs)
        {
            var t = new TempDir();
            t.Write("saved/base/DOOMEternalConfig.cfg", "configVersion 9\nr_swapInterval \"1\"\n");
            t.Write("saved/base/DOOMEternalConfig.local", "r_mode \"25\"\nr_hdrDisplay \"1\"\n");
            t.Write("saved/user/config.json", "{\"fp\":\"x\"}");
            t.Write("steam/userdata/111/782330/remote/PROFILE/profile.bin", "profile-v1");
            locs = GameLayout.FindSettingsLocations(t.Combine("saved"), t.Combine("steam"), "111");
            return t;
        }

        [Fact]
        public void SessionThatForcedKeysIsRestoredKeyByKey()
        {
            using (var t = MakeTree(out var locs))
            {
                var snap = SettingsSnapshot.Take(t.Combine("data", "snapshots"), "s1", locs);
                Assert.True(SettingsSnapshot.IsComplete(snap));

                // What a VR session may do: drop a forced key, add another, and the player changes r_mode.
                t.Write("saved/base/DOOMEternalConfig.local", "r_mode \"30\"\nr_windowWidth \"2560\"\n");
                t.Write("saved/base/DOOMEternalConfig.cfg", "configVersion 9\nr_swapInterval \"1\"\nr_dof \"0\"\n");

                var report = SettingsSnapshot.Restore(snap, Forced);
                Assert.Equal(2, report.Restored.Count);
                Assert.Equal("r_mode \"30\"\nr_windowWidth \"2560\"\nr_hdrDisplay \"1\"\n", t.Read("saved/base/DOOMEternalConfig.local"));
                Assert.Equal("configVersion 9\nr_swapInterval \"1\"\n", t.Read("saved/base/DOOMEternalConfig.cfg"));
                Assert.True(File.Exists(Path.Combine(snap, "replaced", "saved-games", "base", "DOOMEternalConfig.local")));
                Assert.True(File.Exists(Path.Combine(snap, "restore.txt")));
            }
        }

        [Fact]
        public void BinaryProfileChangeIsReportedNotOverwritten()
        {
            using (var t = MakeTree(out var locs))
            {
                var snap = SettingsSnapshot.Take(t.Combine("data", "snapshots"), "s1", locs);
                t.Write("steam/userdata/111/782330/remote/PROFILE/profile.bin", "profile-v2");
                var report = SettingsSnapshot.Restore(snap, Forced);
                // The game rewrites profile.bin every session: a Steam Cloud line, not the warning about local files.
                Assert.Single(report.ChangedInCloud, l => l.Contains("PROFILE/profile.bin"));
                Assert.Empty(report.ChangedNotRestored);
                Assert.Equal("profile-v2", t.Read("steam/userdata/111/782330/remote/PROFILE/profile.bin"));
            }
        }

        [Fact]
        public void AKeyedConfigInASteamCloudFolderIsComparedOnly()
        {
            using (var t = MakeTree(out var locs))
            {
                // A future game build that keeps a text config under PROFILE: Steam records it, so it is never rewritten.
                t.Write("steam/userdata/111/782330/remote/PROFILE/x.cfg", "r_dof \"1\"\n");
                var snap = SettingsSnapshot.Take(t.Combine("data", "snapshots"), "s1", locs);
                t.Write("steam/userdata/111/782330/remote/PROFILE/x.cfg", "r_dof \"0\"\nr_hdrDisplay \"0\"\n");
                var report = SettingsSnapshot.Restore(snap, Forced);
                Assert.Equal("r_dof \"0\"\nr_hdrDisplay \"0\"\n", t.Read("steam/userdata/111/782330/remote/PROFILE/x.cfg"));
                Assert.Contains(report.ChangedInCloud, l => l.Contains("PROFILE/x.cfg"));
                Assert.DoesNotContain(report.Restored, l => l.Contains("x.cfg"));
                Assert.False(Directory.Exists(Path.Combine(snap, "replaced", "steam-111")));
            }
        }

        [Fact]
        public void WindowKeysTheGameSavedAreRestoredWithTheForcedKeys()
        {
            using (var t = MakeTree(out var locs))
            {
                // The owner's flat window position is on the primary display before the session.
                t.Write("saved/base/DOOMEternalConfig.local", "r_mode \"25\"\nr_hdrDisplay \"1\"\nr_windowPosX \"100\"\n");
                var snap = SettingsSnapshot.Take(t.Combine("data", "snapshots"), "s1", locs);

                // The game quits from the stereo window the layer placed on the virtual display and saves
                // it; none of the window keys was on its command line except the forced ones.
                t.Write("saved/base/DOOMEternalConfig.local",
                    "r_mode \"30\"\nr_windowPosX \"2569\"\nm_sensitivity \"7\"\nr_windowPosY \"0\"\nr_windowWidth \"2064\"\nr_windowHeight \"2100\"\nr_fullscreen \"0\"\nr_displayRefresh \"60\"\n");

                var report = SettingsSnapshot.Restore(snap, LauncherData.Load(TestData.Dir).RestoredKeys);

                Assert.Equal("r_mode \"25\"\nr_windowPosX \"100\"\nm_sensitivity \"7\"\nr_hdrDisplay \"1\"\n", t.Read("saved/base/DOOMEternalConfig.local"));
                Assert.Contains(report.Restored, r => r.Contains("r_windowPosX: \"2569\" -> \"100\"") && r.Contains("r_windowPosY: removed"));
                // Only the forced keys: the window position would stay on the virtual display.
                t.Write("saved/base/DOOMEternalConfig.local", "r_windowPosX \"2569\"\n");
                SettingsSnapshot.Restore(snap, Forced);
                Assert.Contains("r_windowPosX \"2569\"", t.Read("saved/base/DOOMEternalConfig.local"));
            }
        }

        [Fact]
        public void CvarsOfExtraGameArgumentsAreRestoredWithTheSession()
        {
            using (var t = MakeTree(out var locs))
            {
                t.Write("saved/base/DOOMEternalConfig.local", "r_mode \"25\"\nm_sensitivity \"5\"\n");
                var extra = Launch.SessionKeys.FromArguments("+m_sensitivity 3 +set r_fullscreen 1");
                var snap = SettingsSnapshot.Take(t.Combine("data", "snapshots"), "s1", locs, extra);

                // The game saved the session's values on exit.
                t.Write("saved/base/DOOMEternalConfig.local", "r_mode \"25\"\nm_sensitivity \"3\"\nr_fullscreen \"1\"\n");
                // The launcher's list alone (as after a crash, from the marker): the snapshot adds its own keys.
                var report = SettingsSnapshot.Restore(snap, Forced);
                Assert.Equal("r_mode \"25\"\nm_sensitivity \"5\"\n", t.Read("saved/base/DOOMEternalConfig.local"));
                Assert.Contains(report.Restored, r => r.Contains("m_sensitivity: \"3\" -> \"5\"") && r.Contains("r_fullscreen: removed"));
                Assert.Equal(new[] { "m_sensitivity", "r_fullscreen" }, SettingsSnapshot.SessionKeysOf(snap));
            }
        }

        [Fact]
        public void LocalConfigCreatedDuringTheSessionLosesTheWindowKeys()
        {
            using (var t = new TempDir())
            {
                t.Write("saved/base/DOOMEternalConfig.cfg", "configVersion 9\n");
                var locs = GameLayout.FindSettingsLocations(t.Combine("saved"), null, null);
                var snap = SettingsSnapshot.Take(t.Combine("snaps"), "s1", locs);
                t.Write("saved/base/DOOMEternalConfig.local", "r_windowPosX \"2569\"\nr_windowPosY \"0\"\nr_fullscreen \"0\"\nm_sensitivity \"7\"\n");

                SettingsSnapshot.Restore(snap, LauncherData.Load(TestData.Dir).RestoredKeys);

                Assert.Equal("m_sensitivity \"7\"\n", t.Read("saved/base/DOOMEternalConfig.local"));
            }
        }

        [Fact]
        public void DeletedConfigIsRestoredWhole()
        {
            using (var t = MakeTree(out var locs))
            {
                var snap = SettingsSnapshot.Take(t.Combine("data", "snapshots"), "s1", locs);
                File.Delete(t.Combine("saved", "base", "DOOMEternalConfig.local"));
                SettingsSnapshot.Restore(snap, Forced);
                Assert.Equal("r_mode \"25\"\nr_hdrDisplay \"1\"\n", t.Read("saved/base/DOOMEternalConfig.local"));
            }
        }

        [Fact]
        public void RestoreIsIdempotent()
        {
            using (var t = MakeTree(out var locs))
            {
                var snap = SettingsSnapshot.Take(t.Combine("data", "snapshots"), "s1", locs);
                t.Write("saved/base/DOOMEternalConfig.local", "r_mode \"25\"\nr_hdrDisplay \"0\"\n");
                SettingsSnapshot.Restore(snap, Forced);
                var second = SettingsSnapshot.Restore(snap, Forced);
                Assert.Empty(second.Restored);
            }
        }

        [Fact]
        public void CorruptOrIncompleteSnapshotRefusesToRestore()
        {
            using (var t = MakeTree(out var locs))
            {
                var snap = SettingsSnapshot.Take(t.Combine("data", "snapshots"), "s1", locs);
                File.WriteAllText(Path.Combine(snap, "saved-games", "base", "DOOMEternalConfig.local"), "tampered");
                Assert.Throws<IOException>(() => SettingsSnapshot.Restore(snap, Forced));

                File.Delete(Path.Combine(snap, "COMPLETE"));
                Assert.Throws<IOException>(() => SettingsSnapshot.Restore(snap, Forced));
            }
        }

        [Fact]
        public void SnapshotFolderIsNeverReused()
        {
            using (var t = MakeTree(out var locs))
            {
                SettingsSnapshot.Take(t.Combine("snaps"), "s1", locs);
                Assert.Throws<IOException>(() => SettingsSnapshot.Take(t.Combine("snaps"), "s1", locs));
            }
        }

        [Fact]
        public void PruneKeepsNewestAndProtected()
        {
            using (var t = new TempDir())
            {
                foreach (var n in new[] { "20260101-000000", "20260102-000000", "20260103-000000", "20260104-000000" })
                    Directory.CreateDirectory(t.Combine("snaps", n));
                SettingsSnapshot.Prune(t.Combine("snaps"), 2, t.Combine("snaps", "20260101-000000"));
                var left = Directory.GetDirectories(t.Combine("snaps")).Select(Path.GetFileName).OrderBy(n => n).ToArray();
                Assert.Equal(new[] { "20260101-000000", "20260103-000000", "20260104-000000" }, left);
            }
        }
    }

    public class SaveBackupTests
    {
        private static TempDir MakeSaves(out System.Collections.Generic.IReadOnlyList<SettingsLocation> locs)
        {
            var t = new TempDir();
            t.Write("steam/userdata/111/782330/remote/PROFILE/profile.bin", "profile");
            t.Write("steam/userdata/111/782330/remote/GAME-AUTOSAVE0/game.details", "d1");
            t.Write("steam/userdata/111/782330/remote/GAME-AUTOSAVE0/game_duration.dat", "g1");
            t.Write("steam/userdata/111/782330/remote/HORDE-AUTOSAVE9/game.details", "h1");
            locs = GameLayout.FindSettingsLocations(null, t.Combine("steam"), "111");
            return t;
        }

        [Fact]
        public void BackupCoversSlotsButNotTheProfile()
        {
            using (var t = MakeSaves(out var locs))
            {
                var dir = SaveBackups.Create(t.Combine("backups"), "20260101-000000", locs);
                Assert.Empty(SaveBackups.Verify(dir));
                Assert.True(File.Exists(Path.Combine(dir, "steam-111", "GAME-AUTOSAVE0", "game_duration.dat")));
                Assert.False(Directory.Exists(Path.Combine(dir, "steam-111", "PROFILE")));
            }
        }

        [Fact]
        public void RotationKeepsFive()
        {
            using (var t = MakeSaves(out var locs))
            {
                for (int i = 1; i <= 7; i++) SaveBackups.Create(t.Combine("backups"), $"2026010{i}-000000", locs);
                Directory.CreateDirectory(t.Combine("backups", "20260109-000000")); // incomplete, no sums
                SaveBackups.Rotate(t.Combine("backups"));
                var names = SaveBackups.List(t.Combine("backups")).Select(Path.GetFileName).ToArray();
                Assert.Equal(new[] { "20260107-000000", "20260106-000000", "20260105-000000", "20260104-000000", "20260103-000000" }, names);
                Assert.False(Directory.Exists(t.Combine("backups", "20260109-000000")));
            }
        }

        [Fact]
        public void SevenRestoresKeepFivePreRestoreBackups()
        {
            using (var t = MakeSaves(out var locs))
            {
                var dir = SaveBackups.Create(t.Combine("backups"), "20260101-000000", locs);
                for (int i = 1; i <= 7; i++)
                {
                    t.Write("steam/userdata/111/782330/remote/GAME-AUTOSAVE0/game.details", "played " + i);
                    SaveBackups.Restore(t.Combine("backups"), dir, new DateTime(2026, 1, 2, 0, 0, i));
                }
                var pre = SaveBackups.PreRestoreBackups(t.Combine("backups")).Select(Path.GetFileName).ToArray();
                Assert.Equal(new[] { "pre-restore-20260102-000007", "pre-restore-20260102-000006", "pre-restore-20260102-000005",
                    "pre-restore-20260102-000004", "pre-restore-20260102-000003" }, pre);
                Assert.Equal("played 7", File.ReadAllText(Path.Combine(t.Combine("backups"), pre[0], "steam-111", "GAME-AUTOSAVE0", "game.details")));
                // The rotating backup restored from is not one of them.
                Assert.Equal(new[] { "20260101-000000" }, SaveBackups.List(t.Combine("backups")).Select(Path.GetFileName));

                // Older ones left by an earlier launcher go at the next rotation.
                Directory.CreateDirectory(t.Combine("backups", "pre-restore-20250101-000000"));
                SaveBackups.Rotate(t.Combine("backups"));
                Assert.Equal(SaveBackups.Keep, SaveBackups.PreRestoreBackups(t.Combine("backups")).Count);
                Assert.False(Directory.Exists(t.Combine("backups", "pre-restore-20250101-000000")));
            }
        }

        [Fact]
        public void AHalfCopiedPreRestoreBackupDoesNotPushOutAComplete()
        {
            using (var t = MakeSaves(out var locs))
            {
                var dir = SaveBackups.Create(t.Combine("backups"), "20260101-000000", locs);
                for (int i = 1; i <= 5; i++) SaveBackups.Restore(t.Combine("backups"), dir, new DateTime(2026, 1, 2, 0, 0, i));
                // A crash in the middle of the next pre-restore copy: files, no sums.
                t.Write("backups/pre-restore-20260103-000000/steam-111/GAME-AUTOSAVE0/game.details", "half");

                Assert.Equal(5, SaveBackups.PreRestoreBackups(t.Combine("backups")).Count);
                Assert.DoesNotContain(SaveBackups.PreRestoreBackups(t.Combine("backups")), d => d.EndsWith("20260103-000000", StringComparison.Ordinal));
                SaveBackups.Rotate(t.Combine("backups"));
                Assert.False(Directory.Exists(t.Combine("backups", "pre-restore-20260103-000000")));
                Assert.True(Directory.Exists(t.Combine("backups", "pre-restore-20260102-000001")));
                Assert.Equal(5, SaveBackups.PreRestoreBackups(t.Combine("backups")).Count);

                // A restore prunes the same way.
                t.Write("backups/pre-restore-20260103-000000/steam-111/GAME-AUTOSAVE0/game.details", "half");
                SaveBackups.Restore(t.Combine("backups"), dir, new DateTime(2026, 1, 2, 0, 0, 6));
                Assert.False(Directory.Exists(t.Combine("backups", "pre-restore-20260103-000000")));
                Assert.Equal(new[] { "pre-restore-20260102-000006", "pre-restore-20260102-000005", "pre-restore-20260102-000004",
                    "pre-restore-20260102-000003", "pre-restore-20260102-000002" }, SaveBackups.PreRestoreBackups(t.Combine("backups")).Select(Path.GetFileName));
            }
        }

        [Fact]
        public void VerifyFindsCorruption()
        {
            using (var t = MakeSaves(out var locs))
            {
                var dir = SaveBackups.Create(t.Combine("backups"), "20260101-000000", locs);
                File.WriteAllText(Path.Combine(dir, "steam-111", "GAME-AUTOSAVE0", "game.details"), "bad");
                Assert.Single(SaveBackups.Verify(dir));
                Assert.Throws<IOException>(() => SaveBackups.Restore(t.Combine("backups"), dir, new DateTime(2026, 1, 2)));
            }
        }

        [Fact]
        public void RestorePutsSlotsBackAndKeepsThePreviousSaves()
        {
            using (var t = MakeSaves(out var locs))
            {
                var dir = SaveBackups.Create(t.Combine("backups"), "20260101-000000", locs);
                t.Write("steam/userdata/111/782330/remote/GAME-AUTOSAVE0/game.details", "d2");
                t.Write("steam/userdata/111/782330/remote/GAME-AUTOSAVE0/extra.tmp", "x");
                t.Write("steam/userdata/111/782330/remote/GAME-AUTOSAVE1/game.details", "new slot");

                var pre = SaveBackups.Restore(t.Combine("backups"), dir, new DateTime(2026, 1, 2, 3, 4, 5));

                Assert.Equal("d1", t.Read("steam/userdata/111/782330/remote/GAME-AUTOSAVE0/game.details"));
                Assert.False(File.Exists(t.Combine("steam", "userdata", "111", "782330", "remote", "GAME-AUTOSAVE0", "extra.tmp")));
                Assert.Equal("new slot", t.Read("steam/userdata/111/782330/remote/GAME-AUTOSAVE1/game.details")); // not in the backup: untouched
                Assert.EndsWith("pre-restore-20260102-030405", pre);
                Assert.Equal("d2", File.ReadAllText(Path.Combine(pre, "steam-111", "GAME-AUTOSAVE0", "game.details")));
                Assert.Equal(new[] { "20260101-000000" }, SaveBackups.List(t.Combine("backups")).Select(Path.GetFileName));
            }
        }
    }

    public class SessionMarkerTests
    {
        [Fact]
        public void RoundTrip()
        {
            using (var t = new TempDir())
            {
                var path = t.Combine("SESSION_PENDING");
                var start = new DateTime(2026, 9, 26, 1, 2, 3, DateTimeKind.Utc);
                new SessionMarker { State = SessionState.Running, SessionId = "s1", SnapshotDir = @"E:\x\snap", GamePid = 42, GameStartUtc = start, GameExe = "g.exe" }.Write(path);
                var m = SessionMarker.Read(path);
                Assert.Equal(SessionState.Running, m.State);
                Assert.Equal("s1", m.SessionId);
                Assert.Equal(@"E:\x\snap", m.SnapshotDir);
                Assert.Equal(42, m.GamePid);
                Assert.Equal(start, m.GameStartUtc);
                Assert.Null(SessionMarker.Read(t.Combine("missing")));
            }
        }

        [Fact]
        public void UnreadableStateIsTreatedAsHavingASnapshot()
        {
            using (var t = new TempDir())
            {
                var path = t.Write("SESSION_PENDING", "state = ???\nsession = s1\n");
                Assert.Equal(SessionState.Snapshotted, SessionMarker.Read(path).State);
            }
        }

        [Theory]
        [InlineData(null, false, RecoveryAction.None)]
        [InlineData(SessionState.Preparing, true, RecoveryAction.Discard)]
        [InlineData(SessionState.Snapshotted, false, RecoveryAction.Restore)]
        [InlineData(SessionState.Running, false, RecoveryAction.Restore)]
        [InlineData(SessionState.Running, true, RecoveryAction.WaitForGame)]
        [InlineData(SessionState.Snapshotted, true, RecoveryAction.WaitForGame)]
        public void RecoveryDecision(SessionState? state, bool gameRunning, RecoveryAction expected)
        {
            var marker = state.HasValue ? new SessionMarker { State = state.Value } : null;
            Assert.Equal(expected, SessionRecovery.Decide(marker, gameRunning));
        }
    }
}
