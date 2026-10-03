using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;

namespace EternalVR.Launcher.Core.Report
{
    /// <summary>
    /// Windows' own crash and hang reports of the game, the launcher or the layer (<see cref="ReportSource.WindowsErrorReports"/>):
    /// the <c>Report.wer</c> text file in each <c>AppCrash_*</c> / <c>AppHang_*</c> folder of the <c>ReportArchive</c> and
    /// <c>ReportQueue</c> folders under a WER folder (<c>%ProgramData%\Microsoft\Windows\WER</c>, <c>%LOCALAPPDATA%\Microsoft\Windows\WER</c>).
    /// Each names the faulting module and offset and lists every module loaded at the time (an overlay or another Vulkan layer
    /// shows there), which the event log entry does not. Only the text is taken, never the memory dumps WER may keep beside it.
    /// </summary>
    public static class WindowsErrorReports
    {
        public const string ReportFile = "Report.wer";
        public const int Kept = 3;
        public const int Days = 7;

        /// <summary>The folders under a WER folder that hold report folders.</summary>
        public static readonly IReadOnlyList<string> StoreFolders = new[] { "ReportArchive", "ReportQueue" };

        /// <summary>WER names a report folder <c>AppCrash_&lt;the first 16 characters of the exe name&gt;_...</c>.</summary>
        private const int FolderNameChars = 16;

        private static readonly string[] Kinds = { "AppCrash_", "AppHang_" };

        /// <summary>
        /// The reports of <see cref="WindowsEvents.Programs"/> under <paramref name="werDirs"/> written at or after <paramref name="since"/>,
        /// newest first, at most <paramref name="max"/>; <paramref name="notTaken"/> counts the older or extra ones. A folder is taken
        /// when its name starts with a kind and a program's name as WER shortens it, and its report names that program.
        /// </summary>
        public static IReadOnlyList<string> Find(IEnumerable<string> werDirs, DateTime since, int max, out int notTaken)
        {
            var prefixes = Kinds.SelectMany(k => WindowsEvents.Programs.Select(p => k + FolderPrefix(p))).ToList();
            var all = new List<KeyValuePair<string, DateTime>>();
            foreach (var wer in (werDirs ?? Enumerable.Empty<string>()).Where(d => !string.IsNullOrEmpty(d)).Distinct(StringComparer.OrdinalIgnoreCase))
                foreach (var store in StoreFolders)
                    foreach (var dir in SafeDirectories(Path.Combine(wer, store)))
                    {
                        var name = Path.GetFileName(dir);
                        if (!prefixes.Any(p => name.StartsWith(p, StringComparison.OrdinalIgnoreCase))) continue;
                        var report = Path.Combine(dir, ReportFile);
                        if (!File.Exists(report)) continue;
                        all.Add(new KeyValuePair<string, DateTime>(report, LastWrite(report)));
                    }
            var taken = all.Where(r => r.Value >= since && NamesAProgram(Read(r.Key)))
                .OrderByDescending(r => r.Value)
                .ThenBy(r => r.Key, StringComparer.OrdinalIgnoreCase)
                .Take(Math.Max(0, max))
                .Select(r => r.Key)
                .ToList();
            notTaken = all.Count - taken.Count;
            return taken;
        }

        /// <summary><c>DOOMEternalx64vk.exe</c> to <c>DOOMEternalx64vk</c>, <c>EternalVR.Launcher.exe</c> to <c>EternalVR.Launch</c>.</summary>
        internal static string FolderPrefix(string program)
        {
            var name = program.EndsWith(".exe", StringComparison.OrdinalIgnoreCase) ? program.Substring(0, program.Length - 4) : program;
            return name.Length > FolderNameChars ? name.Substring(0, FolderNameChars) : name;
        }

        /// <summary>The report's <c>AppName</c>, <c>OriginalFilename</c> or faulting module is one of the programs.</summary>
        internal static bool NamesAProgram(string text)
        {
            if (string.IsNullOrEmpty(text)) return false;
            foreach (var line in text.Split('\n'))
            {
                int eq = line.IndexOf('=');
                if (eq < 0) continue;
                var key = line.Substring(0, eq);
                if (key != "AppName" && key != "OriginalFilename" && key != "NsAppName" && !key.StartsWith("Sig[", StringComparison.Ordinal)) continue;
                var value = line.Substring(eq + 1).Trim();
                if (WindowsEvents.Programs.Any(p => string.Equals(p, value, StringComparison.OrdinalIgnoreCase))) return true;
            }
            return false;
        }

        /// <summary>
        /// <c>windows-crashes/AppCrash-20261003-221952.txt</c>: the kind and the report's time; <paramref name="used"/> keeps two
        /// reports of the same second apart.
        /// </summary>
        public static string ZipPath(string reportPath, ISet<string> used)
        {
            var folder = Path.GetFileName(Path.GetDirectoryName(reportPath) ?? string.Empty);
            var kind = folder.StartsWith("AppHang_", StringComparison.OrdinalIgnoreCase) ? "AppHang" : "AppCrash";
            var stem = "windows-crashes/" + kind + "-" + LastWrite(reportPath).ToString("yyyyMMdd-HHmmss", CultureInfo.InvariantCulture);
            var path = stem + ".txt";
            for (int n = 2; used != null && !used.Add(path); n++) path = stem + "-" + n.ToString(CultureInfo.InvariantCulture) + ".txt";
            return path;
        }

        /// <summary>A report's text: WER writes UTF-16 with a byte order mark; other encodings are read by their mark, else as UTF-8.</summary>
        public static string Read(string path)
        {
            try
            {
                using (var s = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
                using (var r = new StreamReader(s, new UTF8Encoding(false), detectEncodingFromByteOrderMarks: true))
                    return r.ReadToEnd().Replace("\r\n", "\n");
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException) { return null; }
        }

        private static IEnumerable<string> SafeDirectories(string dir)
        {
            try { return Directory.Exists(dir) ? Directory.GetDirectories(dir) : new string[0]; }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException) { return new string[0]; }
        }

        private static DateTime LastWrite(string path)
        {
            try { return File.GetLastWriteTime(path); }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException) { return DateTime.MinValue; }
        }
    }
}
