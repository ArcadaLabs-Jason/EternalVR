using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Xml;
using System.Xml.Linq;
using EternalVR.Launcher.Core.Steam;

namespace EternalVR.Launcher.Core.Game
{
    /// <summary>Where the installed game came from.</summary>
    public enum GamePlatform { Steam, GamePass }

    /// <summary>The <c>Identity</c> element of a store package's <c>MicrosoftGame.Config</c>.</summary>
    public sealed class StoreIdentity
    {
        public StoreIdentity(string name, string publisher, string version)
        {
            Name = name;
            Publisher = publisher;
            Version = version;
        }

        public string Name { get; }
        public string Publisher { get; }
        public string Version { get; }
    }

    /// <summary>
    /// The Game Pass (Microsoft Store) install. Gaming Services puts a game under
    /// <c>&lt;drive&gt;\&lt;folder&gt;\&lt;name&gt;\Content</c>, where each drive it uses has a hidden <c>.GamingRoot</c>
    /// file naming the folder (<c>XboxGames</c> by default). The exe there cannot be opened for reading, so the
    /// build is told by the package identity in <c>Content\MicrosoftGame.Config</c>, not by a hash. Saves go
    /// through XGameSave into the package's <c>wgs</c> containers under <c>%LOCALAPPDATA%\Packages</c>; the text
    /// configs are shared with the Steam build (Saved Games).
    /// </summary>
    public static class GamePassInstall
    {
        public const string PackageName = "BethesdaSoftworks.DOOMEternal-PC";
        public const string ConfigFile = "MicrosoftGame.Config";
        public const string ContentFolder = "Content";
        public const string GamingRootFile = ".GamingRoot";
        public const string DefaultGamingFolder = "XboxGames";

        /// <summary>The backup location name of the save containers.</summary>
        public const string SaveLocationName = "gamepass";

        private static readonly byte[] GamingRootMagic = Encoding.ASCII.GetBytes("RGBX");

        /// <summary>
        /// The folders a <c>.GamingRoot</c> file names, relative to its drive: the magic <c>RGBX</c>, a 32-bit
        /// little-endian count, then that many NUL-terminated UTF-16LE paths. Empty when the data is not one.
        /// </summary>
        public static IReadOnlyList<string> ParseGamingRoot(byte[] data)
        {
            if (data == null || data.Length < 8 || !data.Take(4).SequenceEqual(GamingRootMagic)) return new string[0];
            int count = data[4] | data[5] << 8 | data[6] << 16 | data[7] << 24;
            if (count <= 0) return new string[0];
            return Encoding.Unicode.GetString(data, 8, (data.Length - 8) & ~1)
                .Split('\0')
                .Select(p => p.Trim().TrimStart('\\', '/'))
                .Where(p => p.Length > 0)
                .Take(count)
                .ToList();
        }

        /// <summary>The identity of a <c>MicrosoftGame.Config</c> text; null when it has none or is not XML.</summary>
        public static StoreIdentity ParseIdentity(string configXml)
        {
            XDocument doc;
            try { doc = XDocument.Parse(configXml ?? string.Empty); }
            catch (XmlException) { return null; }
            var e = doc.Root?.Elements().FirstOrDefault(x => x.Name.LocalName == "Identity");
            var name = (string)e?.Attribute("Name");
            if (string.IsNullOrWhiteSpace(name)) return null;
            return new StoreIdentity(name.Trim(), ((string)e.Attribute("Publisher"))?.Trim(), ((string)e.Attribute("Version"))?.Trim());
        }

        /// <summary>The identity in a Content folder's <c>MicrosoftGame.Config</c>; null when missing or unreadable.</summary>
        public static StoreIdentity ReadIdentity(string contentDir)
        {
            if (string.IsNullOrEmpty(contentDir)) return null;
            try
            {
                var path = Path.Combine(contentDir, ConfigFile);
                return File.Exists(path) ? ParseIdentity(File.ReadAllText(path)) : null;
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException || e is ArgumentException) { return null; }
        }

        public static bool IsDoomEternal(StoreIdentity identity) =>
            identity != null && string.Equals(identity.Name, PackageName, StringComparison.OrdinalIgnoreCase);

        /// <summary>The Game Pass DOOM Eternal Content folder for a folder the player chose: itself or its <c>Content</c>; null when neither.</summary>
        public static string ContentFolderOf(string dir)
        {
            if (string.IsNullOrEmpty(dir)) return null;
            if (IsDoomEternal(ReadIdentity(dir))) return dir;
            var content = Path.Combine(dir, ContentFolder);
            return IsDoomEternal(ReadIdentity(content)) ? content : null;
        }

        /// <summary>
        /// A path in the package store (<c>Program Files\WindowsApps</c>). The package's install location may still point
        /// there after the game was moved to an XboxGames folder; the Content folder on the drive is the one to start.
        /// </summary>
        public static bool IsPackageStorePath(string path) =>
            !string.IsNullOrEmpty(path) && path.Split('\\', '/').Any(p => string.Equals(p, "WindowsApps", StringComparison.OrdinalIgnoreCase));

        /// <summary>The install found in a Content folder (the package version as its build).</summary>
        public static GameInstallLocation Locate(string contentDir, string source) =>
            new GameInstallLocation(contentDir, null, ReadIdentity(contentDir)?.Version, source, GamePlatform.GamePass);

        /// <summary>
        /// Finds the Game Pass DOOM Eternal on the given drive roots: each drive's <c>.GamingRoot</c> folders and
        /// the default <c>XboxGames</c>, every game folder's <c>Content</c>. Null when not found.
        /// </summary>
        public static GameInstallLocation FindInDrives(IEnumerable<string> driveRoots)
        {
            foreach (var root in driveRoots ?? Enumerable.Empty<string>())
            {
                foreach (var folder in GamingFolders(root))
                {
                    var dir = Path.Combine(root, folder);
                    string[] games;
                    try
                    {
                        if (!Directory.Exists(dir)) continue;
                        games = Directory.GetDirectories(dir);
                    }
                    catch (Exception e) when (e is IOException || e is UnauthorizedAccessException) { continue; }
                    foreach (var game in games.OrderBy(g => g, StringComparer.OrdinalIgnoreCase))
                    {
                        var content = Path.Combine(game, ContentFolder);
                        if (IsDoomEternal(ReadIdentity(content)))
                            return Locate(content, ConfigFile + " in " + dir);
                    }
                }
            }
            return null;
        }

        /// <summary>The folders Gaming Services uses on a drive: those its <c>.GamingRoot</c> names, then <c>XboxGames</c>.</summary>
        public static IReadOnlyList<string> GamingFolders(string driveRoot)
        {
            var list = new List<string>();
            try
            {
                var file = Path.Combine(driveRoot, GamingRootFile);
                if (File.Exists(file)) list.AddRange(ParseGamingRoot(File.ReadAllBytes(file)));
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException || e is ArgumentException) { }
            if (!list.Contains(DefaultGamingFolder, StringComparer.OrdinalIgnoreCase)) list.Add(DefaultGamingFolder);
            return list;
        }

        /// <summary>
        /// The build check of a Game Pass install: missing when the folder has no DOOM Eternal identity or no exe,
        /// else known or unknown by the identity's version (<see cref="KnownBuilds.CheckStore"/>).
        /// </summary>
        public static BuildCheck Check(KnownBuilds builds, string contentDir)
        {
            var identity = ReadIdentity(contentDir);
            if (!IsDoomEternal(identity) || !File.Exists(Path.Combine(contentDir, GameLayout.RetailExe)))
                return new BuildCheck(BuildStatus.Missing, null, null);
            return builds.CheckStore(identity);
        }

        /// <summary>The package's save containers (<c>Packages\&lt;name&gt;_&lt;publisher id&gt;\SystemAppData\wgs</c>); null when absent.</summary>
        public static string SaveContainerDir(string localAppData)
        {
            if (string.IsNullOrEmpty(localAppData)) return null;
            try
            {
                var packages = Path.Combine(localAppData, "Packages");
                if (!Directory.Exists(packages)) return null;
                return Directory.GetDirectories(packages, PackageName + "_*")
                    .OrderBy(d => d, StringComparer.OrdinalIgnoreCase)
                    .Select(d => Path.Combine(d, "SystemAppData", "wgs"))
                    .FirstOrDefault(Directory.Exists);
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException) { return null; }
        }

        /// <summary>The save containers as a save backup location; null when the folder is absent.</summary>
        public static SettingsLocation SaveLocation(string localAppData)
        {
            var dir = SaveContainerDir(localAppData);
            return dir == null ? null : new SettingsLocation(SaveLocationName, SettingsLocationKind.GamePassSaves, dir);
        }
    }
}
