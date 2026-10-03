using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using EternalVR.Launcher.Core.Game;
using EternalVR.Launcher.Core.Preflight;
using EternalVR.Launcher.Core.Safety;
using EternalVR.Launcher.Core.Steam;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>
    /// Steam's cloud record in the fixtures, as Steam writes it at the end of an app session (the same
    /// format as the rig suite's Format-TestCloudRecord in tools/rig/tests/run-tests.ps1).
    /// </summary>
    internal static class CloudFixture
    {
        public const string Remote = "steam/userdata/111/782330/remote";

        /// <summary>
        /// The text of <c>remotecache.vdf</c> for every file under <paramref name="remoteDir"/>;
        /// <paramref name="stale"/> maps a relative path (with /) to the SHA-1 to write instead of the file's own.
        /// </summary>
        public static string Format(string remoteDir, IDictionary<string, string> stale = null)
        {
            var root = Path.GetFullPath(remoteDir).TrimEnd('\\', '/');
            var sb = new StringBuilder("\"782330\"\n{\n\t\"ChangeNumber\"\t\t\"7\"\n\t\"OSType\"\t\t\"0\"\n");
            foreach (var f in Directory.GetFiles(root, "*", SearchOption.AllDirectories).OrderBy(x => x, StringComparer.Ordinal))
            {
                var rel = f.Substring(root.Length + 1).Replace('\\', '/');
                var sha = SteamCloudCache.Sha1OfFile(f);
                if (stale != null && stale.TryGetValue(rel, out var s)) sha = s;
                sb.Append($"\t\"{rel}\"\n\t{{\n\t\t\"root\"\t\t\"0\"\n\t\t\"size\"\t\t\"{new FileInfo(f).Length}\"\n\t\t\"sha\"\t\t\"{sha}\"\n")
                  .Append("\t\t\"syncstate\"\t\t\"1\"\n\t\t\"persiststate\"\t\t\"0\"\n\t}\n");
            }
            return sb.Append("}\n").ToString();
        }

        public static void WriteRecord(TempDir t, IDictionary<string, string> stale = null)
        {
            var remote = t.Combine(Remote.Split('/'));
            File.WriteAllText(SteamCloudCache.RecordPathFor(remote), Format(remote, stale));
        }

        public static TempDir MakeTree(out IReadOnlyList<SettingsLocation> locs)
        {
            var t = new TempDir();
            t.Write("saved/base/DOOMEternalConfig.local", "r_mode \"25\"\n");
            t.Write(Remote + "/PROFILE/profile.bin", "profile-v1");
            t.Write(Remote + "/GAME-AUTOSAVE0/game_duration.dat", new string('a', 512));
            t.Write(Remote + "/GAME-MANUAL1/game.details", new string('b', 256));
            locs = GameLayout.FindSettingsLocations(t.Combine("saved"), t.Combine("steam"), "111");
            return t;
        }
    }

    public class SteamCloudCacheTests
    {
        [Fact]
        public void RecordIsReadPerFile()
        {
            using (var t = CloudFixture.MakeTree(out _))
            {
                CloudFixture.WriteRecord(t, new Dictionary<string, string> { ["PROFILE/profile.bin"] = new string('0', 40) });
                var record = SteamCloudCache.Read(t.Combine(CloudFixture.Remote.Split('/')));
                Assert.True(record.Present);
                Assert.Null(record.Error);
                Assert.Equal(3, record.Files.Count);
                var save = record.Files[Path.Combine("GAME-AUTOSAVE0", "game_duration.dat")];
                Assert.Equal(512, save.Size);
                Assert.Equal(SteamCloudCache.Sha1OfFile(t.Combine(CloudFixture.Remote + "/GAME-AUTOSAVE0/game_duration.dat")), save.Sha1);
                Assert.Equal("1", save.SyncState);
            }
        }

        [Fact]
        public void OnlyRootZeroEntriesAreFilesOfTheRemoteFolder()
        {
            var record = new CloudRecord("x");
            SteamCloudCache.Parse("\"782330\" { \"ChangeNumber\" \"3\" \"a.bin\" { \"root\" \"0\" \"size\" \"1\" \"sha\" \"ABC\" } \"b.bin\" { \"root\" \"2\" \"size\" \"1\" } }", record);
            Assert.True(record.Present);
            Assert.Equal(new[] { "a.bin" }, record.Files.Keys.ToArray());
            Assert.Equal("abc", record.Files["a.bin"].Sha1);
        }

        [Fact]
        public void StaleUntrackedAndUnreadableAreReported()
        {
            using (var t = CloudFixture.MakeTree(out var locs))
            {
                CloudFixture.WriteRecord(t, new Dictionary<string, string> { ["PROFILE/profile.bin"] = new string('0', 40) });
                var c = SteamCloudCache.Check(locs);
                Assert.Equal(1, c.Checked);
                Assert.Equal(new[] { "steam-111/PROFILE/profile.bin" }, c.Stale.Select(s => s.Key).ToArray());
                Assert.Empty(c.Untracked);
                Assert.Empty(c.Unreadable);

                t.Write(CloudFixture.Remote + "/GAME-MANUAL2/game.details", "new");
                Assert.Equal(new[] { "steam-111/GAME-MANUAL2/game.details" }, SteamCloudCache.Check(locs).Untracked.ToArray());

                // A size change alone is enough.
                CloudFixture.WriteRecord(t);
                t.Write(CloudFixture.Remote + "/PROFILE/profile.bin", "profile-v2-longer");
                Assert.Single(SteamCloudCache.Check(locs).Stale);

                File.WriteAllText(SteamCloudCache.RecordPathFor(locs[1].Path), "\"782330\" { \"PROFILE/profile.bin\" { \"size\" \"1\" ");
                var broken = SteamCloudCache.Check(locs);
                Assert.Single(broken.Unreadable);
                Assert.Equal(0, broken.Checked);
            }
        }

        [Fact]
        public void MissingRecordIsNotChecked()
        {
            using (var t = CloudFixture.MakeTree(out var locs))
            {
                var c = SteamCloudCache.Check(locs);
                Assert.Equal(0, c.Checked);
                Assert.Empty(c.Unreadable);
                Assert.True(c.Consistent);
            }
        }

        [Fact]
        public void StaleRecordIsAPreflightWarning()
        {
            var f = new PreflightFacts
            {
                SteamRunning = true, SteamLoggedIn = true, GameRoot = "g",
                Build = new BuildCheck(BuildStatus.Known, "ab", new KnownBuild(new string('a', 64), "1", "t")),
                LayerManifestExists = true, LayerLibraryExists = true, RuntimeManifest = "r", RuntimeManifestExists = true,
                SettingsLocationCount = 2, CloudRecordStale = new[] { "steam-111/PROFILE/profile.bin" },
            };
            var r = PreflightEvaluator.Evaluate(f);
            var c = r.Checks.Single(x => x.Id == "cloud-record");
            Assert.Equal(Severity.Warn, c.Severity);
            Assert.Contains("PROFILE/profile.bin", c.Message);
            Assert.Contains("through Steam", c.Message);
            Assert.True(r.CanLaunch);

            f.CloudRecordStale = new string[0];
            Assert.DoesNotContain(PreflightEvaluator.Evaluate(f).Checks, x => x.Id == "cloud-record");
        }
    }

    public class CloudSettingsRestoreTests
    {
        [Fact]
        public void ProfileRemovedDuringTheSessionIsNotPutBack()
        {
            using (var t = CloudFixture.MakeTree(out var locs))
            {
                var snap = SettingsSnapshot.Take(t.Combine("data", "snapshots"), "s1", locs);
                var profile = t.Combine(CloudFixture.Remote + "/PROFILE/profile.bin");
                File.Delete(profile);
                File.Delete(t.Combine("saved", "base", "DOOMEternalConfig.local"));

                var report = SettingsSnapshot.Restore(snap, new[] { "r_dof" });
                Assert.False(File.Exists(profile));
                Assert.Contains(report.ChangedInCloud, l => l.Contains("PROFILE/profile.bin") && l.Contains("removed"));
                Assert.Empty(report.ChangedNotRestored);
                Assert.Contains(report.Lines, l => l.Contains("Steam Cloud file kept") && l.Contains("PROFILE/profile.bin"));
                // The local text config is still restored whole.
                Assert.Equal("r_mode \"25\"\n", t.Read("saved/base/DOOMEternalConfig.local"));
            }
        }
    }

    /// <summary>The machine as the resync step sees it, with a clock that moves only when waited on.</summary>
    internal sealed class FakeHost : ICloudResyncHost
    {
        public bool Steam = true;
        public List<int> Games = new List<int>();
        public DateTime Now = new DateTime(2026, 9, 26, 3, 0, 0, DateTimeKind.Utc);
        public List<string> Started = new List<string>();
        public IReadOnlyDictionary<string, string> StartEnv;
        public List<int> Killed = new List<int>();
        /// <summary>Runs when the stand-in game starts (it plays Steam and the game).</summary>
        public Action OnStart;
        /// <summary>Runs on every wait.</summary>
        public Action OnWait;
        /// <summary>Game processes that appear once the stand-in has started (a hand-off).</summary>
        public List<int> AppearAfterStart = new List<int>();

        public bool SteamRunning() => Steam;
        public IReadOnlyList<int> GameProcessIds() => Games.ToList();

        public int Start(string exe, string arguments, string workingDirectory, IReadOnlyDictionary<string, string> environment)
        {
            Started.Add(exe + " " + arguments);
            StartEnv = environment;
            Games.Add(1000);
            Games.AddRange(AppearAfterStart);
            OnStart?.Invoke();
            return 1000;
        }

        public bool KillAndWait(int pid, int timeoutMs)
        {
            Killed.Add(pid);
            Games.Remove(pid);
            return true;
        }

        public void Wait(int ms)
        {
            Now = Now.AddMilliseconds(ms);
            OnWait?.Invoke();
        }

        public DateTime UtcNow => Now;
    }

    public class SaveRestoreTests
    {
        private static string Save(TempDir t) => t.Combine(CloudFixture.Remote + "/GAME-AUTOSAVE0/game_duration.dat");

        /// <summary>A tree with a save backup, a later save written by the game and a matching record.</summary>
        private static TempDir Prepared(out IReadOnlyList<SettingsLocation> locs, out string backup, out CloudResyncOptions options)
        {
            var t = CloudFixture.MakeTree(out locs);
            backup = SaveBackups.Create(t.Combine("backups"), "20260926-010000", locs);
            File.WriteAllText(Save(t), new string('z', 700));
            CloudFixture.WriteRecord(t);
            options = new CloudResyncOptions { Exe = t.Write("game/DOOMEternalx64vk.exe", "stand-in"), WorkingDirectory = t.Combine("game") };
            return t;
        }

        private static SaveRestoreResult Run(TempDir t, IReadOnlyList<SettingsLocation> locs, string backup, FakeHost host, CloudResyncOptions options, List<string> log = null) =>
            SaveRestore.Run(t.Combine("backups"), backup, new DateTime(2026, 9, 26, 3, 0, 0), locs, host, options, l => log?.Add(l));

        [Fact]
        public void RefusedWhileTheGameRuns()
        {
            using (var t = Prepared(out var locs, out var backup, out var options))
            {
                var r = Run(t, locs, backup, new FakeHost { Games = { 42 } }, options);
                Assert.Equal(SaveRestoreOutcome.Refused, r.Outcome);
                Assert.Contains("Quit the game", r.Summary);
                Assert.Equal(new string('z', 700), File.ReadAllText(Save(t)));
                Assert.Single(SaveBackups.List(t.Combine("backups")));
                Assert.Single(Directory.GetDirectories(t.Combine("backups")));
            }
        }

        [Fact]
        public void RefusedWithoutSteam()
        {
            using (var t = Prepared(out var locs, out var backup, out var options))
            {
                var r = Run(t, locs, backup, new FakeHost { Steam = false }, options);
                Assert.Equal(SaveRestoreOutcome.Refused, r.Outcome);
                Assert.Contains("Start Steam", r.Summary);
                Assert.Equal(new string('z', 700), File.ReadAllText(Save(t)));
            }
        }

        [Fact]
        public void RefusedWhileSteamSyncs()
        {
            using (var t = Prepared(out var locs, out var backup, out var options))
            {
                // Steam writes its record during the quiet window.
                var host = new FakeHost { OnWait = () => File.AppendAllText(SteamCloudCache.RecordPathFor(locs[1].Path), "\n") };
                var r = Run(t, locs, backup, host, options);
                Assert.Equal(SaveRestoreOutcome.Refused, r.Outcome);
                Assert.Contains("syncing", r.Summary);
                Assert.Equal(new string('z', 700), File.ReadAllText(Save(t)));
                Assert.Empty(host.Started);
            }
        }

        [Fact]
        public void StaleRecordIsResyncedByAShortLaunch()
        {
            using (var t = Prepared(out var locs, out var backup, out var options))
            {
                var local = t.Combine("saved", "base", "DOOMEternalConfig.local");
                var host = new FakeHost
                {
                    // The stand-in plays Steam taking the files on disk, and the game touching a local config.
                    OnStart = () =>
                    {
                        CloudFixture.WriteRecord(t);
                        File.AppendAllText(local, "r_fullscreen \"0\"\n");
                    },
                };
                var log = new List<string>();
                var r = Run(t, locs, backup, host, options, log);

                Assert.Equal(SaveRestoreOutcome.Restored, r.Outcome);
                Assert.True(r.Resynced);
                Assert.Contains("Steam's cloud record now matches", r.Summary);
                Assert.Equal(new string('a', 512), File.ReadAllText(Save(t)));
                Assert.NotNull(r.PreRestoreDir);
                Assert.Equal(new[] { options.Exe + " +r_fullscreen 0 +s_volume 0" }, host.Started.ToArray());
                Assert.Equal("782330", host.StartEnv["SteamAppId"]);
                Assert.Equal(new[] { 1000 }, host.Killed.ToArray());
                Assert.Equal("r_mode \"25\"\n", File.ReadAllText(local));
                Assert.True(SteamCloudCache.Check(locs).Consistent);
                Assert.Contains(log, l => l.Contains("Steam's cloud record is stale after the restore"));
            }
        }

        [Fact]
        public void FailedResyncTellsTheUserToLaunchThroughSteam()
        {
            using (var t = Prepared(out var locs, out var backup, out var options))
            {
                var host = new FakeHost();
                var r = Run(t, locs, backup, host, options);
                Assert.Equal(SaveRestoreOutcome.RestoredRecordStale, r.Outcome);
                Assert.Contains("through Steam and quit at the main menu", r.Summary);
                Assert.Equal(new string('a', 512), File.ReadAllText(Save(t)));
                Assert.Single(host.Started);
                // The poll is bounded by the timeout.
                Assert.True(host.Now <= new DateTime(2026, 9, 26, 3, 0, 0, DateTimeKind.Utc).AddMilliseconds(options.SyncQuietMs + options.KillAfterMs + options.SyncTimeoutMs + options.PollMs));
            }
        }

        [Fact]
        public void MatchingRecordNeedsNoLaunch()
        {
            using (var t = Prepared(out var locs, out var backup, out var options))
            {
                // The record already describes the backed-up bytes (the game's write never synced).
                File.WriteAllText(Save(t), new string('a', 512));
                CloudFixture.WriteRecord(t);
                File.WriteAllText(Save(t), new string('z', 700));
                var host = new FakeHost();
                var r = Run(t, locs, backup, host, options);
                Assert.Equal(SaveRestoreOutcome.Restored, r.Outcome);
                Assert.False(r.Resynced);
                Assert.Empty(host.Started);
            }
        }

        [Fact]
        public void WithoutARecordTheRestoreIsUnverified()
        {
            using (var t = Prepared(out var locs, out var backup, out var options))
            {
                File.Delete(SteamCloudCache.RecordPathFor(locs[1].Path));
                var host = new FakeHost();
                var r = Run(t, locs, backup, host, options);
                Assert.Equal(SaveRestoreOutcome.RestoredUnverified, r.Outcome);
                Assert.Contains("through Steam", r.Summary);
                Assert.Empty(host.Started);
            }
        }
    }

    public class CloudResyncTests
    {
        [Fact]
        public void HandOffProcessIsClosedToo()
        {
            using (var t = CloudFixture.MakeTree(out var locs))
            {
                CloudFixture.WriteRecord(t, new Dictionary<string, string> { ["PROFILE/profile.bin"] = new string('0', 40) });
                var options = new CloudResyncOptions { Exe = t.Write("game/x.exe", "x"), WorkingDirectory = t.Combine("game") };
                var host = new FakeHost { AppearAfterStart = { 2000 }, OnStart = () => CloudFixture.WriteRecord(t) };
                var r = CloudResync.Run(options, host, locs, _ => { });
                Assert.True(r.Ok);
                Assert.Equal(new[] { 1000, 2000 }, host.Killed.ToArray());
            }
        }

        [Fact]
        public void RefusedWhileTheGameRuns()
        {
            using (var t = CloudFixture.MakeTree(out var locs))
            {
                var options = new CloudResyncOptions { Exe = t.Write("game/x.exe", "x") };
                var host = new FakeHost { Games = { 7 } };
                var r = CloudResync.Run(options, host, locs, _ => { });
                Assert.False(r.Ok);
                Assert.StartsWith("refused", r.Note);
                Assert.Empty(host.Started);
            }
        }

        [Fact]
        public void ALaunchThatRewritesACloudFileFails()
        {
            using (var t = CloudFixture.MakeTree(out var locs))
            {
                CloudFixture.WriteRecord(t, new Dictionary<string, string> { ["PROFILE/profile.bin"] = new string('0', 40) });
                var options = new CloudResyncOptions { Exe = t.Write("game/x.exe", "x"), WorkingDirectory = t.Combine("game") };
                var host = new FakeHost
                {
                    OnStart = () =>
                    {
                        t.Write(CloudFixture.Remote + "/PROFILE/profile.bin", "fresh profile");
                        CloudFixture.WriteRecord(t);
                    },
                };
                var r = CloudResync.Run(options, host, locs, _ => { });
                Assert.False(r.Ok);
                Assert.Contains("changed the restored cloud files", r.Note);
            }
        }
    }
}
