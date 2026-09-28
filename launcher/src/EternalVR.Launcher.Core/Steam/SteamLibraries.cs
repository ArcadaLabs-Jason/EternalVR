using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using EternalVR.Launcher.Core.Text;

namespace EternalVR.Launcher.Core.Steam
{
    /// <summary>Where the game was found and how.</summary>
    public sealed class GameInstallLocation
    {
        public GameInstallLocation(string gameRoot, string libraryRoot, string buildId, string source)
        {
            GameRoot = gameRoot;
            LibraryRoot = libraryRoot;
            BuildId = buildId;
            Source = source;
        }

        public string GameRoot { get; }
        public string LibraryRoot { get; }
        /// <summary>Steam's build ID from the app manifest; null when unknown.</summary>
        public string BuildId { get; }
        public string Source { get; }
    }

    /// <summary>Steam library discovery through <c>steamapps\libraryfolders.vdf</c> and app manifests.</summary>
    public static class SteamLibraries
    {
        /// <summary>
        /// Library roots listed in <c>libraryfolders.vdf</c> (both the current block format and the old
        /// "1" = "path" format), with the Steam root itself first. Duplicates are removed.
        /// </summary>
        public static IReadOnlyList<string> ParseLibraryFolders(string vdfText, string steamRoot)
        {
            var result = new List<string>();
            void AddPath(string p)
            {
                if (string.IsNullOrWhiteSpace(p)) return;
                var norm = NormalizeDir(p);
                if (!result.Any(r => string.Equals(r, norm, StringComparison.OrdinalIgnoreCase))) result.Add(norm);
            }

            if (!string.IsNullOrEmpty(steamRoot)) AddPath(steamRoot);
            var root = VdfParser.Parse(vdfText);
            var folders = root["libraryfolders"] ?? root["LibraryFolders"];
            if (folders == null) return result;
            foreach (var entry in folders.Children)
            {
                if (!IsIndexKey(entry.Key)) continue;
                if (entry.Value.Value != null) AddPath(entry.Value.Value);          // old format
                else AddPath(entry.Value.GetString("path"));                        // current format
            }
            return result;
        }

        /// <summary>Library roots whose "apps" block lists the app ID (the block can be stale).</summary>
        public static IReadOnlyList<string> LibrariesListingApp(string vdfText, string appId)
        {
            var result = new List<string>();
            var folders = VdfParser.Parse(vdfText)["libraryfolders"];
            if (folders == null) return result;
            foreach (var entry in folders.Children)
            {
                var path = entry.Value.GetString("path");
                if (path != null && entry.Value["apps"]?[appId] != null) result.Add(NormalizeDir(path));
            }
            return result;
        }

        /// <summary>
        /// Finds an installed app: every library's <c>appmanifest_&lt;id&gt;.acf</c> is checked (the
        /// libraryfolders "apps" lists are not trusted alone, they go stale), and the install folder must
        /// contain <paramref name="exeName"/>. Returns null when not found.
        /// </summary>
        public static GameInstallLocation FindApp(string steamRoot, string appId, string exeName)
        {
            if (string.IsNullOrEmpty(steamRoot)) return null;
            var vdfPath = Path.Combine(steamRoot, "steamapps", "libraryfolders.vdf");
            IReadOnlyList<string> libraries;
            try
            {
                libraries = File.Exists(vdfPath)
                    ? ParseLibraryFolders(File.ReadAllText(vdfPath), steamRoot)
                    : new[] { NormalizeDir(steamRoot) };
            }
            catch (VdfFormatException)
            {
                libraries = new[] { NormalizeDir(steamRoot) };
            }

            foreach (var library in libraries)
            {
                var found = FindInLibrary(library, appId, exeName);
                if (found != null) return found;
            }
            return null;
        }

        public static GameInstallLocation FindInLibrary(string libraryRoot, string appId, string exeName)
        {
            var manifest = Path.Combine(libraryRoot, "steamapps", "appmanifest_" + appId + ".acf");
            if (!File.Exists(manifest)) return null;
            VdfNode state;
            try { state = VdfParser.Parse(File.ReadAllText(manifest))["AppState"]; }
            catch (Exception e) when (e is VdfFormatException || e is IOException) { return null; }
            var installDir = state?.GetString("installdir");
            if (string.IsNullOrEmpty(installDir)) return null;
            var gameRoot = Path.Combine(libraryRoot, "steamapps", "common", installDir);
            if (!File.Exists(Path.Combine(gameRoot, exeName))) return null;
            return new GameInstallLocation(gameRoot, libraryRoot, state.GetString("buildid"),
                "appmanifest_" + appId + ".acf in " + libraryRoot);
        }

        /// <summary>
        /// The SteamVR OpenXR runtime manifest in each library, for the runtime list (T-110).
        /// </summary>
        public static IEnumerable<string> FindSteamVrRuntimes(IEnumerable<string> libraries)
        {
            foreach (var library in libraries)
            {
                var json = Path.Combine(library, "steamapps", "common", "SteamVR", "steamxr_win64.json");
                if (File.Exists(json)) yield return json;
            }
        }

        private static bool IsIndexKey(string key) => key.Length > 0 && key.All(char.IsDigit);

        private static string NormalizeDir(string path)
        {
            var p = path.Replace('/', Path.DirectorySeparatorChar).Trim();
            while (p.Length > 3 && (p.EndsWith("\\") || p.EndsWith("/"))) p = p.Substring(0, p.Length - 1);
            return p;
        }
    }
}
