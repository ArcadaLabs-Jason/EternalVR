using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;
using EternalVR.Launcher.Core.Launch;

namespace EternalVR.Launcher.Core.Settings
{
    /// <summary>
    /// A VR settings profile as its preset plus the player's own changes (REQ-10, T-016): which settings differ from the preset
    /// (the window marks them), putting one or all of them back, and the profile file that holds only the changes.
    /// Settings are compared by their values in the settings file (<see cref="LauncherSettings.Serialize"/>), without this
    /// machine's keys (<see cref="SettingKeys.Machine"/>); a CPU Saver item is compared by whether it is on, so an item the
    /// window wrote at its default is not a change.
    /// </summary>
    public static partial class ProfileEdits
    {
        /// <summary>
        /// Keys an older launcher wrote instead of a newer one: while a profile file holds the old key, the newer one is read
        /// from it (<see cref="LauncherSettings.Parse"/>), not taken from the preset.
        /// </summary>
        private static readonly Dictionary<string, string[]> OlderKeys = new Dictionary<string, string[]>(StringComparer.OrdinalIgnoreCase)
        {
            ["dlss_dll"] = new[] { "dlss_version", "dlss_preset" },
            ["pace"] = new[] { "frame_pacing" },
        };

        /// <summary>
        /// The profile's values by key: its settings file lines without this machine's keys, with each CPU Saver item of
        /// <paramref name="saver"/> as <c>on</c> or <c>off</c> (an older launcher's <c>cpu_saver</c> switch then counted in).
        /// Without <paramref name="saver"/> the CPU Saver lines are compared as written.
        /// </summary>
        public static Dictionary<string, string> Values(LauncherSettings s, CpuSaver saver = null)
        {
            var map = LauncherSettings.ReadMap(s.Serialize());
            foreach (var key in SettingKeys.Machine) map.Remove(key);
            if (saver != null && saver.Items.Count > 0)
            {
                foreach (var key in map.Keys.Where(k => IsListed(k, saver)).ToList()) map.Remove(key);
                foreach (var item in saver.Items) map[item.Key] = CpuSaver.IsOn(item, s) ? "on" : "off";
            }
            return map;
        }

        /// <summary>The keys whose value in <paramref name="shown"/> differs from <paramref name="preset"/>'s (or that only one of them has).</summary>
        public static ISet<string> Changed(LauncherSettings shown, LauncherSettings preset, CpuSaver saver = null)
        {
            var mine = Values(shown, saver);
            var theirs = Values(preset, saver);
            var changed = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var kv in mine)
                if (!theirs.TryGetValue(kv.Key, out var v) || !Same(v, kv.Value)) changed.Add(kv.Key);
            foreach (var key in theirs.Keys)
                if (!mine.ContainsKey(key)) changed.Add(key);
            return changed;
        }

        /// <summary>
        /// <paramref name="shown"/> with the settings of <paramref name="keys"/> put back to <paramref name="preset"/>'s values;
        /// every other setting, this machine's keys, the profile and its preset stay as they are.
        /// </summary>
        public static LauncherSettings Reset(LauncherSettings shown, LauncherSettings preset, IEnumerable<string> keys, CpuSaver saver = null)
        {
            var wanted = new HashSet<string>(keys, StringComparer.OrdinalIgnoreCase);
            bool items = saver != null && saver.Items.Count > 0;
            var theirs = LauncherSettings.ReadMap(preset.Serialize());
            var sb = new StringBuilder();
            var done = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var line in Lines(shown.Serialize()))
            {
                var key = LauncherSettings.KeyOf(line);
                if (key == null || !wanted.Contains(key) || SettingKeys.Machine.Contains(key) || IsListed(key, saver))
                {
                    sb.Append(line).Append('\n');
                    continue;
                }
                done.Add(key);
                if (theirs.TryGetValue(key, out var v)) sb.Append(key).Append(" = ").Append(v).Append('\n');
            }
            foreach (var key in wanted.Where(k => !done.Contains(k) && !SettingKeys.Machine.Contains(k) && !IsListed(k, saver)))
                if (theirs.TryGetValue(key, out var v)) sb.Append(key).Append(" = ").Append(v).Append('\n');
            var result = LauncherSettings.Parse(sb.ToString());
            if (items)
            {
                var put = saver.Items.Where(i => wanted.Contains(i.Key)).ToList();
                if (put.Count > 0)
                    // Every item's choice as it is first, so an older launcher's cpu_saver switch keeps counting for the others.
                    result.SetCpuSaverChoices(saver.Items.Select(i => new KeyValuePair<string, bool>(i.Id, CpuSaver.IsOn(i, result)))
                        .Concat(put.Select(i => new KeyValuePair<string, bool>(i.Id, CpuSaver.IsOn(i, preset)))).ToList());
            }
            return result;
        }

        /// <summary>
        /// Every setting of <paramref name="shown"/> put back to <paramref name="preset"/>'s value ("Reset to Recommended"):
        /// this machine's keys, the profile, its preset and the keys this launcher does not know stay as they are.
        /// </summary>
        public static LauncherSettings ResetAll(LauncherSettings shown, LauncherSettings preset, CpuSaver saver = null)
        {
            var unknown = new HashSet<string>(shown.UnknownKeys.Select(kv => kv.Key), StringComparer.OrdinalIgnoreCase);
            var keys = Values(shown, saver).Keys.Concat(Values(preset, saver).Keys).Where(k => !unknown.Contains(k)).ToList();
            return Reset(shown, preset, keys, saver);
        }

        /// <summary>
        /// The profile file for <paramref name="s"/> (already without this machine's values): its schema, its preset, and only
        /// the lines that differ from the preset, in the settings file's order. A preset this launcher does not know (a newer
        /// launcher's) gets every line, so any launcher reads the same values from it.
        /// </summary>
        public static string ProfileText(LauncherSettings s, CpuSaver saver = null)
        {
            var preset = SettingsPresets.Find(s.Preset);
            var sb = new StringBuilder();
            sb.Append("# EternalVR VR settings profile. Written by the launcher: the settings that differ from its preset.\n");
            sb.Append("schema_version = ").Append(LauncherSettings.SchemaVersion.ToString(CultureInfo.InvariantCulture)).Append('\n');
            sb.Append("preset = ").Append(SettingsPresets.NormaliseId(s.Preset) ?? SettingsPresets.RecommendedId).Append('\n');
            var changed = preset == null ? null : Changed(s, preset.Values(), saver);
            bool items = saver != null && saver.Items.Count > 0;
            foreach (var line in Lines(s.Serialize()))
            {
                var key = LauncherSettings.KeyOf(line);
                if (key == null || SettingKeys.Machine.Contains(key) || IsListed(key, saver)) continue;
                if (changed == null || changed.Contains(key)) sb.Append(line.Trim()).Append('\n');
            }
            if (items)
                foreach (var item in saver.Items.Where(i => changed == null || changed.Contains(i.Key)))
                    sb.Append(item.Key).Append(" = ").Append(CpuSaver.IsOn(item, s) ? "on" : "off").Append('\n');
            return sb.ToString();
        }

        /// <summary>
        /// The settings a profile file holds: its lines, and for every setting it does not name its preset's value. A key it
        /// does not name takes the launcher's default when read, which is Recommended's value; another preset's values are
        /// put in (<paramref name="presets"/> finds a preset by id, <see cref="SettingsPresets.Find"/> by default). An older
        /// launcher's full file names every setting it knew.
        /// </summary>
        public static LauncherSettings FromProfileText(string text, Func<string, SettingsPreset> presets = null)
        {
            var s = LauncherSettings.Parse(text);
            var preset = (presets ?? SettingsPresets.Find)(s.Preset);
            if (preset == null || preset.Id == SettingsPresets.RecommendedId) return s;
            var named = LauncherSettings.ReadMap(text);
            var covered = new HashSet<string>(named.Keys, StringComparer.OrdinalIgnoreCase);
            foreach (var kv in OlderKeys)
                if (named.ContainsKey(kv.Key)) covered.UnionWith(kv.Value);
            if (named.ContainsKey("cpu_saver")) covered.UnionWith(Values(preset.Values()).Keys.Where(IsCpuSaverKey));
            var missing = Values(preset.Values()).Keys.Where(k => !covered.Contains(k)).ToList();
            return Reset(s, preset.Values(), missing);
        }

        /// <summary>
        /// A CPU Saver line compared by whether its item is on: an item <paramref name="saver"/> lists, or an older launcher's
        /// switch for all of them. A choice for an item it does not list (a newer launcher's) is a line like any other, kept.
        /// </summary>
        private static bool IsListed(string key, CpuSaver saver) =>
            saver != null && saver.Items.Count > 0
            && (string.Equals(key, "cpu_saver", StringComparison.OrdinalIgnoreCase)
                || saver.Items.Any(i => string.Equals(i.Key, key, StringComparison.OrdinalIgnoreCase)));

        private static bool IsCpuSaverKey(string key) =>
            string.Equals(key, "cpu_saver", StringComparison.OrdinalIgnoreCase)
            || key.StartsWith(LauncherSettings.CpuSaverKeyPrefix, StringComparison.OrdinalIgnoreCase);

        private static IEnumerable<string> Lines(string text) => text.Replace("\r\n", "\n").TrimEnd('\n').Split('\n');

        private static bool Same(string a, string b) => string.Equals(a?.Trim(), b?.Trim(), StringComparison.Ordinal);
    }
}
