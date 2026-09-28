using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;

namespace EternalVR.Launcher.Core
{
    /// <summary>The per-session log folders under <c>logs\</c> (<c>yyyyMMdd-HHmmss[-n]</c>): only the newest are kept.</summary>
    public static class SessionLogs
    {
        public const int Kept = 20;
        private static readonly Regex SessionName = new Regex(@"^\d{8}-\d{6}(-\d+)?$", RegexOptions.CultureInvariant);

        /// <summary>
        /// Deletes the oldest session folders beyond <paramref name="keep"/> (never <paramref name="current"/>, and nothing that is not a
        /// session folder, such as launcher.log). Returns the folders deleted; a folder that cannot be deleted is skipped.
        /// </summary>
        public static IReadOnlyList<string> Prune(string logsDir, int keep, string current)
        {
            var deleted = new List<string>();
            if (!Directory.Exists(logsDir)) return deleted;
            var sessions = Directory.GetDirectories(logsDir)
                .Where(d => SessionName.IsMatch(Path.GetFileName(d)))
                .OrderByDescending(d => Key(Path.GetFileName(d)), StringComparer.Ordinal)
                .ToList();
            foreach (var dir in sessions.Skip(Math.Max(0, keep)))
            {
                if (current != null && string.Equals(Path.GetFullPath(dir), Path.GetFullPath(current), StringComparison.OrdinalIgnoreCase)) continue;
                try
                {
                    FileUtil.DeleteDirectory(dir);
                    deleted.Add(dir);
                }
                catch (Exception e) when (e is IOException || e is UnauthorizedAccessException) { }
            }
            return deleted;
        }

        /// <summary>Sorts <c>20260927-141500-10</c> after <c>20260927-141500-9</c>.</summary>
        private static string Key(string name)
        {
            var parts = name.Split('-');
            var n = parts.Length > 2 ? parts[2] : "0";
            return parts[0] + parts[1] + n.PadLeft(6, '0');
        }
    }
}
