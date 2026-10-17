using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace EternalVR.Launcher.Core.Settings
{
    /// <summary>
    /// Player profiles: named sets of the Play and Advanced settings, one file per profile in
    /// <c>&lt;data folder&gt;\profiles\&lt;name&gt;.ini</c> (launcher.ini's format). A profile is a preset plus the player's own
    /// changes (REQ-10): its file names its preset (<c>preset</c>) and holds only the settings that differ from it
    /// (<see cref="ProfileEdits"/>); a file from an older launcher holds every setting and reads the same. The first time
    /// such a file is saved over, it is kept in <see cref="BackupDir"/>. launcher.ini stays the settings in use and names the
    /// active profile (<c>profile</c>); the window saves a change to both. A profile never carries this machine's game
    /// folder, layer folder or OpenXR runtime: loading one keeps them, as Reset to Recommended does.
    /// </summary>
    public sealed class ProfileStore
    {
        public const int MaxNameLength = 32;
        private const string Extension = ".ini";

        private static readonly HashSet<string> Reserved = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
        {
            "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
            "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
        };

        /// <summary>The characters a Windows file name cannot hold (Path.GetInvalidFileNameChars on Windows), on every OS.</summary>
        private static readonly char[] InvalidNameChars =
            Enumerable.Range(0, 32).Select(c => (char)c).Concat(new[] { '"', '<', '>', '|', ':', '*', '?', '\\', '/' }).ToArray();

        public ProfileStore(string dir)
        {
            if (string.IsNullOrWhiteSpace(dir)) throw new ArgumentException("the profiles folder is empty", nameof(dir));
            Dir = dir;
        }

        public string Dir { get; }

        /// <summary>Each profile file as an older launcher wrote it (every setting, no <c>preset</c>), kept the first time it is saved over.</summary>
        public string BackupDir => Path.Combine(Dir, "backup");

        /// <summary>
        /// A profile name as typed, trimmed; null when it cannot be a profile: empty, longer than
        /// <see cref="MaxNameLength"/>, a character a file name cannot hold, a leading or trailing dot, or a
        /// name Windows reserves.
        /// </summary>
        public static string NormaliseName(string name)
        {
            var n = (name ?? string.Empty).Trim();
            if (n.Length == 0 || n.Length > MaxNameLength) return null;
            if (n.IndexOfAny(InvalidNameChars) >= 0 || n.StartsWith(".", StringComparison.Ordinal) || n.EndsWith(".", StringComparison.Ordinal)) return null;
            if (Reserved.Contains(n)) return null;
            return n;
        }

        /// <summary>The profiles on disk, sorted by name (case ignored); none when the folder does not exist.</summary>
        public IReadOnlyList<string> Names()
        {
            if (!Directory.Exists(Dir)) return new string[0];
            return Directory.GetFiles(Dir, "*")
                .Where(f => f.EndsWith(Extension, StringComparison.OrdinalIgnoreCase))
                .Select(Path.GetFileNameWithoutExtension)
                .Where(n => NormaliseName(n) == n)
                .OrderBy(n => n, StringComparer.OrdinalIgnoreCase)
                .ToList();
        }

        public bool Exists(string name) => ExistingFile(name) != null;

        /// <summary>
        /// Writes <paramref name="settings"/> as the profile <paramref name="name"/>: its preset and the settings that differ
        /// from it (<see cref="ProfileEdits.ProfileText"/>, CPU Saver items compared by <paramref name="saver"/>'s), without this
        /// machine's folders, runtime, DLSS file and graphics card choice.
        /// </summary>
        public void Save(string name, LauncherSettings settings, Launch.CpuSaver saver = null)
        {
            var n = NormaliseName(name) ?? throw new ArgumentException("not a profile name: " + name, nameof(name));
            var copy = LauncherSettings.Parse(settings.Serialize());
            copy.GameDir = string.Empty;
            copy.LayerDir = string.Empty;
            copy.Runtime = LauncherSettings.SystemRuntime;
            copy.DlssDllPath = string.Empty;
            copy.AdapterSteer = false;
            copy.Profile = string.Empty;
            Directory.CreateDirectory(Dir);
            KeepOlderFile(n);
            FileUtil.WriteAllTextAtomic(PathOf(n), ProfileEdits.ProfileText(copy, saver));
        }

        /// <summary>
        /// Copies the profile's file into <see cref="BackupDir"/> when an older launcher wrote it (no <c>preset</c> line) and no
        /// copy is there yet: the file about to replace it holds only the changes from Recommended.
        /// </summary>
        private void KeepOlderFile(string name)
        {
            if (!(ExistingFile(name) is string file)) return;
            if (LauncherSettings.ReadMap(File.ReadAllText(file)).ContainsKey("preset")) return;
            var backup = Path.Combine(BackupDir, Path.GetFileName(file));
            if (File.Exists(backup)) return;
            Directory.CreateDirectory(BackupDir);
            File.Copy(file, backup);
        }

        /// <summary>
        /// The profile <paramref name="name"/>'s settings with <paramref name="current"/>'s folders, runtime, DLSS file and graphics card, and
        /// <see cref="LauncherSettings.Profile"/> set to it; null when there is no such profile.
        /// </summary>
        public LauncherSettings Load(string name, LauncherSettings current)
        {
            if (!(NormaliseName(name) is string n) || !(ExistingFile(n) is string file)) return null;
            var s = ProfileEdits.FromProfileText(File.ReadAllText(file));
            s.GameDir = current.GameDir;
            s.LayerDir = current.LayerDir;
            s.Runtime = current.Runtime;
            s.DlssDllPath = current.DlssDllPath;
            s.AdapterSteer = current.AdapterSteer;
            s.Profile = n;
            return s;
        }

        /// <summary>Removes the profile's file; nothing when there is none.</summary>
        public void Delete(string name)
        {
            if (ExistingFile(name) is string file) File.Delete(file);
        }

        /// <summary>The file profile <paramref name="name"/> has or would have (the name as given).</summary>
        public string PathOf(string name) => Path.Combine(Dir, name + Extension);

        /// <summary>
        /// The file of the profile <paramref name="name"/>, its case ignored as Windows does on every OS; null when there is none
        /// or the name cannot be a profile.
        /// </summary>
        public string ExistingFile(string name)
        {
            if (!(NormaliseName(name) is string n)) return null;
            name = n;
            if (File.Exists(PathOf(name))) return PathOf(name);
            var found = Names().FirstOrDefault(other => string.Equals(other, name, StringComparison.OrdinalIgnoreCase));
            return found == null ? null : PathOf(found);
        }
    }
}
