using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace EternalVR.Launcher.Core.Settings
{
    /// <summary>
    /// The player's controls folder, <c>&lt;data root&gt;\controls\</c> (docs/release/CONTROLS.md): a copy of every built-in
    /// controller map in <c>defaults\</c>, and the player's own edited maps directly in the folder. When it holds any, the
    /// launch passes the folder to the layer (<c>ETERNALVR_CONTROLLER_DATA</c>), which reads every <c>*.toml</c> in it.
    /// </summary>
    public sealed class ControlsFolder
    {
        public const string ReadmeName = "README.txt";
        public const string DefaultsName = "defaults";
        public const string MapExtension = ".toml";

        public const string Readme =
            "Your EternalVR controls\r\n"
            + "\r\n"
            + "The defaults folder holds the built-in controls, one file per kind of controller:\r\n"
            + "  oculus_touch.toml: Meta Quest and Rift (Touch)\r\n"
            + "  valve_index.toml: Valve Index\r\n"
            + "  hp_reverb_g2.toml: HP Reverb G2\r\n"
            + "  windows_mixed_reality.toml: Windows Mixed Reality\r\n"
            + "  htc_vive_cosmos.toml: HTC Vive Cosmos\r\n"
            + "  htc_vive_wand.toml: HTC Vive wands\r\n"
            + "  pico4.toml: Pico 4\r\n"
            + "The launcher replaces everything in defaults each time you press Edit controls, so do not edit there.\r\n"
            + "\r\n"
            + "To change your controls:\r\n"
            + "1. Copy the file for your controllers from defaults into this folder (next to this README).\r\n"
            + "2. Open your copy in a text editor such as Notepad. Change only the lines under the [map.right],\r\n"
            + "   [map.left_button_swap] and [map.left_full_mirror] headings, for example\r\n"
            + "   \"right.primary.press\" = \"jump\". The map that is used follows the launcher's Weapon hand setting.\r\n"
            + "3. Save it and start the game from the launcher.\r\n"
            + "\r\n"
            + "If your file has a mistake, the game uses the built-in controls, and the session log names the\r\n"
            + "problem (look for \"controllers:\" lines in logs\\<session>\\eternalvr-*.log in the EternalVR data\r\n"
            + "folder). Delete your file to go back to the built-in controls.\r\n"
            + "\r\n"
            + "The full guide, with every action name, is docs\\CONTROLS.md in the EternalVR download.\r\n";

        public ControlsFolder(string dir)
        {
            if (string.IsNullOrWhiteSpace(dir)) throw new ArgumentException("controls folder is empty", nameof(dir));
            Dir = Path.GetFullPath(dir);
        }

        public string Dir { get; }
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
            FileUtil.WriteAllTextAtomic(ReadmeFile, Readme);
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

        /// <summary>True when the folder holds at least one map of the player's own (a <c>*.toml</c> directly in it).</summary>
        public bool HasPlayerMaps
        {
            get
            {
                try { return Directory.Exists(Dir) && MapFiles(Dir).Count > 0; }
                catch (Exception e) when (e is IOException || e is UnauthorizedAccessException) { return false; }
            }
        }

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
