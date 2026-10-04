using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;

namespace EternalVR.Launcher.Core.Game
{
    /// <summary>Fixed facts about DOOM Eternal's install and user files.</summary>
    public static class GameLayout
    {
        public const string SteamAppId = "782330";
        public const string RetailExe = "DOOMEternalx64vk.exe";

        /// <summary>Processes that count as "the game is running" (T-108 rule 5, ARCHITECTURE section 4).</summary>
        public static readonly IReadOnlyList<string> GameProcessNames =
            new[] { "DOOMEternalx64vk", "DOOMSandBox64vk", "idTechLauncher" };

        /// <summary>
        /// The folder under <c>Saved Games\id Software\DOOMEternal</c> that holds the text configs, the game's console log
        /// (<c>qconsole.log</c>), its crash reports (<c>Crash.&lt;computer&gt;.&lt;number&gt;.html</c>) and <c>crash-dumps</c>.
        /// </summary>
        public const string SavedGamesBaseFolder = "base";

        /// <summary>Text config files under <c>Saved Games\id Software\DOOMEternal</c> (T-099).</summary>
        public static readonly IReadOnlyList<string> SavedGamesConfigFiles = new[]
        {
            Path.Combine(SavedGamesBaseFolder, "DOOMEternalConfig.cfg"),
            Path.Combine(SavedGamesBaseFolder, "DOOMEternalConfig.local"),
            Path.Combine("user", "config.json"),
        };

        /// <summary>The folder inside <c>userdata\&lt;id&gt;\782330\remote</c> holding the profile.</summary>
        public const string ProfileFolder = "PROFILE";

        /// <summary>Steam's userdata folder name for a SteamID64 (the 32-bit account ID).</summary>
        public static string AccountIdFromSteamId64(string steamId64)
        {
            if (!ulong.TryParse(steamId64, NumberStyles.None, CultureInfo.InvariantCulture, out var id)) return null;
            const ulong individualBase = 76561197960265728UL;
            if (id < individualBase) return null;
            return (id - individualBase).ToString(CultureInfo.InvariantCulture);
        }

        /// <summary>
        /// A Microsoft Store / Game Pass install: a <c>MicrosoftGame.config</c> or <c>appxmanifest.xml</c> in the game folder, or a
        /// path under <c>WindowsApps</c> or <c>XboxGames</c>. A game folder that is one is taken as the Game Pass install
        /// (<see cref="GamePassInstall"/>): its Content folder, else the one found on the drives.
        /// </summary>
        public static bool IsStoreInstall(string gameRoot)
        {
            if (string.IsNullOrEmpty(gameRoot)) return false;
            // Both separators on every platform: the paths are Windows paths, the Core tests also run on Linux.
            var parts = gameRoot.Split('\\', '/');
            if (parts.Any(p => string.Equals(p, "WindowsApps", StringComparison.OrdinalIgnoreCase) || string.Equals(p, "XboxGames", StringComparison.OrdinalIgnoreCase)))
                return true;
            try
            {
                return File.Exists(Path.Combine(gameRoot, "MicrosoftGame.config")) || File.Exists(Path.Combine(gameRoot, "appxmanifest.xml"));
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException || e is ArgumentException) { return false; }
        }

        /// <summary>The settings locations of an install: Game Pass keeps its settings in the Saved Games folder only.</summary>
        public static IReadOnlyList<SettingsLocation> FindSettingsLocations(GamePlatform platform, string savedGamesDir, string steamRoot, string activeAccountId) =>
            platform == GamePlatform.GamePass
                ? FindSettingsLocations(savedGamesDir, null, null)
                : FindSettingsLocations(savedGamesDir, steamRoot, activeAccountId);

        /// <summary>
        /// Every settings location that exists (T-094, T-099): the Saved Games folder, and the
        /// <c>782330\remote</c> folder of the active Steam user, or of every user when the active one is
        /// unknown (0 or null means unknown).
        /// </summary>
        /// <remarks>
        /// When a Steam location is found the Saved Games location is included even if its folder does not
        /// exist yet: a launch then forces cvars, the game may create its text configs there, and the
        /// snapshot records them as absent so the restore can take the forced keys out again.
        /// </remarks>
        public static IReadOnlyList<SettingsLocation> FindSettingsLocations(string savedGamesDir, string steamRoot, string activeAccountId)
        {
            var list = new List<SettingsLocation>();
            bool savedExists = !string.IsNullOrEmpty(savedGamesDir) && Directory.Exists(savedGamesDir);
            if (savedExists)
                list.Add(new SettingsLocation("saved-games", SettingsLocationKind.SavedGames, savedGamesDir));

            if (string.IsNullOrEmpty(steamRoot)) return list;
            var userdata = Path.Combine(steamRoot, "userdata");
            if (!Directory.Exists(userdata)) return list;

            IEnumerable<string> accounts;
            bool activeKnown = !string.IsNullOrEmpty(activeAccountId) && activeAccountId != "0";
            if (activeKnown) accounts = new[] { activeAccountId };
            else accounts = Directory.GetDirectories(userdata).Select(Path.GetFileName).OrderBy(n => n, StringComparer.Ordinal);

            foreach (var account in accounts)
            {
                var remote = Path.Combine(userdata, account, SteamAppId, "remote");
                if (Directory.Exists(remote))
                    list.Add(new SettingsLocation("steam-" + account, SettingsLocationKind.SteamRemote, remote));
            }
            if (!savedExists && !string.IsNullOrEmpty(savedGamesDir) && list.Count > 0)
                list.Insert(0, new SettingsLocation("saved-games", SettingsLocationKind.SavedGames, savedGamesDir));
            return list;
        }

        /// <summary>
        /// The config files the game may create in a location during a session although they were absent
        /// before (the fixed text configs of the Saved Games location; a Steam profile's files are not known in advance).
        /// </summary>
        public static IReadOnlyList<string> AbsentConfigFilesOf(SettingsLocation location) =>
            location.Kind == SettingsLocationKind.SavedGames
                ? SavedGamesConfigFiles.Where(f => !File.Exists(Path.Combine(location.Path, f))).ToList()
                : (IReadOnlyList<string>)new string[0];

        /// <summary>Save-slot folders in a Steam remote folder: every subfolder except PROFILE.</summary>
        public static IReadOnlyList<string> SaveSlotFolders(string remoteDir)
        {
            if (!Directory.Exists(remoteDir)) return new string[0];
            return Directory.GetDirectories(remoteDir)
                .Where(d => !string.Equals(Path.GetFileName(d), ProfileFolder, StringComparison.OrdinalIgnoreCase))
                .OrderBy(d => d, StringComparer.OrdinalIgnoreCase)
                .ToList();
        }

        /// <summary>The config files of a location that the snapshot covers (those that exist).</summary>
        public static IReadOnlyList<string> ConfigFilesOf(SettingsLocation location)
        {
            if (location.Kind == SettingsLocationKind.SavedGames)
                return SavedGamesConfigFiles.Where(f => File.Exists(Path.Combine(location.Path, f))).ToList();

            var profile = Path.Combine(location.Path, ProfileFolder);
            if (!Directory.Exists(profile)) return new string[0];
            return Directory.GetFiles(profile, "*", SearchOption.AllDirectories)
                .Select(f => f.Substring(location.Path.Length).TrimStart('\\', '/'))
                .OrderBy(f => f, StringComparer.OrdinalIgnoreCase)
                .ToList();
        }

        /// <summary>
        /// A cvar's value in the player's text configs, the last file that sets it winning (DOOMEternalConfig.cfg before
        /// .local); null when none sets it or none can be read.
        /// </summary>
        public static string PlayerCvar(IEnumerable<SettingsLocation> locations, string name)
        {
            string value = null;
            foreach (var location in locations)
                foreach (var file in ConfigFilesOf(location).Where(IsKeyedTextConfig))
                {
                    try
                    {
                        if (CvarConfig.ParseBytes(File.ReadAllBytes(Path.Combine(location.Path, file)), out _).TryGet(name, out var v)) value = v;
                    }
                    catch (IOException) { }
                    catch (UnauthorizedAccessException) { }
                }
            return value;
        }

        /// <summary>True for the id Tech text configs whose keys we restore one by one.</summary>
        public static bool IsKeyedTextConfig(string relativePath)
        {
            var ext = Path.GetExtension(relativePath);
            return string.Equals(ext, ".cfg", StringComparison.OrdinalIgnoreCase)
                || string.Equals(ext, ".local", StringComparison.OrdinalIgnoreCase);
        }
    }

    public enum SettingsLocationKind
    {
        SavedGames,
        SteamRemote,
        /// <summary>The Game Pass save containers (<c>wgs</c>): copied into the save backups only, never snapshotted or restored.</summary>
        GamePassSaves,
    }

    public sealed class SettingsLocation
    {
        public SettingsLocation(string name, SettingsLocationKind kind, string path)
        {
            Name = name;
            Kind = kind;
            Path = path;
        }

        /// <summary>Stable folder name inside a snapshot or backup: "saved-games", "steam-&lt;account&gt;" or "gamepass".</summary>
        public string Name { get; }
        public SettingsLocationKind Kind { get; }
        public string Path { get; }
    }
}
