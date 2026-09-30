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
            foreach (var dir in Sessions(logsDir).Skip(Math.Max(0, keep)))
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

        /// <summary>
        /// The OpenXR interaction profile of the controllers in the newest session whose layer logs name one (the layer logs
        /// <c>controllers: the runtime reports &lt;profile&gt; for the right hand</c>, and the right hand's picks the control
        /// map), or null. The controls editor opens on those controllers, so a player edits the file the game uses.
        /// </summary>
        public static string LastControllerProfile(string logsDir)
        {
            if (!Directory.Exists(logsDir)) return null;
            foreach (var dir in Sessions(logsDir))
            {
                string found = null;
                foreach (var file in Directory.GetFiles(dir, "eternalvr-*.log").OrderBy(f => f, StringComparer.Ordinal))
                {
                    try
                    {
                        // The game may still hold the file open for writing.
                        using (var stream = new FileStream(file, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
                        using (var reader = new StreamReader(stream))
                        {
                            string line;
                            while ((line = reader.ReadLine()) != null)
                            {
                                var m = RightHandProfile.Match(line);
                                if (m.Success) found = m.Groups[1].Value;
                            }
                        }
                    }
                    catch (Exception e) when (e is IOException || e is UnauthorizedAccessException) { }
                }
                if (found != null) return found;
            }
            return null;
        }

        private static readonly Regex RightHandProfile =
            new Regex(@"controllers: the runtime reports (/interaction_profiles/\S+) for the right hand", RegexOptions.CultureInvariant);

        /// <summary>The session folders, newest first.</summary>
        private static List<string> Sessions(string logsDir) =>
            Directory.GetDirectories(logsDir)
                .Where(d => SessionName.IsMatch(Path.GetFileName(d)))
                .OrderByDescending(d => Key(Path.GetFileName(d)), StringComparer.Ordinal)
                .ToList();

        /// <summary>Sorts <c>20260927-141500-10</c> after <c>20260927-141500-9</c>.</summary>
        private static string Key(string name)
        {
            var parts = name.Split('-');
            var n = parts.Length > 2 ? parts[2] : "0";
            return parts[0] + parts[1] + n.PadLeft(6, '0');
        }
    }
}
