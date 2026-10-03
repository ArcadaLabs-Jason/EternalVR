using System;
using System.Collections.Generic;
using System.Text;

namespace EternalVR.Launcher.Core.Settings
{
    public sealed partial class LauncherSettings
    {
        /// <summary>
        /// <paramref name="mine"/> (the launcher's settings to save) with the hand edits made to the file on disk since the
        /// launcher last read or wrote it (<paramref name="written"/>): a key whose value on disk differs from
        /// <paramref name="written"/> takes the disk's value unless the launcher changed that key too; a key only on disk is
        /// added. Without this, editing launcher.ini while the launcher is open was lost at its next save.
        /// </summary>
        public static string MergeHandEdits(string written, string mine, string disk)
        {
            var before = ReadMap(written);
            var onDisk = ReadMap(disk);
            var ours = ReadMap(mine);
            var sb = new StringBuilder();
            foreach (var raw in (mine ?? string.Empty).Replace("\r\n", "\n").Split('\n'))
            {
                var key = KeyOf(raw);
                if (key != null && onDisk.TryGetValue(key, out var d) && !Same(d, Get(before, key)) && Same(ours[key], Get(before, key)))
                    sb.Append(key).Append(" = ").Append(d).Append('\n');
                else
                    sb.Append(raw).Append('\n');
            }
            foreach (var kv in onDisk)
                if (!ours.ContainsKey(kv.Key) && !before.ContainsKey(kv.Key)) sb.Append(kv.Key).Append(" = ").Append(kv.Value).Append('\n');
            return sb.ToString().TrimEnd('\n') + "\n";
        }

        private static Dictionary<string, string> ReadMap(string text)
        {
            var map = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            foreach (var raw in (text ?? string.Empty).Replace("\r\n", "\n").Split('\n'))
            {
                var key = KeyOf(raw);
                if (key != null) map[key] = raw.Substring(raw.IndexOf('=') + 1).Trim();
            }
            return map;
        }

        private static string KeyOf(string raw)
        {
            var line = raw.Trim();
            if (line.Length == 0 || line.StartsWith("#", StringComparison.Ordinal) || line.StartsWith(";", StringComparison.Ordinal)) return null;
            int eq = line.IndexOf('=');
            return eq <= 0 ? null : line.Substring(0, eq).Trim();
        }

        private static string Get(Dictionary<string, string> map, string key) => map.TryGetValue(key, out var v) ? v : null;

        private static bool Same(string a, string b) => string.Equals(a?.Trim(), b?.Trim(), StringComparison.Ordinal);
    }
}
