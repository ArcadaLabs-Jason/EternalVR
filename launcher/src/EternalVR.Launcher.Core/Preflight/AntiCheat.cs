using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using EternalVR.Launcher.Core.Data;

namespace EternalVR.Launcher.Core.Preflight
{
    /// <summary>
    /// Anti-cheat tripwire, folder part (T-109): anti-cheat files or folders inside the game folder refuse
    /// VR. Only the game root and its first-level folders are looked at; Steam's metadata and other
    /// games' system-wide installs do not count. There is no in-process module check (ARCHITECTURE 4a).
    /// </summary>
    public sealed class AntiCheat
    {
        private readonly List<KeyValuePair<string, string>> prefixes;

        public AntiCheat(IEnumerable<KeyValuePair<string, string>> prefixes) { this.prefixes = prefixes.ToList(); }

        public static AntiCheat Parse(string text) =>
            new AntiCheat(DataFile.ParseRecords(text).Select(r => new KeyValuePair<string, string>(DataFile.Field(r, 0), DataFile.Field(r, 1))));

        public IReadOnlyList<string> Scan(string gameRoot)
        {
            var found = new List<string>();
            if (string.IsNullOrEmpty(gameRoot) || !Directory.Exists(gameRoot)) return found;
            var entries = new List<string>(Directory.GetFileSystemEntries(gameRoot));
            foreach (var dir in Directory.GetDirectories(gameRoot))
            {
                try { entries.AddRange(Directory.GetFileSystemEntries(dir)); }
                catch (UnauthorizedAccessException) { }
                catch (IOException) { }
            }
            foreach (var entry in entries)
            {
                var name = Path.GetFileName(entry);
                var hit = prefixes.FirstOrDefault(p => name.StartsWith(p.Key, StringComparison.OrdinalIgnoreCase));
                if (hit.Key != null) found.Add($"{entry.Substring(gameRoot.Length).TrimStart('\\', '/')} ({hit.Value})");
            }
            return found;
        }
    }
}
