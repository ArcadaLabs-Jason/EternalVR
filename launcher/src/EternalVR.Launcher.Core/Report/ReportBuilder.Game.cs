using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using EternalVR.Launcher.Core.Game;

namespace EternalVR.Launcher.Core.Report
{
    /// <summary>The game's own files in a report: its console log and crash reports from its Saved Games <c>base</c> folder.</summary>
    public static partial class ReportBuilder
    {
        /// <summary>The game's minidumps, next to its crash reports: counted in the report, never included.</summary>
        internal const string GameCrashDumpsFolder = "crash-dumps";

        /// <summary>The key of the game's video settings in system.txt (<see cref="LastGameSettings"/>).</summary>
        public const string GameSettingsKey = "game settings (last session)";

        /// <summary>The layer's line with the game's video settings (src/vkcore/game_settings.hpp), after its time and thread.</summary>
        private const string GameSettingsMarker = "] game settings: ";

        /// <summary><c>Crash.&lt;computer&gt;.&lt;number&gt;.html</c>: the number is kept, the computer name never goes into the zip's file names.</summary>
        private static readonly Regex GameCrashName = new Regex(@"^Crash\..+\.(\d+)\.html$", RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);

        /// <summary>The <c>base</c> folders of the game's Saved Games folders that exist, each once.</summary>
        public static IReadOnlyList<string> GameBaseFolders(IEnumerable<string> savedGamesDirs) =>
            (savedGamesDirs ?? Enumerable.Empty<string>())
                .Where(d => !string.IsNullOrWhiteSpace(d))
                .Select(d => Path.Combine(d, GameLayout.SavedGamesBaseFolder))
                .Distinct(StringComparer.OrdinalIgnoreCase)
                .Where(Directory.Exists)
                .ToList();

        /// <summary>
        /// Where the game's crash reports are taken from: the start of the oldest session in the report (the newest
        /// <see cref="ReportManifest.SessionsKept"/>) or <see cref="ReportManifest.GameCrashDays"/> days before <paramref name="now"/>,
        /// whichever is earlier.
        /// </summary>
        public static DateTime CrashWindowStart(IReadOnlyList<string> sessionsNewestFirst, DateTime now)
        {
            var since = now.AddDays(-ReportManifest.GameCrashDays);
            var oldest = (sessionsNewestFirst ?? new string[0]).Take(ReportManifest.SessionsKept).LastOrDefault();
            if (oldest != null && oldest.Length >= 15
                && DateTime.TryParseExact(oldest.Substring(0, 15), "yyyyMMdd-HHmmss", CultureInfo.InvariantCulture, DateTimeStyles.None, out var start)
                && start < since)
                since = start;
            return since;
        }

        /// <summary>
        /// The game's crash reports (<c>Crash.&lt;computer&gt;.&lt;number&gt;.html</c>) in <paramref name="baseDirs"/> last written at or
        /// after <paramref name="since"/>, newest first, at most <paramref name="max"/>; <paramref name="notTaken"/> counts the others.
        /// Only the folders themselves are searched, never <c>crash-dumps</c>.
        /// </summary>
        public static IReadOnlyList<string> GameCrashReports(IEnumerable<string> baseDirs, DateTime since, int max, out int notTaken)
        {
            var all = (baseDirs ?? Enumerable.Empty<string>())
                .SelectMany(d => SafeFiles(d, "Crash.*.html"))
                .Where(p => GameCrashName.IsMatch(Path.GetFileName(p)))
                .Select(p => new { Path = p, Time = LastWrite(p) })
                .ToList();
            var taken = all.Where(f => f.Time >= since)
                .OrderByDescending(f => f.Time)
                .ThenByDescending(f => Path.GetFileName(f.Path), StringComparer.OrdinalIgnoreCase)
                .Take(Math.Max(0, max))
                .Select(f => f.Path)
                .ToList();
            notTaken = all.Count - taken.Count;
            return taken;
        }

        /// <summary><c>Crash.DESKTOP-7Q2XK9M.00014.html</c> to <c>game-crashes/crash-00014.html</c>; null for another name.</summary>
        public static string GameCrashZipPath(string fileName)
        {
            var m = GameCrashName.Match(fileName ?? string.Empty);
            if (!m.Success) return null;
            var item = ReportManifest.Items.First(i => i.Source == ReportSource.GameCrashes);
            return item.ZipPath.Replace("{number}", m.Groups[1].Value);
        }

        /// <summary>
        /// The game's video settings as the layer last logged them in <paramref name="sessionDir"/> (its <c>eternalvr-*.log</c> files,
        /// the last written last): the text of the last <c>game settings:</c> line (<c>r_enableRayTracing 1, ..., g_fov 110</c>), or
        /// null when there is none. A log still open for writing is read too.
        /// </summary>
        public static string LastGameSettings(string sessionDir)
        {
            string last = null;
            foreach (var path in SafeFiles(sessionDir, "eternalvr-*.log").OrderBy(LastWrite))
            {
                try
                {
                    using (var s = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
                    using (var r = new StreamReader(s, Utf8))
                    {
                        string line;
                        while ((line = r.ReadLine()) != null)
                        {
                            int at = line.IndexOf(GameSettingsMarker, StringComparison.Ordinal);
                            if (at >= 0) last = line.Substring(at + GameSettingsMarker.Length).Trim();
                        }
                    }
                }
                catch (Exception e) when (e is IOException || e is UnauthorizedAccessException) { }
            }
            return string.IsNullOrEmpty(last) ? null : last;
        }

        /// <summary>The newest of the files that exist, or null.</summary>
        private static string NewestFile(IEnumerable<string> paths) =>
            paths.Where(File.Exists).OrderByDescending(LastWrite).FirstOrDefault();

        /// <summary>The files matching <paramref name="pattern"/> in a folder and its subfolders (the game keeps its dumps in one per build); 0 when unreadable.</summary>
        private static int CountFilesBelow(string dir, string pattern)
        {
            try { return Directory.Exists(dir) ? Directory.GetFiles(dir, pattern, SearchOption.AllDirectories).Length : 0; }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException) { return 0; }
        }

        private static DateTime LastWrite(string path)
        {
            try { return File.GetLastWriteTime(path); }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException) { return DateTime.MinValue; }
        }

        private static void AddGameCrashes(List<ReportFile> files, List<string> dropped, List<string> missing, IReadOnlyList<string> baseDirs, DateTime since, ReportItem item)
        {
            var taken = GameCrashReports(baseDirs, since, ReportManifest.GameCrashesKept, out var notTaken);
            if (taken.Count == 0) missing.Add(item.ZipPath.Replace("{number}", "*"));
            var used = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var path in taken)
            {
                var zipPath = GameCrashZipPath(Path.GetFileName(path));
                for (int n = 2; !used.Add(zipPath); n++) // the same number in two folders
                    zipPath = GameCrashZipPath(Path.GetFileName(path)).Replace(".html", "-" + n.ToString(CultureInfo.InvariantCulture) + ".html");
                AddFile(files, dropped, path, item, zipPath);
            }
            if (notTaken > 0)
                dropped.Add($"{notTaken} older game crash report(s) (only those since {since.ToString("yyyy-MM-dd HH:mm", CultureInfo.InvariantCulture)} are taken, at most {ReportManifest.GameCrashesKept})");
        }
    }
}
