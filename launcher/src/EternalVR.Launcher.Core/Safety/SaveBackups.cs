using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using EternalVR.Launcher.Core.Game;

namespace EternalVR.Launcher.Core.Safety
{
    /// <summary>
    /// Save-slot backups (T-092): before each VR launch every save-slot folder of every Steam settings
    /// location is copied into <c>save-backups\&lt;timestamp&gt;\&lt;location&gt;\&lt;slot&gt;\</c> with a
    /// <c>SHA256SUMS</c> file; only the newest <see cref="Keep"/> backups are kept. "Restore saves" copies
    /// a verified backup back, after first backing up the current saves under <c>pre-restore-*</c> (the newest
    /// <see cref="Keep"/> of those are kept too). The save slots are Steam-Cloud files: callers go through
    /// <see cref="SaveRestore"/>, which keeps Steam's record consistent (T-115). A Game Pass location (its <c>wgs</c> save containers) is copied whole into the
    /// backup and marked in <c>locations.txt</c>; such a backup is a copy only and is never restored.
    /// </summary>
    public static class SaveBackups
    {
        public const int Keep = 5;
        private const string SumsFile = "SHA256SUMS";
        private const string LocationsFile = "locations.txt";
        private const string PreRestorePrefix = "pre-restore-";
        /// <summary>The third field of a Game Pass line in <c>locations.txt</c>.</summary>
        private const string GamePassMark = "gamepass";

        /// <summary>
        /// Copies the save slots into a new backup folder. Returns null, leaving nothing behind, when
        /// there is no save file to back up: an empty backup would count as one of the <see cref="Keep"/>
        /// and push real backups out of the rotation.
        /// </summary>
        public static string Create(string backupsRoot, string name, IEnumerable<SettingsLocation> locations)
        {
            var dir = Path.Combine(backupsRoot, name);
            if (Directory.Exists(dir)) throw new IOException("save backup already exists: " + dir);
            Directory.CreateDirectory(dir);
            int files = 0;
            var sums = new StringBuilder();
            var locs = new StringBuilder();
            foreach (var loc in locations.Where(l => l.Kind == SettingsLocationKind.SteamRemote || l.Kind == SettingsLocationKind.GamePassSaves))
            {
                locs.Append(loc.Name).Append('|').Append(loc.Path);
                if (loc.Kind == SettingsLocationKind.GamePassSaves) locs.Append('|').Append(GamePassMark);
                locs.Append('\n');
                foreach (var slot in SaveFolders(loc))
                {
                    foreach (var file in Directory.GetFiles(slot, "*", SearchOption.AllDirectories))
                    {
                        var rel = file.Substring(loc.Path.Length).TrimStart('\\', '/');
                        var target = Path.Combine(dir, loc.Name, rel);
                        FileUtil.CopyVerified(file, target);
                        files++;
                        sums.Append(KnownBuilds.Sha256OfFile(target)).Append("  ").Append(loc.Name).Append('/').Append(rel.Replace('\\', '/')).Append('\n');
                    }
                }
            }
            if (files == 0)
            {
                FileUtil.DeleteDirectory(dir);
                return null;
            }
            File.WriteAllText(Path.Combine(dir, LocationsFile), locs.ToString());
            // The sums file marks the backup complete, so it is written last.
            File.WriteAllText(Path.Combine(dir, SumsFile), sums.ToString());
            return dir;
        }

        /// <summary>The folders backed up from a location: a Steam location's save slots, or the whole Game Pass container folder.</summary>
        private static IReadOnlyList<string> SaveFolders(SettingsLocation loc)
        {
            if (loc.Kind != SettingsLocationKind.GamePassSaves) return GameLayout.SaveSlotFolders(loc.Path);
            return Directory.Exists(loc.Path) ? new[] { loc.Path } : new string[0];
        }

        /// <summary>Whether a backup holds Game Pass saves (it is kept as a copy only, <see cref="Restore"/> refuses it).</summary>
        public static bool HoldsGamePassSaves(string backupDir) =>
            LocationsOf(backupDir).Any(l => l.Kind == SettingsLocationKind.GamePassSaves);

        /// <summary>Rotating backups (not pre-restore ones), newest first.</summary>
        public static IReadOnlyList<string> List(string backupsRoot)
        {
            if (!Directory.Exists(backupsRoot)) return new string[0];
            return Directory.GetDirectories(backupsRoot)
                .Where(d => !Path.GetFileName(d).StartsWith(PreRestorePrefix, StringComparison.OrdinalIgnoreCase))
                .Where(d => File.Exists(Path.Combine(d, SumsFile)))
                .OrderByDescending(d => Path.GetFileName(d), StringComparer.Ordinal)
                .ToList();
        }

        /// <summary>
        /// The complete <c>pre-restore-*</c> backups "Restore saves" made of the saves it replaced, newest first (one left
        /// half copied by a crash has no sums and does not count).
        /// </summary>
        public static IReadOnlyList<string> PreRestoreBackups(string backupsRoot)
        {
            if (!Directory.Exists(backupsRoot)) return new string[0];
            return PreRestoreFolders(backupsRoot)
                .Where(d => File.Exists(Path.Combine(d, SumsFile)))
                .OrderByDescending(d => Path.GetFileName(d), StringComparer.Ordinal)
                .ToList();
        }

        private static IEnumerable<string> PreRestoreFolders(string backupsRoot) =>
            Directory.GetDirectories(backupsRoot).Where(d => Path.GetFileName(d).StartsWith(PreRestorePrefix, StringComparison.OrdinalIgnoreCase));

        /// <summary>
        /// Deletes rotating backups beyond the newest <see cref="Keep"/>, <c>pre-restore-*</c> backups beyond the newest
        /// <see cref="Keep"/>, and incomplete ones of either kind.
        /// </summary>
        public static void Rotate(string backupsRoot)
        {
            if (!Directory.Exists(backupsRoot)) return;
            foreach (var d in Directory.GetDirectories(backupsRoot))
            {
                var name = Path.GetFileName(d);
                if (!name.StartsWith(PreRestorePrefix, StringComparison.OrdinalIgnoreCase) && !File.Exists(Path.Combine(d, SumsFile)))
                    FileUtil.DeleteDirectory(d);
            }
            foreach (var d in List(backupsRoot).Skip(Keep)) FileUtil.DeleteDirectory(d);
            PrunePreRestore(backupsRoot);
        }

        /// <summary>Incomplete <c>pre-restore-*</c> folders, and complete ones beyond the newest <see cref="Keep"/>.</summary>
        private static void PrunePreRestore(string backupsRoot)
        {
            if (!Directory.Exists(backupsRoot)) return;
            foreach (var d in PreRestoreFolders(backupsRoot).Where(d => !File.Exists(Path.Combine(d, SumsFile))).ToList())
                FileUtil.DeleteDirectory(d);
            foreach (var d in PreRestoreBackups(backupsRoot).Skip(Keep)) FileUtil.DeleteDirectory(d);
        }

        /// <summary>Checks every file of a backup against its sums. Returns the problems found (none = valid).</summary>
        public static IReadOnlyList<string> Verify(string backupDir)
        {
            var problems = new List<string>();
            var sums = Path.Combine(backupDir, SumsFile);
            if (!File.Exists(sums)) { problems.Add("no " + SumsFile); return problems; }
            foreach (var entry in ReadSums(backupDir))
            {
                // A Game Pass container's names can take a copy past Windows' classic path limit (FileUtil.Long).
                var path = FileUtil.Long(Path.Combine(backupDir, entry.Value.Replace('/', Path.DirectorySeparatorChar)));
                if (!File.Exists(path)) problems.Add("missing " + entry.Value);
                else if (!string.Equals(KnownBuilds.Sha256OfFile(path), entry.Key, StringComparison.OrdinalIgnoreCase)) problems.Add("checksum mismatch " + entry.Value);
            }
            return problems;
        }

        /// <summary>
        /// Restores a backup into the Steam locations it was taken from. The slots the backup covers are
        /// replaced as a whole (files not in the backup are removed from those slots); other slots are not
        /// touched. The current saves are backed up first. Returns the pre-restore backup folder, or null
        /// when there were no current saves to back up.
        /// </summary>
        public static string Restore(string backupsRoot, string backupDir, DateTime now)
        {
            var problems = Verify(backupDir);
            if (problems.Count > 0) throw new IOException("backup does not verify: " + string.Join("; ", problems));

            var locations = LocationsOf(backupDir);
            if (locations.Any(l => l.Kind == SettingsLocationKind.GamePassSaves))
                throw new IOException("the backup holds Game Pass saves, which the launcher keeps as a copy only: " + backupDir);

            // Every entry is checked before anything is changed: its location must be listed and its
            // target must stay inside that location.
            var entries = ReadSums(backupDir).ToList();
            var copies = new List<KeyValuePair<string, string>>();
            foreach (var e in entries)
            {
                int slash = e.Value.IndexOf('/');
                var loc = slash > 0 ? locations.FirstOrDefault(l => string.Equals(l.Name, e.Value.Substring(0, slash), StringComparison.OrdinalIgnoreCase)) : null;
                if (loc == null) throw new IOException("the backup names a location it does not list: " + e.Value);
                var root = Path.GetFullPath(loc.Path).TrimEnd('\\', '/') + Path.DirectorySeparatorChar;
                var target = Path.GetFullPath(Path.Combine(loc.Path, e.Value.Substring(slash + 1).Replace('/', Path.DirectorySeparatorChar)));
                if (!target.StartsWith(root, StringComparison.OrdinalIgnoreCase)) throw new IOException("the backup names a path outside its location: " + e.Value);
                copies.Add(new KeyValuePair<string, string>(Path.Combine(backupDir, e.Value.Replace('/', Path.DirectorySeparatorChar)), target));
            }

            var pre = Create(backupsRoot, PreRestorePrefix + DataPaths.NewSessionId(now), locations);

            var slotsCovered = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var e in entries)
            {
                var parts = e.Value.Split('/');
                if (parts.Length >= 3) slotsCovered.Add(parts[0] + "/" + parts[1]);
            }
            foreach (var loc in locations)
            {
                foreach (var slot in GameLayout.SaveSlotFolders(loc.Path))
                {
                    if (!slotsCovered.Contains(loc.Name + "/" + Path.GetFileName(slot))) continue;
                    foreach (var file in Directory.GetFiles(slot, "*", SearchOption.AllDirectories))
                    {
                        var rel = loc.Name + "/" + file.Substring(loc.Path.Length).TrimStart('\\', '/').Replace('\\', '/');
                        if (!entries.Any(x => string.Equals(x.Value, rel, StringComparison.OrdinalIgnoreCase))) File.Delete(file);
                    }
                }
            }
            foreach (var c in copies) FileUtil.CopyVerified(c.Key, c.Value);
            // The saves are back: an old pre-restore backup that cannot be removed now goes at the next rotation.
            try { PrunePreRestore(backupsRoot); }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException) { }
            return pre;
        }

        /// <summary>The locations a backup was taken from (its <c>locations.txt</c>): Steam ones, and a marked Game Pass one.</summary>
        public static IReadOnlyList<SettingsLocation> LocationsOf(string backupDir)
        {
            var locations = new List<SettingsLocation>();
            foreach (var line in File.ReadAllLines(Path.Combine(backupDir, LocationsFile)))
            {
                var parts = line.Split('|');
                if (parts.Length < 2) continue;
                var kind = parts.Length >= 3 && parts[2] == GamePassMark ? SettingsLocationKind.GamePassSaves : SettingsLocationKind.SteamRemote;
                locations.Add(new SettingsLocation(parts[0], kind, parts[1]));
            }
            return locations;
        }

        private static IEnumerable<KeyValuePair<string, string>> ReadSums(string backupDir)
        {
            foreach (var line in File.ReadAllLines(Path.Combine(backupDir, SumsFile)))
                if (line.Length > 66) yield return new KeyValuePair<string, string>(line.Substring(0, 64), line.Substring(66));
        }
    }
}
