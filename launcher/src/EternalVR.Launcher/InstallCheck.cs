using System;
using System.IO;

namespace EternalVR.Launcher
{
    /// <summary>
    /// The first check at start-up, before any type of the Core assembly is touched: a launcher run from inside the zip
    /// (Windows extracts only the clicked file to a temporary folder) or with files missing next to it cannot load its
    /// Core assembly or data, and would otherwise die with a .NET error dialog.
    /// </summary>
    internal static class InstallCheck
    {
        private const string CoreAssembly = "EternalVR.Launcher.Core.dll";
        private const string ExtractHelp =
            "Extract the whole EternalVR zip to a folder of your own first (right-click the zip > Extract All...), "
            + "then start EternalVR.Launcher.exe from that folder.";

        /// <summary>What is wrong with the program folder, or null when it looks complete.</summary>
        public static string Problem(string[] args)
        {
            var dir = AppDomain.CurrentDomain.BaseDirectory;
            if (IsUnder(dir, Path.GetTempPath()))
                return "EternalVR is running from a temporary folder (" + dir + "), which happens when it is started from inside the zip. " + ExtractHelp;

            var missing = new System.Collections.Generic.List<string>();
            if (!File.Exists(Path.Combine(dir, CoreAssembly))) missing.Add(CoreAssembly);
            if (!Directory.Exists(Path.Combine(dir, "data"))) missing.Add("data\\");
            // Only the window needs the bundled layer folder; command-line runs are for tests and tools and name their own.
            if (args.Length == 0 && !Directory.Exists(Path.Combine(dir, "layer")) && !HasLayerOverride(args)) missing.Add("layer\\");
            if (missing.Count == 0) return null;
            return "Files are missing next to the launcher in " + dir + ": " + string.Join(", ", missing) + ". " + ExtractHelp;
        }

        private static bool IsUnder(string path, string root)
        {
            if (string.IsNullOrEmpty(root)) return false;
            try
            {
                var full = Path.GetFullPath(path).TrimEnd('\\') + "\\";
                var r = Path.GetFullPath(root).TrimEnd('\\') + "\\";
                return full.StartsWith(r, StringComparison.OrdinalIgnoreCase);
            }
            catch (Exception e) when (e is ArgumentException || e is NotSupportedException || e is PathTooLongException) { return false; }
        }

        /// <summary>A layer folder given elsewhere: <c>--layer-dir</c>, or a <c>layer_dir</c> in the default data folder's launcher.ini.</summary>
        private static bool HasLayerOverride(string[] args)
        {
            foreach (var a in args)
                if (string.Equals(a, "--layer-dir", StringComparison.OrdinalIgnoreCase)) return true;
            try
            {
                var ini = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "EternalVR", "launcher.ini");
                if (!File.Exists(ini)) return false;
                foreach (var raw in File.ReadAllLines(ini))
                {
                    var line = raw.Trim();
                    int eq = line.IndexOf('=');
                    if (eq > 0 && string.Equals(line.Substring(0, eq).Trim(), "layer_dir", StringComparison.OrdinalIgnoreCase))
                        return line.Substring(eq + 1).Trim().Length > 0;
                }
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException) { }
            return false;
        }
    }
}
