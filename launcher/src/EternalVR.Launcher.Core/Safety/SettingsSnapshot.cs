using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using EternalVR.Launcher.Core.Game;

namespace EternalVR.Launcher.Core.Safety
{
    /// <summary>What a restore did, for the log and the status pane.</summary>
    public sealed class RestoreReport
    {
        public List<string> Restored { get; } = new List<string>();
        public List<string> Unchanged { get; } = new List<string>();
        /// <summary>
        /// Local files outside the key-level restore that changed during the session (<c>user\config.json</c>): kept
        /// as the game left them, the snapshot holding the old copy. Worth a warning: nothing else puts them back.
        /// </summary>
        public List<string> ChangedNotRestored { get; } = new List<string>();
        /// <summary>
        /// Steam Cloud files that changed, appeared or went during the session (<c>profile.bin</c> changes every
        /// session): never written by the restore (T-115), so they are the game's as Steam synced them. Not a warning.
        /// </summary>
        public List<string> ChangedInCloud { get; } = new List<string>();

        public IEnumerable<string> Lines =>
            Restored.Select(r => "restored " + r)
                .Concat(ChangedNotRestored.Select(c => "changed, left as is (snapshot kept): " + c))
                .Concat(ChangedInCloud.Select(c => "changed, Steam Cloud file kept as the game left it: " + c))
                .Concat(Unchanged.Select(u => "unchanged " + u));
    }

    /// <summary>
    /// Settings backstop (T-036, ARCHITECTURE section 4): the game's config files in every settings
    /// location are copied into <c>snapshots\&lt;session&gt;\</c> with SHA-256 sums before launch; after
    /// the session the forced keys of the text configs are put back to their snapshot values. Binary and
    /// JSON files are compared only; a change is reported and the snapshot kept. The Steam-Cloud files
    /// (<c>782330\remote\</c>) are never written, not even when the game removed one: Steam records
    /// each one's size and SHA-1, and a file put back behind its back makes the game reset the profile
    /// (T-115). Only the local text configs are restored.
    /// </summary>
    public static class SettingsSnapshot
    {
        private const string LocationsFile = "locations.txt";
        private const string SumsFile = "SHA256SUMS";
        private const string CompleteFile = "COMPLETE";
        /// <summary>Config files that did not exist when the snapshot was taken (<c>location/relative</c> per line).</summary>
        private const string AbsentFile = "ABSENT";
        /// <summary>Marks a snapshot taken by <c>--dry-run</c>; never a restore source.</summary>
        public const string DryRunFile = "DRY_RUN";
        /// <summary>Keys this session's restore puts back besides the launcher's list (the Extra game arguments' cvars), one per line.</summary>
        private const string KeysFile = "SESSION_KEYS";

        /// <summary>
        /// Copies the config files of every location. <paramref name="sessionKeys"/> (the cvars the Extra game arguments set)
        /// are recorded with the snapshot, so its <see cref="Restore"/> puts them back too, also after a crash.
        /// </summary>
        public static string Take(string snapshotsRoot, string sessionId, IReadOnlyList<SettingsLocation> locations, IEnumerable<string> sessionKeys = null)
        {
            var dir = Path.Combine(snapshotsRoot, sessionId);
            if (Directory.Exists(dir)) throw new IOException("snapshot folder already exists: " + dir);
            Directory.CreateDirectory(dir);

            var locs = new StringBuilder();
            var sums = new StringBuilder();
            var absent = new StringBuilder();
            foreach (var loc in locations)
            {
                foreach (var rel in GameLayout.AbsentConfigFilesOf(loc))
                    absent.Append(loc.Name).Append('/').Append(rel.Replace('\\', '/')).Append('\n');
                locs.Append(loc.Name).Append('|').Append(loc.Kind).Append('|').Append(loc.Path).Append('\n');
                foreach (var rel in GameLayout.ConfigFilesOf(loc))
                {
                    var target = Path.Combine(dir, loc.Name, rel);
                    FileUtil.CopyVerified(Path.Combine(loc.Path, rel), target);
                    sums.Append(KnownBuilds.Sha256OfFile(target)).Append("  ").Append(loc.Name).Append('/').Append(rel.Replace('\\', '/')).Append('\n');
                }
            }
            File.WriteAllText(Path.Combine(dir, LocationsFile), locs.ToString());
            File.WriteAllText(Path.Combine(dir, SumsFile), sums.ToString());
            File.WriteAllText(Path.Combine(dir, AbsentFile), absent.ToString());
            var keys = (sessionKeys ?? Enumerable.Empty<string>()).Where(k => !string.IsNullOrWhiteSpace(k)).ToList();
            if (keys.Count > 0) File.WriteAllText(Path.Combine(dir, KeysFile), string.Join("\n", keys) + "\n");
            FileUtil.WriteAllTextAtomic(Path.Combine(dir, CompleteFile), DateTime.UtcNow.ToString("o"));
            return dir;
        }

        public static bool IsComplete(string snapshotDir) => File.Exists(Path.Combine(snapshotDir, CompleteFile));

        /// <summary>Snapshot entries: location, relative path and expected hash; verified against the copies.</summary>
        public static IReadOnlyList<SnapshotEntry> ReadVerified(string snapshotDir)
        {
            if (!IsComplete(snapshotDir)) throw new IOException("snapshot is incomplete: " + snapshotDir);
            var locations = new Dictionary<string, SettingsLocation>(StringComparer.OrdinalIgnoreCase);
            foreach (var line in File.ReadAllLines(Path.Combine(snapshotDir, LocationsFile)))
            {
                var parts = line.Split('|');
                if (parts.Length < 3) continue;
                var kind = (SettingsLocationKind)Enum.Parse(typeof(SettingsLocationKind), parts[1]);
                locations[parts[0]] = new SettingsLocation(parts[0], kind, parts[2]);
            }

            var entries = new List<SnapshotEntry>();
            foreach (var line in File.ReadAllLines(Path.Combine(snapshotDir, SumsFile)))
            {
                if (line.Length < 67) continue;
                var hash = line.Substring(0, 64);
                var name = line.Substring(66);
                int slash = name.IndexOf('/');
                var locName = name.Substring(0, slash);
                var rel = name.Substring(slash + 1).Replace('/', Path.DirectorySeparatorChar);
                if (!locations.TryGetValue(locName, out var loc)) throw new IOException("snapshot names an unknown location: " + locName);
                var copy = Path.Combine(snapshotDir, locName, rel);
                if (!File.Exists(copy) || !string.Equals(KnownBuilds.Sha256OfFile(copy), hash, StringComparison.OrdinalIgnoreCase))
                    throw new IOException("snapshot copy is missing or corrupt: " + copy);
                entries.Add(new SnapshotEntry(loc, rel, copy, hash));
            }
            var absentPath = Path.Combine(snapshotDir, AbsentFile);
            if (File.Exists(absentPath))
            {
                foreach (var line in File.ReadAllLines(absentPath))
                {
                    int slash = line.IndexOf('/');
                    if (slash <= 0) continue;
                    if (!locations.TryGetValue(line.Substring(0, slash), out var loc)) throw new IOException("snapshot names an unknown location: " + line);
                    entries.Add(new SnapshotEntry(loc, line.Substring(slash + 1).Replace('/', Path.DirectorySeparatorChar), null, null));
                }
            }
            return entries;
        }

        /// <summary>
        /// Restores the forced keys (see the class summary). A file being rewritten is first kept in
        /// <c>replaced\</c> inside the snapshot. Throws on a corrupt snapshot; the caller keeps the marker.
        /// </summary>
        public static RestoreReport Restore(string snapshotDir, IEnumerable<string> forcedKeys)
        {
            var keys = forcedKeys.Concat(SessionKeysOf(snapshotDir)).Distinct(StringComparer.OrdinalIgnoreCase).ToList();
            var report = new RestoreReport();
            foreach (var entry in ReadVerified(snapshotDir))
            {
                var live = Path.Combine(entry.Location.Path, entry.RelativePath);
                var label = entry.Location.Name + "/" + entry.RelativePath.Replace('\\', '/');
                if (entry.WasAbsent)
                {
                    if (File.Exists(live)) RestoreCreatedFile(snapshotDir, entry, live, label, keys, report);
                    else report.Unchanged.Add(label + " (absent)");
                    continue;
                }
                if (!File.Exists(live) && entry.Location.Kind == SettingsLocationKind.SteamRemote)
                {
                    // A Steam-Cloud file (the profile) is never put back behind Steam's back: Steam's
                    // record would go stale and the game would reset the profile (T-115).
                    report.ChangedInCloud.Add(label + " (removed during the session)");
                    continue;
                }
                if (!File.Exists(live))
                {
                    FileUtil.CopyVerified(entry.CopyPath, live);
                    report.Restored.Add(label + " (file was missing; whole file restored)");
                    continue;
                }
                if (string.Equals(KnownBuilds.Sha256OfFile(live), entry.Sha256, StringComparison.OrdinalIgnoreCase))
                {
                    report.Unchanged.Add(label);
                    continue;
                }
                // Compared only, whatever its kind: a keyed config in a Steam Cloud folder is never rewritten either.
                if (entry.Location.Kind == SettingsLocationKind.SteamRemote)
                {
                    report.ChangedInCloud.Add(label);
                    continue;
                }
                if (!GameLayout.IsKeyedTextConfig(entry.RelativePath))
                {
                    report.ChangedNotRestored.Add(label);
                    continue;
                }

                var before = CvarConfig.ParseBytes(File.ReadAllBytes(entry.CopyPath), out _);
                RestoreKeys(snapshotDir, entry, live, label, before, keys, report, "changed by the player only");
            }
            File.WriteAllText(Path.Combine(snapshotDir, "restore.txt"), string.Join(Environment.NewLine, report.Lines) + Environment.NewLine);
            return report;
        }

        /// <summary>
        /// A config the game created during the session: for a text config every forced key is taken out
        /// (none was set before, as the file did not exist); any other file is reported and left.
        /// </summary>
        private static void RestoreCreatedFile(string snapshotDir, SnapshotEntry entry, string live, string label, List<string> keys, RestoreReport report)
        {
            if (entry.Location.Kind == SettingsLocationKind.SteamRemote)
            {
                report.ChangedInCloud.Add(label + " (created during the session)");
                return;
            }
            if (!GameLayout.IsKeyedTextConfig(entry.RelativePath))
            {
                report.ChangedNotRestored.Add(label + " (created during the session)");
                return;
            }
            RestoreKeys(snapshotDir, entry, live, label + " (created during the session)", CvarConfig.Parse(string.Empty), keys, report, "no forced key in it");
        }

        private static void RestoreKeys(string snapshotDir, SnapshotEntry entry, string live, string label, CvarConfig before,
                                        List<string> keys, RestoreReport report, string unchangedNote)
        {
            var current = CvarConfig.ParseBytes(File.ReadAllBytes(live), out var bom);
            var changes = ForcedKeyRestore.Apply(before, current, keys);
            if (changes.Count == 0)
            {
                report.Unchanged.Add(label + " (" + unchangedNote + ")");
                return;
            }
            var keep = Path.Combine(snapshotDir, "replaced", entry.Location.Name, entry.RelativePath);
            FileUtil.CopyVerified(live, keep);
            FileUtil.WriteAllBytesAtomic(live, current.ToBytes(bom));
            report.Restored.Add(label + ": " + string.Join("; ", changes.Select(c => c.ToString())));
        }

        /// <summary>The keys recorded with the snapshot for its own session (<see cref="Take"/>); empty when none were.</summary>
        public static IReadOnlyList<string> SessionKeysOf(string snapshotDir)
        {
            var path = Path.Combine(snapshotDir, KeysFile);
            if (!File.Exists(path)) return new string[0];
            return File.ReadAllLines(path).Select(l => l.Trim()).Where(l => l.Length > 0).ToList();
        }

        /// <summary>Deletes all but the newest <paramref name="keep"/> snapshots, never <paramref name="protect"/>.</summary>
        public static void Prune(string snapshotsRoot, int keep, string protect)
        {
            if (!Directory.Exists(snapshotsRoot)) return;
            var dirs = Directory.GetDirectories(snapshotsRoot).OrderByDescending(d => Path.GetFileName(d), StringComparer.Ordinal).ToList();
            foreach (var d in dirs.Skip(keep))
                if (!string.Equals(Path.GetFullPath(d), protect == null ? null : Path.GetFullPath(protect), StringComparison.OrdinalIgnoreCase))
                    FileUtil.DeleteDirectory(d);
        }

        /// <summary>
        /// The newest complete snapshot that is not a dry run's: the restore source when a session marker
        /// names none it can use (a marker damaged by a power loss).
        /// </summary>
        public static string NewestComplete(string snapshotsRoot)
        {
            if (!Directory.Exists(snapshotsRoot)) return null;
            return Directory.GetDirectories(snapshotsRoot)
                .Where(d => IsComplete(d) && !File.Exists(Path.Combine(d, DryRunFile)))
                .OrderByDescending(d => Path.GetFileName(d), StringComparer.Ordinal)
                .FirstOrDefault();
        }
    }

    public sealed class SnapshotEntry
    {
        public SnapshotEntry(SettingsLocation location, string relativePath, string copyPath, string sha256)
        {
            Location = location;
            RelativePath = relativePath;
            CopyPath = copyPath;
            Sha256 = sha256;
        }

        public SettingsLocation Location { get; }
        public string RelativePath { get; }
        /// <summary>The snapshot's copy; null for a file that was absent when the snapshot was taken.</summary>
        public string CopyPath { get; }
        public string Sha256 { get; }
        public bool WasAbsent => CopyPath == null;
    }
}
