using System;
using System.Collections.Generic;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using EternalVR.Launcher.Core.Launch;

namespace EternalVR.Launcher.Core.Settings
{
    /// <summary>
    /// launcher.ini with a VR settings profile in use (T-134): the changes not yet saved into the profile are listed with a
    /// check value when launcher.ini is written (<see cref="ForSave"/>), and at start the profile's file, read now, gives the
    /// rest while launcher.ini is still as the launcher wrote it (<see cref="InUse"/>).
    /// </summary>
    public static partial class ProfileEdits
    {
        /// <summary>
        /// The keys the check value leaves out: this machine's paths, runtime and graphics card choice, the file's schema, and the list and check
        /// themselves. Frozen with the check's format (not <see cref="SettingKeys.Machine"/>): a change here changes every check
        /// once, and launcher.ini is then used as it is. The profile and preset lines are checked: the list is the named
        /// profile's.
        /// </summary>
        private static readonly HashSet<string> NotChecked = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
        {
            "schema_version", "game_dir", "layer_dir", "runtime", "dlss_dll_path", "adapter_steer", "profile_unsaved", "profile_check",
        };

        /// <summary>
        /// A short check value of a settings file's lines (<paramref name="map"/>, every key but <see cref="NotChecked"/>), in
        /// key order: 16 hex digits of their SHA-256. Taken from the lines as they are in the file, so a later launcher that
        /// writes more keys still finds the value an earlier one wrote.
        /// </summary>
        public static string Fingerprint(IDictionary<string, string> map)
        {
            var text = new StringBuilder();
            foreach (var kv in map.Where(kv => !NotChecked.Contains(kv.Key)).OrderBy(kv => kv.Key.ToLowerInvariant(), StringComparer.Ordinal))
                text.Append(kv.Key.ToLowerInvariant()).Append('=').Append((kv.Value ?? string.Empty).Trim()).Append('\n');
            using (var sha = SHA256.Create())
            {
                var hash = sha.ComputeHash(Encoding.UTF8.GetBytes(text.ToString()));
                return string.Concat(hash.Take(8).Select(b => b.ToString("x2")));
            }
        }

        /// <summary>
        /// True when launcher.ini's list of changes not saved into its profile can be used: it has one, and its settings are
        /// still those the list was worked out from, for the same profile. A setting or the profile edited by hand with the
        /// launcher closed, or an older launcher that kept the lines as it found them while it changed a setting, makes it false.
        /// </summary>
        public static bool ListHolds(LauncherSettings ini) =>
            ini.ProfileUnsaved != null && ini.FileCheck != null && string.Equals(ini.ProfileCheck, ini.FileCheck, StringComparison.Ordinal);

        /// <summary>
        /// The settings in use at start for launcher.ini's active profile (T-134): <paramref name="saved"/>, the profile's file
        /// read now (so every setting it does not change takes the preset's value of this launcher version), with launcher.ini's
        /// values for the changes not yet saved into the profile (<see cref="LauncherSettings.ProfileUnsaved"/>) and the keys this
        /// launcher does not know. launcher.ini as it is when the list cannot be used (<see cref="ListHolds"/>: none, or the
        /// settings changed since it was made) or there is no profile file; its next save makes the list again.
        /// </summary>
        public static LauncherSettings InUse(LauncherSettings ini, LauncherSettings saved, CpuSaver saver = null)
        {
            if (saved == null || !ListHolds(ini)) return ini;
            var keys = ini.ProfileUnsaved.Concat(ini.UnknownKeys.Select(kv => kv.Key));
            var s = Reset(saved, ini, keys, saver);
            // This machine's keys stay launcher.ini's.
            s.GameDir = ini.GameDir;
            s.LayerDir = ini.LayerDir;
            s.Runtime = ini.Runtime;
            s.DlssDllPath = ini.DlssDllPath;
            s.AdapterSteer = ini.AdapterSteer;
            s.Profile = ini.Profile;
            s.ProfileUnsaved = ini.ProfileUnsaved;
            s.ProfileCheck = ini.ProfileCheck;
            return s;
        }

        /// <summary>
        /// The settings to write to launcher.ini: <paramref name="shown"/>, with the hand edits made to the file since the
        /// launcher last wrote it (<paramref name="written"/>, <paramref name="disk"/>; <see cref="LauncherSettings.MergeHandEdits"/>)
        /// when there are any (<paramref name="merged"/>), and then, worked out from that result, the list of changes not saved
        /// into the active profile (<paramref name="saved"/>; none without one) and its check value. Throws
        /// <see cref="SettingsException"/> when the merged file cannot be read.
        /// </summary>
        public static LauncherSettings ForSave(LauncherSettings shown, string written, string disk, LauncherSettings saved, CpuSaver saver,
            out bool merged)
        {
            var s = shown;
            merged = written != null && disk != null && disk != written;
            if (merged) s = LauncherSettings.Parse(LauncherSettings.MergeHandEdits(written, shown.Serialize(), disk));
            MarkUnsaved(s, saved, saver);
            return s;
        }

        /// <summary>
        /// Sets <paramref name="s"/>'s list of changes not saved into its profile (<paramref name="saved"/>) and the check value
        /// of the settings it was worked out from; neither without a profile or its file.
        /// </summary>
        public static void MarkUnsaved(LauncherSettings s, LauncherSettings saved, CpuSaver saver)
        {
            if (s.Profile.Length == 0 || saved == null)
            {
                s.ProfileUnsaved = null;
                s.ProfileCheck = null;
                return;
            }
            s.ProfileUnsaved = Changed(s, saved, saver).OrderBy(k => k, StringComparer.Ordinal).ToList();
            s.ProfileCheck = Fingerprint(LauncherSettings.ReadMap(s.Serialize()));
        }

        /// <summary>
        /// Names profile <paramref name="to"/> in <paramref name="ini"/> in place of the one it names (a rename finished at
        /// start), keeping the list of changes not saved into it usable when it was: the profile is the same, under its new name.
        /// Its check value and the file's are both those of the lines it is saved as, so the same start reads the list too.
        /// </summary>
        public static void Rename(LauncherSettings ini, string to)
        {
            bool held = ListHolds(ini);
            ini.Profile = to;
            if (held) ini.ProfileCheck = ini.FileCheck = Fingerprint(LauncherSettings.ReadMap(ini.Serialize()));
        }
    }
}
