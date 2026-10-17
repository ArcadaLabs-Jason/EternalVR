using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace EternalVR.Launcher.Core.Settings
{
    /// <summary>
    /// The base preset of the settings (REQ-10): <c>preset = recommended</c> in launcher.ini and in a profile file
    /// (<see cref="SettingsPresets"/>). A file without the key (an older launcher's) is based on Recommended. An id this
    /// launcher does not know (a newer launcher's preset) is kept as it is and compared as Recommended. launcher.ini also
    /// lists the changes not yet saved into its active profile (<c>profile_unsaved = turn,vignette</c>, T-134) with a check
    /// value of the settings that list was worked out from (<c>profile_check</c>), so the next start takes the rest from the
    /// profile's file and its preset only while launcher.ini is as this launcher wrote it (<see cref="ProfileEdits.InUse"/>).
    /// </summary>
    public sealed partial class LauncherSettings
    {
        /// <summary>The id of the preset these settings start from (<see cref="SettingsPresets"/>); Recommended by default.</summary>
        public string Preset { get; set; } = SettingsPresets.RecommendedId;

        /// <summary>
        /// The settings file keys whose value differs from the active profile's file (changes not saved into it yet); null when
        /// launcher.ini does not say (no profile, or an older launcher wrote it). This machine's: never in a profile.
        /// </summary>
        public IReadOnlyList<string> ProfileUnsaved { get; set; }

        /// <summary>
        /// <see cref="ProfileEdits.Fingerprint"/> of the settings file lines <see cref="ProfileUnsaved"/> was worked out from;
        /// written with it. A launcher.ini whose lines no longer give it (<see cref="FileCheck"/>) was changed after the list
        /// was made.
        /// </summary>
        public string ProfileCheck { get; set; }

        /// <summary>The check value of the lines these settings were read from (<see cref="Parse"/>); null when not read from a file.</summary>
        public string FileCheck { get; internal set; }

        private void ReadPreset(IDictionary<string, string> map)
        {
            if (map.TryGetValue("preset", out var p) && SettingsPresets.NormaliseId(p) is string id) Preset = id;
            if (map.TryGetValue("profile_unsaved", out var u))
                ProfileUnsaved = u.Split(',').Select(k => k.Trim()).Where(k => k.Length > 0).Distinct(StringComparer.OrdinalIgnoreCase).ToList();
            if (map.TryGetValue("profile_check", out var c)) ProfileCheck = c.Trim();
            FileCheck = ProfileEdits.Fingerprint(map);
        }

        private void WritePreset(StringBuilder sb)
        {
            sb.AppendLine("preset = " + (SettingsPresets.NormaliseId(Preset) ?? SettingsPresets.RecommendedId));
            if (ProfileUnsaved == null || Profile.Length == 0) return;
            sb.AppendLine("profile_unsaved = " + string.Join(",", ProfileUnsaved));
            sb.AppendLine("profile_check = " + OneLine(ProfileCheck));
        }
    }
}
