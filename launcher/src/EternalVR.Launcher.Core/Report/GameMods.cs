using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;

namespace EternalVR.Launcher.Core.Report
{
    /// <summary>
    /// Other mods of the game for <see cref="ReportManifest.SystemFile"/>: the names in the game folder's <c>Mods</c> folder
    /// (never their contents) and any mod loader's files at the top of the game folder.
    /// </summary>
    public static class GameMods
    {
        public const string ModsKey = "game mods";
        public const string LoaderKey = "game mod loader";
        public const string ModsFolder = "Mods";

        /// <summary>The most names listed from <see cref="ModsFolder"/>; the rest are counted.</summary>
        public const int NamesListed = 30;

        /// <summary>
        /// The starts of the mod loaders' file names (EternalModInjector.bat, DEternal_loadMods.exe, DEternal_patchManifest.exe,
        /// idRehash.exe, EternalPatcher.exe, EternalModManager.exe), compared ignoring case.
        /// </summary>
        public static readonly IReadOnlyList<string> LoaderNames = new[]
        {
            "EternalModInjector", "EternalModManager", "EternalPatcher", "DEternal_", "idRehash",
        };

        /// <summary>
        /// The two lines for the game folder <paramref name="gameRoot"/>: the names in <c>Mods</c> (a folder's name ends in
        /// <c>\</c>), sorted, at most <see cref="NamesListed"/> and then how many more; and the mod loader files found.
        /// </summary>
        public static IReadOnlyList<KeyValuePair<string, string>> Describe(string gameRoot)
        {
            if (string.IsNullOrWhiteSpace(gameRoot) || !Directory.Exists(gameRoot))
                return new[] { Line(ModsKey, "game not found"), Line(LoaderKey, "game not found") };
            return new[] { Line(ModsKey, ModsText(Path.Combine(gameRoot, ModsFolder))), Line(LoaderKey, LoaderText(gameRoot)) };
        }

        private static string ModsText(string mods)
        {
            try
            {
                if (!Directory.Exists(mods)) return "no " + ModsFolder + " folder";
                var names = Directory.GetDirectories(mods).Select(d => Path.GetFileName(d) + "\\")
                    .Concat(Directory.GetFiles(mods).Select(Path.GetFileName))
                    .OrderBy(n => n, StringComparer.OrdinalIgnoreCase)
                    .ToList();
                if (names.Count == 0) return ModsFolder + " folder empty";
                var text = names.Count.ToString(CultureInfo.InvariantCulture) + " in " + ModsFolder + ": " + string.Join(", ", names.Take(NamesListed));
                if (names.Count > NamesListed) text += ", and " + (names.Count - NamesListed).ToString(CultureInfo.InvariantCulture) + " more";
                return text;
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException)
            {
                return ModsFolder + " folder could not be read (" + e.GetType().Name + ")";
            }
        }

        private static string LoaderText(string gameRoot)
        {
            try
            {
                var found = Directory.GetFiles(gameRoot).Select(Path.GetFileName)
                    .Where(n => LoaderNames.Any(l => n.StartsWith(l, StringComparison.OrdinalIgnoreCase)))
                    .OrderBy(n => n, StringComparer.OrdinalIgnoreCase)
                    .ToList();
                return found.Count == 0 ? "none found" : string.Join(", ", found);
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException)
            {
                return "the game folder could not be read (" + e.GetType().Name + ")";
            }
        }

        private static KeyValuePair<string, string> Line(string key, string value) => new KeyValuePair<string, string>(key, value);
    }
}
