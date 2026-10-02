using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using EternalVR.Launcher.Core.Game;
using EternalVR.Launcher.Core.Text;

namespace EternalVR.Launcher.Core.Report
{
    /// <summary>Where a controller binding SteamVR applies to the game comes from.</summary>
    public enum SteamVrBindingKind
    {
        /// <summary>A Steam Workshop binding (<c>vr-input-workshop://</c>): a community one, or one a player saved.</summary>
        Workshop,
        /// <summary>A binding file on this PC (<c>file:</c>).</summary>
        File,
    }

    /// <summary>
    /// A controller binding chosen in SteamVR for DOOM Eternal's app key (<c>steam.app.782330</c>), which SteamVR applies in
    /// place of the binding it generates from the mod's suggested bindings. The key is shared with every OpenXR mod of the
    /// game, so a binding made for another one names other actions, and then the controllers do nothing in game.
    /// </summary>
    public sealed class SteamVrBinding
    {
        public SteamVrBinding(string controllerType, SteamVrBindingKind kind)
        {
            ControllerType = controllerType;
            Kind = kind;
        }

        /// <summary>SteamVR's controller type, without the numeric suffix some keys carry (<c>knuckles</c>).</summary>
        public string ControllerType { get; }
        public SteamVrBindingKind Kind { get; }

        public override string ToString() => (Kind == SteamVrBindingKind.Workshop ? "workshop binding for " : "binding file for ") + ControllerType;
    }

    /// <summary>
    /// A few chosen settings from SteamVR's settings file (<c>config\steamvr.vrsettings</c> in Steam's folder) for
    /// <see cref="ReportManifest.SystemFile"/>: the headset SteamVR last saw, supersampling, motion smoothing, any refresh
    /// rate set, and SteamVR's settings of its own for DOOM Eternal with the controller bindings chosen there (<see
    /// cref="CustomBindings(object)"/>, also a preflight warning). Only the keys named here are read; everything else in
    /// the file (the headset's serial number, install and pairing IDs among them) never goes into the report.
    /// </summary>
    public static class SteamVrSummary
    {
        public const string Key = "steamvr settings";
        public const string HeadsetKey = "steamvr headset";
        public const string SupersamplingKey = "steamvr supersampling";
        public const string MotionSmoothingKey = "steamvr motion smoothing";
        public const string FilteringKey = "steamvr supersample filtering";
        public const string RefreshRateKey = "steamvr refresh rate";
        public const string AppKey = "steamvr settings for DOOM Eternal";
        public const string BindingsKey = "steamvr controller bindings for DOOM Eternal";

        /// <summary>SteamVR's section for the game's app key: the launch sets <c>SteamAppId</c>, so the OpenXR session runs under it.</summary>
        public const string AppSection = "steam.app." + GameLayout.SteamAppId;
        public const string WorkshopScheme = "vr-input-workshop://";
        public const string CurrentUrlSuffix = "_CurrentURL_openxr";
        public const string AutosaveUrlSuffix = "_AutosaveURL_openxr";

        /// <summary>The longest value written for one key of the game's own section; a longer one is cut short.</summary>
        public const int MaxValueLength = 80;

        private const string NotSet = "not set";

        /// <summary>The settings file below Steam's folder.</summary>
        public static readonly string RelativePath = Path.Combine("config", "steamvr.vrsettings");

        /// <summary>
        /// The summary for Steam's folder <paramref name="steamRoot"/>: one line saying why when Steam's folder is unknown,
        /// the file is not there or it cannot be read.
        /// </summary>
        public static IReadOnlyList<KeyValuePair<string, string>> Read(string steamRoot)
        {
            var text = ReadFile(steamRoot, out var why);
            return text == null ? One(Key, why) : Summarize(text);
        }

        /// <summary>
        /// The controller bindings chosen in SteamVR for the game (<see cref="CustomBindings(string)"/>) in Steam's folder
        /// <paramref name="steamRoot"/>; none when the settings file is not there or cannot be read.
        /// </summary>
        public static IReadOnlyList<SteamVrBinding> ReadCustomBindings(string steamRoot)
        {
            var text = ReadFile(steamRoot, out _);
            return text == null ? new SteamVrBinding[0] : CustomBindings(text);
        }

        /// <summary>The controller bindings chosen in SteamVR for the game in the settings file's text; none when it cannot be read. Never throws.</summary>
        public static IReadOnlyList<SteamVrBinding> CustomBindings(string json)
        {
            try { return CustomBindings(MiniJson.Parse(json)); }
            catch (Exception e) when (e is FormatException || e is OverflowException || e is ArgumentException)
            {
                return new SteamVrBinding[0];
            }
        }

        /// <summary>
        /// The bindings of <see cref="AppSection"/>, one per controller type, in name order: each <c>&lt;type&gt;_CurrentURL_openxr</c>
        /// naming a workshop binding or a binding file, or, for a type without a current one, its
        /// <c>&lt;type&gt;_AutosaveURL_openxr</c>. Without either SteamVR uses the binding it generates from the mod's suggestions.
        /// </summary>
        public static IReadOnlyList<SteamVrBinding> CustomBindings(object root)
        {
            if (!(MiniJson.Get(root, AppSection) is Dictionary<string, object> section)) return new SteamVrBinding[0];
            var current = Urls(section, CurrentUrlSuffix);
            var autosave = Urls(section, AutosaveUrlSuffix);
            var list = new List<SteamVrBinding>();
            foreach (var type in current.Keys.Union(autosave.Keys, StringComparer.OrdinalIgnoreCase).OrderBy(t => t, StringComparer.Ordinal))
            {
                var kind = BindingKind(current.TryGetValue(type, out var url) ? url : autosave[type]);
                var name = Regex.Replace(type, @"_\d+$", string.Empty);
                if (kind.HasValue && !list.Any(b => b.Kind == kind.Value && string.Equals(b.ControllerType, name, StringComparison.OrdinalIgnoreCase)))
                    list.Add(new SteamVrBinding(name, kind.Value));
            }
            return list;
        }

        /// <summary>The non-empty URLs of the keys ending in <paramref name="suffix"/>, by controller type (the key before the suffix).</summary>
        private static Dictionary<string, string> Urls(Dictionary<string, object> section, string suffix)
        {
            var urls = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            foreach (var kv in section)
            {
                if (kv.Key.Length > suffix.Length && kv.Key.EndsWith(suffix, StringComparison.OrdinalIgnoreCase)
                    && kv.Value is string url && url.Trim().Length > 0)
                    urls[kv.Key.Substring(0, kv.Key.Length - suffix.Length)] = url.Trim();
            }
            return urls;
        }

        private static SteamVrBindingKind? BindingKind(string url) =>
            url.StartsWith(WorkshopScheme, StringComparison.OrdinalIgnoreCase) ? SteamVrBindingKind.Workshop
            : url.StartsWith("file:", StringComparison.OrdinalIgnoreCase) ? SteamVrBindingKind.File
            : (SteamVrBindingKind?)null;

        /// <summary>The settings file's text, or null with why (Steam's folder unknown, the file not there or unreadable).</summary>
        private static string ReadFile(string steamRoot, out string why)
        {
            why = null;
            if (string.IsNullOrWhiteSpace(steamRoot))
            {
                why = "Steam's folder not found";
                return null;
            }
            var path = Path.Combine(steamRoot, RelativePath);
            try
            {
                if (!File.Exists(path))
                {
                    why = "none (no " + RelativePath + " in Steam's folder)";
                    return null;
                }
                using (var s = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
                using (var r = new StreamReader(s))
                    return r.ReadToEnd();
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException)
            {
                why = "could not be read (" + e.GetType().Name + ")";
                return null;
            }
        }

        /// <summary>The summary of the settings file's text; one line saying so when it is not a JSON object. Never throws.</summary>
        public static IReadOnlyList<KeyValuePair<string, string>> Summarize(string json)
        {
            object root;
            try { root = MiniJson.Parse(json); }
            catch (Exception e) when (e is FormatException || e is OverflowException || e is ArgumentException)
            {
                return One(Key, "could not be read (" + e.GetType().Name + ": " + e.Message + ")");
            }
            if (!(root is Dictionary<string, object> sections)) return One(Key, "could not be read (not a JSON object)");

            var lines = new List<KeyValuePair<string, string>>();
            void Add(string key, string value) => lines.Add(new KeyValuePair<string, string>(key, value));

            var maker = Text(MiniJson.Get(sections, "LastKnown", "HMDManufacturer"));
            var model = Text(MiniJson.Get(sections, "LastKnown", "HMDModel"));
            var driver = Text(MiniJson.Get(sections, "LastKnown", "ActualHMDDriver"));
            var headset = string.Join(" ", new[] { maker, model }.Where(v => v != null));
            if (driver != null) headset = (headset.Length == 0 ? string.Empty : headset + " ") + "(driver " + driver + ")";
            Add(HeadsetKey, headset.Length == 0 ? NotSet : headset);

            Add(SupersamplingKey, "manual override " + OnOff(MiniJson.Get(sections, "steamvr", "supersampleManualOverride"))
                + ", scale " + (Text(MiniJson.Get(sections, "steamvr", "supersampleScale")) ?? NotSet));
            Add(MotionSmoothingKey, OnOff(MiniJson.Get(sections, "steamvr", "motionSmoothing")));
            Add(FilteringKey, OnOff(MiniJson.Get(sections, "steamvr", "allowSupersampleFiltering")));

            var rates = sections
                .Where(kv => Text(MiniJson.Get(kv.Value, "preferredRefreshRate")) != null)
                .OrderBy(kv => kv.Key, StringComparer.Ordinal)
                .Select(kv => kv.Key + " " + Text(MiniJson.Get(kv.Value, "preferredRefreshRate")))
                .ToList();
            Add(RefreshRateKey, rates.Count == 0 ? NotSet : string.Join(", ", rates));

            var apps = sections
                .Where(kv => kv.Key.IndexOf(GameLayout.SteamAppId, StringComparison.Ordinal) >= 0 && kv.Value is Dictionary<string, object>)
                .OrderBy(kv => kv.Key, StringComparer.Ordinal)
                .ToList();
            if (apps.Count == 0) Add(AppKey, "none");
            foreach (var app in apps)
            {
                var values = ((Dictionary<string, object>)app.Value)
                    .Where(kv => !Private(kv.Key))
                    .OrderBy(kv => kv.Key, StringComparer.Ordinal)
                    .Select(kv => Clip(kv.Key) + " " + (Text(kv.Value) ?? (kv.Value is string ? "(empty)" : "(not a single value)")))
                    .ToList();
                Add(AppKey + " (" + Clip(app.Key) + ")", values.Count == 0 ? "empty" : string.Join(", ", values));
            }
            var bindings = CustomBindings(sections);
            Add(BindingsKey, bindings.Count == 0 ? "none chosen (SteamVR generates them from the mod's suggestions)"
                : "chosen in SteamVR: " + string.Join(", ", bindings));
            return lines;
        }

        /// <summary>A key that might name the headset or the account: never written, even in the game's own section.</summary>
        private static bool Private(string key) =>
            new[] { "serial", "install", "pair", "account", "user" }.Any(w => key.IndexOf(w, StringComparison.OrdinalIgnoreCase) >= 0);

        private static string OnOff(object value) => value is bool b ? (b ? "on" : "off") : Text(value) ?? NotSet;

        /// <summary>A single value as text (numbers in the invariant culture), on one line; null for none, an empty string, an object or an array.</summary>
        private static string Text(object value)
        {
            switch (value)
            {
                case string s: return Clip(s) is string c && c.Length > 0 ? c : null;
                case bool b: return b ? "true" : "false";
                case double d: return d.ToString(CultureInfo.InvariantCulture);
                default: return null;
            }
        }

        private static string Clip(string s)
        {
            var one = s.Replace('\r', ' ').Replace('\n', ' ').Trim();
            return one.Length > MaxValueLength ? one.Substring(0, MaxValueLength) + "..." : one;
        }

        private static IReadOnlyList<KeyValuePair<string, string>> One(string key, string value) =>
            new[] { new KeyValuePair<string, string>(key, value) };
    }
}
