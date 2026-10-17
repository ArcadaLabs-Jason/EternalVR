using System;
using System.Collections.Generic;
using System.Linq;

namespace EternalVR.Launcher.Core.Settings
{
    /// <summary>A named set of values every setting starts from (REQ-10): a VR settings profile is a preset plus the player's own changes.</summary>
    public sealed class SettingsPreset
    {
        private readonly Func<LauncherSettings> values;

        public SettingsPreset(string id, string name, Func<LauncherSettings> values)
        {
            Id = SettingsPresets.NormaliseId(id) ?? throw new ArgumentException("not a preset id: " + id, nameof(id));
            Name = name;
            this.values = values;
        }

        /// <summary>The id in the settings files (<c>preset = ...</c>).</summary>
        public string Id { get; }
        /// <summary>The window's name for it.</summary>
        public string Name { get; }

        /// <summary>A new copy of the preset's values, with <see cref="LauncherSettings.Preset"/> set to it.</summary>
        public LauncherSettings Values()
        {
            var s = values();
            s.Preset = Id;
            return s;
        }
    }

    /// <summary>
    /// The presets a VR settings profile can start from. Recommended is the launcher's defaults, the values a new player gets;
    /// the comfort presets of REQ-15 join it once the comfort options are settled (D-048, ROADMAP foundation step 7).
    /// </summary>
    public static class SettingsPresets
    {
        public const string RecommendedId = "recommended";

        public static readonly SettingsPreset Recommended = new SettingsPreset(RecommendedId, "Recommended", () => new LauncherSettings());

        public static readonly IReadOnlyList<SettingsPreset> All = new[] { Recommended };

        /// <summary>The preset with that id (case ignored); null for an id this launcher does not know.</summary>
        public static SettingsPreset Find(string id) =>
            NormaliseId(id) is string n ? All.FirstOrDefault(p => p.Id == n) : null;

        /// <summary>The preset <paramref name="s"/> starts from; Recommended when its id is unknown here.</summary>
        public static SettingsPreset Of(LauncherSettings s) => Find(s?.Preset) ?? Recommended;

        /// <summary>A preset id in lower case: letters, digits, '_' and '-', 1 to 32 of them; null for anything else.</summary>
        public static string NormaliseId(string text)
        {
            var t = (text ?? string.Empty).Trim().ToLowerInvariant();
            if (t.Length == 0 || t.Length > 32) return null;
            return t.All(c => (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-') ? t : null;
        }
    }
}
