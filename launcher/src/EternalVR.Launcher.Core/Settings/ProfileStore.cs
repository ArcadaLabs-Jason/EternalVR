using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace EternalVR.Launcher.Core.Settings
{
    /// <summary>
    /// Player profiles: named copies of the Play and Advanced settings, one file per profile in
    /// <c>&lt;data folder&gt;\profiles\&lt;name&gt;.ini</c> (launcher.ini's format). launcher.ini stays the settings in
    /// use and names the active profile (<c>profile</c>); the window saves a change to both. A profile never
    /// carries this machine's game folder, layer folder or OpenXR runtime: loading one keeps them, as Reset to
    /// defaults does.
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

        public ProfileStore(string dir)
        {
            if (string.IsNullOrWhiteSpace(dir)) throw new ArgumentException("the profiles folder is empty", nameof(dir));
            Dir = dir;
        }

        public string Dir { get; }

        /// <summary>
        /// A profile name as typed, trimmed; null when it cannot be a profile: empty, longer than
        /// <see cref="MaxNameLength"/>, a character a file name cannot hold, a leading or trailing dot, or a
        /// name Windows reserves.
        /// </summary>
        public static string NormaliseName(string name)
        {
            var n = (name ?? string.Empty).Trim();
            if (n.Length == 0 || n.Length > MaxNameLength) return null;
            if (n.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 || n.StartsWith(".", StringComparison.Ordinal) || n.EndsWith(".", StringComparison.Ordinal)) return null;
            if (Reserved.Contains(n)) return null;
            return n;
        }

        /// <summary>The profiles on disk, sorted by name (case ignored); none when the folder does not exist.</summary>
        public IReadOnlyList<string> Names()
        {
            if (!Directory.Exists(Dir)) return new string[0];
            return Directory.GetFiles(Dir, "*" + Extension)
                .Select(Path.GetFileNameWithoutExtension)
                .Where(n => NormaliseName(n) == n)
                .OrderBy(n => n, StringComparer.OrdinalIgnoreCase)
                .ToList();
        }

        public bool Exists(string name) => NormaliseName(name) is string n && File.Exists(PathOf(n));

        /// <summary>Writes <paramref name="settings"/> as the profile <paramref name="name"/>, without this machine's folders and runtime.</summary>
        public void Save(string name, LauncherSettings settings)
        {
            var n = NormaliseName(name) ?? throw new ArgumentException("not a profile name: " + name, nameof(name));
            var copy = LauncherSettings.Parse(settings.Serialize());
            copy.GameDir = string.Empty;
            copy.LayerDir = string.Empty;
            copy.Runtime = LauncherSettings.SystemRuntime;
            copy.Profile = string.Empty;
            Directory.CreateDirectory(Dir);
            FileUtil.WriteAllTextAtomic(PathOf(n), copy.Serialize());
        }

        /// <summary>
        /// The profile <paramref name="name"/>'s settings with <paramref name="current"/>'s folders and runtime, and
        /// <see cref="LauncherSettings.Profile"/> set to it; null when there is no such profile.
        /// </summary>
        public LauncherSettings Load(string name, LauncherSettings current)
        {
            if (!(NormaliseName(name) is string n) || !File.Exists(PathOf(n))) return null;
            var s = LauncherSettings.Parse(File.ReadAllText(PathOf(n)));
            s.GameDir = current.GameDir;
            s.LayerDir = current.LayerDir;
            s.Runtime = current.Runtime;
            s.Profile = n;
            return s;
        }

        /// <summary>Removes the profile's file; nothing when there is none.</summary>
        public void Delete(string name)
        {
            if (NormaliseName(name) is string n && File.Exists(PathOf(n))) File.Delete(PathOf(n));
        }

        private string PathOf(string name) => Path.Combine(Dir, name + Extension);
    }
}
