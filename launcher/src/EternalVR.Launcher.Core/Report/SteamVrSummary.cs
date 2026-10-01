using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using EternalVR.Launcher.Core.Game;
using EternalVR.Launcher.Core.Text;

namespace EternalVR.Launcher.Core.Report
{
    /// <summary>
    /// A few chosen settings from SteamVR's settings file (<c>config\steamvr.vrsettings</c> in Steam's folder) for
    /// <see cref="ReportManifest.SystemFile"/>: the headset SteamVR last saw, supersampling, motion smoothing, any refresh
    /// rate set, and SteamVR's settings of its own for DOOM Eternal. Only the keys named here are read; everything else in
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
            if (string.IsNullOrWhiteSpace(steamRoot)) return One(Key, "Steam's folder not found");
            var path = Path.Combine(steamRoot, RelativePath);
            try
            {
                if (!File.Exists(path)) return One(Key, "none (no " + RelativePath + " in Steam's folder)");
                using (var s = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
                using (var r = new StreamReader(s))
                    return Summarize(r.ReadToEnd());
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException)
            {
                return One(Key, "could not be read (" + e.GetType().Name + ")");
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
