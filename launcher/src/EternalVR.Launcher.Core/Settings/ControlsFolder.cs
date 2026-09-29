using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using EternalVR.Launcher.Core.Controls;

namespace EternalVR.Launcher.Core.Settings
{
    /// <summary>
    /// One set of the player's controls (docs/release/CONTROLS.md): <c>&lt;data root&gt;\controls\</c> for no VR settings
    /// profile, or a profile's own folder under it (<see cref="ControlSets"/>). It holds a copy of every built-in controller
    /// map in <c>defaults\</c>, and the player's own edited maps directly in the folder. When the set in use holds any, the
    /// launch passes its folder to the layer (<c>ETERNALVR_CONTROLLER_DATA</c>), which reads every <c>*.toml</c> directly in
    /// it (so the profiles' folders under the no-profile set are not read with it).
    /// </summary>
    public sealed class ControlsFolder
    {
        public const string ReadmeName = "README.txt";
        public const string DefaultsName = "defaults";
        public const string MapExtension = ".toml";

        /// <summary>
        /// The README of a set of controls: <paramref name="profile"/>'s own (<see cref="ControlSets"/>), or with null the
        /// controls used with no VR settings profile.
        /// </summary>
        public static string ReadmeFor(string profile) =>
            (string.IsNullOrEmpty(profile)
                ? "Your EternalVR controls\r\n"
                    + "\r\n"
                    + "The files here are the controls used when the launcher's VR settings profile is (none). Each VR settings\r\n"
                    + "profile keeps its own controls in its own folder in profiles, for example profiles\\Karen.\r\n"
                : "Your EternalVR controls for the VR settings profile " + profile + "\r\n"
                    + "\r\n"
                    + "The files here are the controls used when the launcher's VR settings profile is " + profile + ".\r\n")
            + "\r\n"
            + "The easy way to change your controls is Edit controls on the launcher's Play tab: pick your controllers,\r\n"
            + "pick an action for each button and press Save. It saves the controls of the VR settings profile in use.\r\n"
            + "\r\n"
            + "The defaults folder holds the built-in controls, one file per kind of controller:\r\n"
            + "  oculus_touch.toml: Meta Quest and Rift (Touch)\r\n"
            + "  valve_index.toml: Valve Index\r\n"
            + "  hp_reverb_g2.toml: HP Reverb G2\r\n"
            + "  windows_mixed_reality.toml: Windows Mixed Reality\r\n"
            + "  htc_vive_cosmos.toml: HTC Vive Cosmos\r\n"
            + "  htc_vive_wand.toml: HTC Vive wands\r\n"
            + "  pico4.toml: Pico 4\r\n"
            + "The launcher replaces everything in defaults each time you press Edit controls or Open folder, so do not\r\n"
            + "edit there.\r\n"
            + "\r\n"
            + "To change your controls by hand instead:\r\n"
            + "1. Copy the file for your controllers from defaults into this folder (next to this README).\r\n"
            + "2. Open your copy in a text editor such as Notepad. Change only the lines under the [map.right],\r\n"
            + "   [map.left_button_swap] and [map.left_full_mirror] headings, for example\r\n"
            + "   \"right.primary.press\" = \"jump\". The map that is used follows the launcher's Weapon hand setting.\r\n"
            + "3. Save it and start the game from the launcher.\r\n"
            + "\r\n"
            + "If your file has a mistake, the game uses the built-in controls, and the session log names the\r\n"
            + "problem (look for \"controllers:\" lines in logs\\<session>\\eternalvr-*.log in the EternalVR data\r\n"
            + "folder). Edit controls shows the same problems. Delete your file to go back to the built-in controls.\r\n"
            + "\r\n"
            + "The full guide, with every action name, is docs\\CONTROLS.md in the EternalVR download.\r\n";

        /// <summary>The README of the controls used with no VR settings profile.</summary>
        public static readonly string Readme = ReadmeFor(null);

        /// <param name="dir">The folder.</param>
        /// <param name="profile">The VR settings profile whose controls these are; null for the controls used with no profile.</param>
        public ControlsFolder(string dir, string profile = null)
        {
            if (string.IsNullOrWhiteSpace(dir)) throw new ArgumentException("controls folder is empty", nameof(dir));
            Dir = Path.GetFullPath(dir);
            Profile = string.IsNullOrEmpty(profile) ? null : profile;
        }

        public string Dir { get; }
        /// <summary>The VR settings profile whose controls these are; null for the controls used with no profile.</summary>
        public string Profile { get; }
        public string DefaultsDir => Path.Combine(Dir, DefaultsName);
        public string ReadmeFile => Path.Combine(Dir, ReadmeName);

        /// <summary>
        /// Creates the folder, writes the README and refreshes <c>defaults\</c> with a fresh copy of every built-in map in
        /// <paramref name="defaultsSourceDir"/> (a file there that is no longer built in is deleted). The player's own
        /// files directly in the folder are left alone. Throws an <see cref="IOException"/> when the source cannot be read.
        /// </summary>
        public void Prepare(string defaultsSourceDir)
        {
            var sources = MapFiles(defaultsSourceDir);
            Directory.CreateDirectory(Dir);
            Directory.CreateDirectory(DefaultsDir);
            FileUtil.WriteAllTextAtomic(ReadmeFile, ReadmeFor(Profile));
            var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var source in sources)
            {
                var name = Path.GetFileName(source);
                names.Add(name);
                var target = Path.Combine(DefaultsDir, name);
                if (File.Exists(target)) File.SetAttributes(target, FileAttributes.Normal);
                File.Copy(source, target, overwrite: true);
            }
            foreach (var stale in Directory.GetFiles(DefaultsDir).Where(f => !names.Contains(Path.GetFileName(f))))
            {
                File.SetAttributes(stale, FileAttributes.Normal);
                File.Delete(stale);
            }
        }

        /// <summary>The player's own maps: the <c>*.toml</c> files directly in the folder (none when it does not exist).</summary>
        public IReadOnlyList<string> PlayerMaps() => Directory.Exists(Dir) ? MapFiles(Dir) : new string[0];

        /// <summary>True when the folder holds at least one map of the player's own (a <c>*.toml</c> directly in it).</summary>
        public bool HasPlayerMaps
        {
            get
            {
                try { return Directory.Exists(Dir) && MapFiles(Dir).Count > 0; }
                catch (Exception e) when (e is IOException || e is UnauthorizedAccessException) { return false; }
            }
        }

        /// <summary>
        /// The kinds of controller with a built-in file in <c>defaults\</c> (after <see cref="Prepare"/>), in the README's order.
        /// A file that cannot be read, or names no interaction profile, is left out.
        /// </summary>
        public IReadOnlyList<ControllerFamily> Families()
        {
            var list = new List<ControllerFamily>();
            if (!Directory.Exists(DefaultsDir)) return list;
            foreach (var file in MapFiles(DefaultsDir))
            {
                var profile = ProfilePathOf(file);
                if (!string.IsNullOrEmpty(profile)) list.Add(new ControllerFamily(file, profile));
            }
            return list.OrderBy(f => ControlNames.FamilyOrder(f.FileName)).ThenBy(f => f.FileName, StringComparer.OrdinalIgnoreCase).ToList();
        }

        /// <summary>
        /// The player's files that name the interaction profile <paramref name="profilePath"/>, in the order the layer reads
        /// them (features/input/player_controller_data.hpp: by name, ignoring case). The layer uses the last one.
        /// </summary>
        public IReadOnlyList<string> PlayerFilesFor(string profilePath)
        {
            if (!Directory.Exists(Dir)) return new string[0];
            return MapFiles(Dir)
                .OrderBy(f => AsciiLower(Path.GetFileName(f)), StringComparer.Ordinal)
                .ThenBy(f => Path.GetFileName(f), StringComparer.Ordinal)
                .Where(f => ProfilePathOf(f) == profilePath)
                .ToList();
        }

        /// <summary>The player's file the layer uses for the interaction profile <paramref name="profilePath"/>, or null.</summary>
        public string PlayerFileFor(string profilePath) => PlayerFilesFor(profilePath).LastOrDefault();

        private static string ProfilePathOf(string file)
        {
            try { return ControllerMapFile.Parse(ControlsEdit.ReadText(file)).ProfilePath; }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException) { return null; }
        }

        /// <summary>Lower case for A to Z only, as the layer compares names.</summary>
        private static string AsciiLower(string s) => new string(s.Select(c => c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c).ToArray());

        /// <summary>The <c>*.toml</c> files directly in <paramref name="dir"/>, as the layer reads them (a name with more before the extension).</summary>
        private static IReadOnlyList<string> MapFiles(string dir) =>
            Directory.GetFiles(dir, "*" + MapExtension, SearchOption.TopDirectoryOnly)
                .Where(f =>
                {
                    var name = Path.GetFileName(f);
                    return name.Length > MapExtension.Length && name.EndsWith(MapExtension, StringComparison.OrdinalIgnoreCase);
                })
                .ToList();
    }
}
