using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using EternalVR.Launcher.Core.Game;
using EternalVR.Launcher.Core.Steam;

namespace EternalVR.Launcher.Core.Safety
{
    /// <summary>What the resync step needs from the machine; replaced by a fake in the unit tests.</summary>
    public interface ICloudResyncHost
    {
        bool SteamRunning();
        /// <summary>Process IDs of every running process that counts as the game.</summary>
        IReadOnlyList<int> GameProcessIds();
        /// <summary>Starts the exe with the given extra environment; returns its process ID.</summary>
        int Start(string exe, string arguments, string workingDirectory, IReadOnlyDictionary<string, string> environment);
        /// <summary>Kills a process (if it still runs) and waits for it; false when it did not exit in time.</summary>
        bool KillAndWait(int pid, int timeoutMs);
        void Wait(int ms);
        DateTime UtcNow { get; }
    }

    /// <summary>The real host: processes through <see cref="System.Diagnostics.Process"/>.</summary>
    public sealed class ProcessResyncHost : ICloudResyncHost
    {
        private readonly IReadOnlyList<string> gameProcessNames;
        private readonly string steamProcessName;

        public ProcessResyncHost(IReadOnlyList<string> gameProcessNames, string steamProcessName = "steam")
        {
            this.gameProcessNames = gameProcessNames;
            this.steamProcessName = steamProcessName;
        }

        public bool SteamRunning() => Ids(steamProcessName).Count > 0;

        public IReadOnlyList<int> GameProcessIds() => gameProcessNames.SelectMany(Ids).ToList();

        public int Start(string exe, string arguments, string workingDirectory, IReadOnlyDictionary<string, string> environment)
        {
            var psi = new ProcessStartInfo(exe, arguments) { UseShellExecute = false, WorkingDirectory = workingDirectory };
            // Set on the start info only: the launcher's own environment is never changed.
            foreach (var kv in environment) psi.EnvironmentVariables[kv.Key] = kv.Value;
            using (var p = Process.Start(psi) ?? throw new InvalidOperationException("the game process did not start"))
                return p.Id;
        }

        public bool KillAndWait(int pid, int timeoutMs)
        {
            Process p;
            try { p = Process.GetProcessById(pid); }
            catch (ArgumentException) { return true; }
            using (p)
            {
                try { if (!p.HasExited) p.Kill(); }
                catch (InvalidOperationException) { }
                catch (System.ComponentModel.Win32Exception) { }
                return p.WaitForExit(timeoutMs);
            }
        }

        public void Wait(int ms) => System.Threading.Thread.Sleep(ms);

        public DateTime UtcNow => DateTime.UtcNow;

        private static IReadOnlyList<int> Ids(string name)
        {
            var ids = new List<int>();
            foreach (var p in Process.GetProcessesByName(name))
            {
                ids.Add(p.Id);
                p.Dispose();
            }
            return ids;
        }
    }

    public sealed class CloudResyncOptions
    {
        public const string DefaultArguments = "+r_fullscreen 0 +s_volume 0";

        public string Exe { get; set; }
        public string WorkingDirectory { get; set; }
        public string Arguments { get; set; } = DefaultArguments;
        /// <summary>The game is killed this long after its start, before it loads its profile (rig: 4 s).</summary>
        public int KillAfterMs { get; set; } = 4000;
        public int KillTimeoutMs { get; set; } = 10000;
        /// <summary>How long Steam's record is polled after the kill.</summary>
        public int SyncTimeoutMs { get; set; } = 60000;
        public int PollMs { get; set; } = 1000;
        /// <summary>"Restore saves": how long Steam's app folder must stay unchanged before anything is written.</summary>
        public int SyncQuietMs { get; set; } = 2000;
    }

    public sealed class CloudResyncResult
    {
        public CloudResyncResult(bool ok, string note, CloudConsistency after)
        {
            Ok = ok;
            Note = note;
            After = after;
        }

        /// <summary>Steam's record now matches every cloud file on disk.</summary>
        public bool Ok { get; }
        public string Note { get; }
        /// <summary>The last comparison with the record; null when the step was refused before the launch.</summary>
        public CloudConsistency After { get; }
    }

    /// <summary>
    /// The resync step of the rig (<c>Invoke-RigCloudResync</c>, T-115): with cloud files restored on
    /// disk and the game closed, the game exe is started directly (<c>SteamAppId=782330</c>, windowed and
    /// muted) and killed about 4 s in, before it loads its profile. At the end of that short app session
    /// Steam finds the restored files changed and takes them as the current version, so its record
    /// (<c>remotecache.vdf</c>) and the cloud then hold them. The record is polled until it matches or a
    /// timeout. The local text configs the short launch may touch are put back as they were, and the
    /// restored cloud files must still be as restored. Steam's files are only read.
    /// </summary>
    public static class CloudResync
    {
        public static CloudResyncResult Run(CloudResyncOptions options, ICloudResyncHost host,
                                            IReadOnlyList<SettingsLocation> locations, Action<string> log)
        {
            if (!host.SteamRunning()) return Refused("Steam is not running (Steam must see the short app session)", log);
            var before = new HashSet<int>(host.GameProcessIds());
            if (before.Count > 0) return Refused("a game process is running", log);
            if (string.IsNullOrEmpty(options.Exe) || !File.Exists(options.Exe)) return Refused("game exe not found: " + options.Exe, log);

            var cloudBefore = HashCloudFiles(locations);
            var local = LocalFiles.Capture(locations);

            var env = new Dictionary<string, string>
            {
                ["SteamAppId"] = GameLayout.SteamAppId,
                // The short launch is a plain game start: the VR layer stays off even if registered.
                ["ETERNALVR_DISABLE_LAYER"] = "1",
            };
            int pid;
            try { pid = host.Start(options.Exe, options.Arguments, options.WorkingDirectory, env); }
            catch (Exception e) when (e is InvalidOperationException || e is System.ComponentModel.Win32Exception || e is IOException)
            {
                return Failed("the game could not be started: " + e.Message, null, log);
            }
            log($"cloud resync: started {Path.GetFileName(options.Exe)} {options.Arguments} (pid {pid}); closing it after {options.KillAfterMs} ms, before the profile load");
            host.Wait(options.KillAfterMs);

            // A hand-off (the game restarting itself through Steam) is closed as well.
            var targets = new List<int> { pid };
            targets.AddRange(host.GameProcessIds().Where(id => id != pid && !before.Contains(id)));
            var survivors = targets.Where(id => !host.KillAndWait(id, options.KillTimeoutMs)).ToList();
            if (survivors.Count > 0)
                return Failed("process(es) " + string.Join(", ", survivors) + " did not exit after the kill", null, log);

            var putBack = local.PutBackChanged();
            foreach (var f in putBack) log("cloud resync: the short launch changed " + f + "; put back as it was");

            var changed = HashCloudFiles(locations).Where(kv => !cloudBefore.TryGetValue(kv.Key, out var h) || h != kv.Value).Select(kv => kv.Key)
                .Concat(cloudBefore.Keys.Where(k => !File.Exists(k))).ToList();
            if (changed.Count > 0)
                return Failed("the short launch changed the restored cloud files: " + string.Join(", ", changed), null, log);

            var deadline = host.UtcNow.AddMilliseconds(options.SyncTimeoutMs);
            while (true)
            {
                var check = SteamCloudCache.Check(locations);
                if (check.Checked > 0 && check.Consistent)
                {
                    log("cloud resync: Steam's record (remotecache.vdf) now matches every cloud file");
                    return new CloudResyncResult(true, "Steam took the restored files as the current version", check);
                }
                if (host.UtcNow >= deadline)
                {
                    var why = check.Checked == 0 ? "Steam's record could not be read" : "Steam's record still differs: " + string.Join("; ", check.Stale);
                    return Failed($"after {options.SyncTimeoutMs / 1000} s {why}", check, log);
                }
                host.Wait(options.PollMs);
            }
        }

        private static CloudResyncResult Refused(string why, Action<string> log)
        {
            log("cloud resync refused: " + why);
            return new CloudResyncResult(false, "refused: " + why, null);
        }

        private static CloudResyncResult Failed(string why, CloudConsistency after, Action<string> log)
        {
            log("cloud resync failed: " + why);
            return new CloudResyncResult(false, why, after);
        }

        private static Dictionary<string, string> HashCloudFiles(IEnumerable<SettingsLocation> locations)
        {
            var map = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            foreach (var loc in locations.Where(l => l.Kind == SettingsLocationKind.SteamRemote && Directory.Exists(l.Path)))
                foreach (var f in Directory.GetFiles(loc.Path, "*", SearchOption.AllDirectories))
                    map[f] = KnownBuilds.Sha256OfFile(f);
            return map;
        }

        /// <summary>The local text configs (Saved Games) as they were before the short launch.</summary>
        private sealed class LocalFiles
        {
            private readonly Dictionary<string, byte[]> files = new Dictionary<string, byte[]>(StringComparer.OrdinalIgnoreCase);

            public static LocalFiles Capture(IEnumerable<SettingsLocation> locations)
            {
                var l = new LocalFiles();
                foreach (var loc in locations.Where(x => x.Kind == SettingsLocationKind.SavedGames))
                    foreach (var rel in GameLayout.SavedGamesConfigFiles)
                    {
                        var path = Path.Combine(loc.Path, rel);
                        l.files[path] = File.Exists(path) ? File.ReadAllBytes(path) : null;
                    }
                return l;
            }

            /// <summary>Writes back every file that changed, removes one that was created; returns them.</summary>
            public List<string> PutBackChanged()
            {
                var done = new List<string>();
                foreach (var kv in files)
                {
                    bool exists = File.Exists(kv.Key);
                    if (kv.Value == null)
                    {
                        if (!exists) continue;
                        File.Delete(kv.Key);
                        done.Add(kv.Key);
                    }
                    else if (!exists || !File.ReadAllBytes(kv.Key).SequenceEqual(kv.Value))
                    {
                        Directory.CreateDirectory(Path.GetDirectoryName(kv.Key));
                        FileUtil.WriteAllBytesAtomic(kv.Key, kv.Value);
                        done.Add(kv.Key);
                    }
                }
                return done;
            }
        }
    }
}
