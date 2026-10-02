using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text.RegularExpressions;
using EternalVR.Launcher.Core.Headsets;

namespace EternalVR.Launcher.Core
{
    /// <summary>
    /// What a VR session's headset really did, from the layer's log: the refresh rate, the time the runtime held the game to
    /// a half or a third of it (Virtual Desktop's SSW, Meta's ASW, SteamVR's Motion Smoothing or throttling), refresh rate
    /// changes, and the game's new frames a second against it, with what to change named for the route.
    /// The layer logs the runtime's display period every 10 s (<c>xr: N frame(s), ...; display period 11.11 ms</c>): the
    /// refresh rate is the shortest period the session held (a period seen in two windows or more, or the first frame's), a
    /// window at 2 to 6 times it was held to a part of it, and a steady period that is no such multiple is another refresh
    /// rate. The period is a snapshot of the last frame of each window, so the shares are of windows, not of exact time.
    /// </summary>
    public sealed class SessionSummary
    {
        /// <summary>A game this close to the refresh rate (its share) kept up.</summary>
        public const double KeptUpShare = 0.95;

        private static readonly Regex RuntimeLine = new Regex(@"\] xr: runtime '(?<name>[^']*)'", RegexOptions.CultureInvariant);
        private static readonly Regex SystemLine = new Regex(@"\] xr: system '(?<name>.*)', max swapchain", RegexOptions.CultureInvariant);
        private static readonly Regex PeriodLine = new Regex(
            @"\] xr: (?<frames>[0-9]+) frame\(s\), .*display period (?<period>[0-9.]+) ms", RegexOptions.CultureInvariant);
        private static readonly Regex RatesLine = new Regex(
            @"\] rates: game [0-9.]+ present\(s\)/s, [0-9.]+ tick\(s\)/s, (?<pairs>[0-9.]+) stereo pair\(s\)/s shown", RegexOptions.CultureInvariant);

        private sealed class Window
        {
            public int Hz;
            public double? Pairs;
        }

        private SessionSummary() { }

        /// <summary>The runtime's name as the layer logged it (<c>VirtualDesktopXR</c>); null when not logged.</summary>
        public string RuntimeName { get; private set; }
        /// <summary>The system's name as the layer logged it; null when not logged.</summary>
        public string SystemName { get; private set; }
        public RouteKind Route { get; private set; }
        /// <summary>The headset's refresh rate in Hz (the shortest period the session held).</summary>
        public int RefreshHz { get; private set; }
        /// <summary>The 10 s windows counted: those in play when there were enough, else all of them after the first frame.</summary>
        public int Windows { get; private set; }
        /// <summary>True when <see cref="Windows"/> are windows in play (the game drew new frames), false for the whole session.</summary>
        public bool InPlay { get; private set; }
        /// <summary>The share (0 to 1) of the windows at 2 to 6 times the refresh period: the runtime held the game to a part of it.</summary>
        public double HeldShare { get; private set; }
        /// <summary>The rate it was held to most often (a half or a third of the refresh rate); 0 when never.</summary>
        public int HeldHz { get; private set; }
        /// <summary>Other refresh rates the headset held for two windows or more, with their windows, most first.</summary>
        public IReadOnlyList<KeyValuePair<int, int>> OtherRates { get; private set; } = new KeyValuePair<int, int>[0];
        /// <summary>The game's new pairs of eye images a second while the headset ran at its refresh rate (the median of those
        /// windows in play); null without enough play.</summary>
        public double? GameRate { get; private set; }

        /// <summary>The summary of a layer log's lines; null when it holds no display period (no VR session).</summary>
        public static SessionSummary FromLines(IEnumerable<string> lines)
        {
            var s = new SessionSummary();
            var windows = new List<Window>();
            int? firstHz = null;
            foreach (var line in lines ?? Enumerable.Empty<string>())
            {
                if (line == null) continue;
                var p = PeriodLine.Match(line);
                if (p.Success)
                {
                    if (!double.TryParse(p.Groups["period"].Value, NumberStyles.Float, CultureInfo.InvariantCulture, out var ms) || ms <= 0.5) continue;
                    int hz = (int)Math.Round(1000.0 / ms, MidpointRounding.AwayFromZero);
                    // The line of the first frame (its running total is 1): the period the runtime started with, not a window.
                    if (long.TryParse(p.Groups["frames"].Value, NumberStyles.Integer, CultureInfo.InvariantCulture, out var frames) && frames <= 1)
                    {
                        if (firstHz == null) firstHz = hz;
                        continue;
                    }
                    windows.Add(new Window { Hz = hz });
                    continue;
                }
                var r = RatesLine.Match(line);
                if (r.Success)
                {
                    // The rates line follows its window's period line.
                    if (windows.Count > 0 && windows[windows.Count - 1].Pairs == null
                        && double.TryParse(r.Groups["pairs"].Value, NumberStyles.Float, CultureInfo.InvariantCulture, out var pairs))
                        windows[windows.Count - 1].Pairs = pairs;
                    continue;
                }
                var rt = RuntimeLine.Match(line);
                if (rt.Success && s.RuntimeName == null) { s.RuntimeName = rt.Groups["name"].Value; continue; }
                var sy = SystemLine.Match(line);
                if (sy.Success && s.SystemName == null) s.SystemName = sy.Groups["name"].Value;
            }
            if (windows.Count == 0 && firstHz == null) return null;
            s.Route = HeadsetIdentity.RouteOf(s.RuntimeName);

            // The refresh rate: the highest rate held for two windows or more, or the first frame's.
            var counts = windows.GroupBy(w => w.Hz).ToDictionary(g => g.Key, g => g.Count());
            var steady = counts.Where(kv => kv.Value >= 2).Select(kv => kv.Key).ToList();
            if (firstHz.HasValue) steady.Add(firstHz.Value);
            s.RefreshHz = steady.Count > 0 ? steady.Max() : counts.Keys.Max();

            var play = windows.Where(w => w.Pairs >= SessionRates.PlayThreshold).ToList();
            s.InPlay = play.Count >= SessionRates.MinWindows;
            var counted = s.InPlay ? play : windows;
            s.Windows = counted.Count;
            var held = new Dictionary<int, int>();
            var others = new Dictionary<int, int>();
            foreach (var w in counted)
            {
                if (w.Hz == s.RefreshHz) continue;
                int m = Multiple(s.RefreshHz, w.Hz);
                if (m > 0) held[m] = held.TryGetValue(m, out var n) ? n + 1 : 1;
                else others[w.Hz] = others.TryGetValue(w.Hz, out var o) ? o + 1 : 1;
            }
            if (s.Windows > 0 && held.Count > 0)
            {
                s.HeldShare = (double)held.Values.Sum() / s.Windows;
                int most = held.OrderByDescending(kv => kv.Value).ThenBy(kv => kv.Key).First().Key;
                s.HeldHz = (int)Math.Round((double)s.RefreshHz / most, MidpointRounding.AwayFromZero);
            }
            s.OtherRates = others.Where(kv => kv.Value >= 2).OrderByDescending(kv => kv.Value).ThenByDescending(kv => kv.Key).ToList();

            var atRefresh = play.Where(w => w.Hz == s.RefreshHz).Select(w => w.Pairs.Value).OrderBy(v => v).ToList();
            if (atRefresh.Count < SessionRates.MinWindows) atRefresh = s.InPlay ? play.Select(w => w.Pairs.Value).OrderBy(v => v).ToList() : new List<double>();
            if (atRefresh.Count > 0) s.GameRate = Median(atRefresh);
            return s;
        }

        /// <summary>The summary of the layer logs in a session's log folder; null when there is none.</summary>
        public static SessionSummary FromSessionDir(string logDir) => FromLines(SessionRates.ReadLayerLogs(logDir));

        /// <summary>2 to 6 when <paramref name="hz"/> is the refresh rate divided by that (within 5%), else 0.</summary>
        private static int Multiple(int refreshHz, int hz)
        {
            if (hz <= 0) return 0;
            double ratio = (double)refreshHz / hz;
            int m = (int)Math.Round(ratio, MidpointRounding.AwayFromZero);
            return m >= 2 && m <= 6 && Math.Abs(ratio - m) < 0.05 * m ? m : 0;
        }

        /// <summary>"of play" or "of the session".</summary>
        private string Scope => InPlay ? "of play" : "of the session";

        /// <summary>The held share as a whole percent, at least 1 when there was any.</summary>
        private string HeldPercent => Math.Max(1, (int)Math.Round(HeldShare * 100, MidpointRounding.AwayFromZero)).ToString(CultureInfo.InvariantCulture) + "%";

        /// <summary>The Play tab's Refresh value: "90 Hz, steady", "144 Hz, throttled to 72 for 18% of play", "varied 72-144 Hz, mostly 144".</summary>
        public string Compact()
        {
            if (OtherRates.Count > 0)
            {
                var all = OtherRates.Select(kv => kv.Key).Concat(new[] { RefreshHz }).ToList();
                int refreshWindows = Windows - OtherRates.Sum(kv => kv.Value);
                int mostly = OtherRates[0].Value > refreshWindows ? OtherRates[0].Key : RefreshHz;
                return string.Format(CultureInfo.InvariantCulture, "varied {0}-{1} Hz, mostly {2}", all.Min(), all.Max(), mostly);
            }
            if (HeldShare > 0)
                return string.Format(CultureInfo.InvariantCulture, "{0} Hz, {1} to {2} for {3} {4}", RefreshHz, Route == RouteKind.SteamVr ? "throttled" : "held",
                    HeldHz, HeldPercent, Scope);
            return RefreshHz.ToString(CultureInfo.InvariantCulture) + " Hz, steady";
        }

        /// <summary>The sentences for the status line after the session (and the report), named for the route.</summary>
        public string Describe()
        {
            var parts = new List<string>();
            if (OtherRates.Count > 0)
                parts.Add(string.Format(CultureInfo.InvariantCulture, "The headset ran at {0} Hz, and at {1}.", RefreshHz,
                    string.Join(" and at ", OtherRates.Select(kv => kv.Key.ToString(CultureInfo.InvariantCulture) + " Hz for " + Duration(kv.Value)))));
            if (HeldShare > 0) parts.Add(HeldSentence());
            if (GameRate.HasValue) parts.Add(RateSentence(GameRate.Value));
            return string.Join(" ", parts);
        }

        private string HeldSentence()
        {
            var amount = string.Format(CultureInfo.InvariantCulture, "{0} of {1} Hz for {2} {3}", HeldHz, RefreshHz, HeldPercent, Scope);
            switch (Route)
            {
                case RouteKind.VirtualDesktop: return "Virtual Desktop's SSW held the game to " + amount + ".";
                case RouteKind.MetaLink: return "Meta's ASW held the game to " + amount + ".";
                case RouteKind.SteamVr: return "SteamVR throttled the game to " + amount + " (Motion Smoothing or throttling).";
                case RouteKind.PimaxPlay: return "Pimax's Smart Smoothing held the game to " + amount + ".";
                default: return "The runtime held the game to " + amount + " (its reprojection or throttling).";
            }
        }

        private string RateSentence(double pairs)
        {
            var rate = string.Format(CultureInfo.InvariantCulture, "about {0:0} new frames a second at {1} Hz", pairs, RefreshHz);
            if (pairs >= KeptUpShare * RefreshHz) return "The game kept up with your headset: " + rate + ".";
            return "The game drew " + rate + ".";
        }

        /// <summary>"40 seconds", "about 2 minutes".</summary>
        private static string Duration(int windows)
        {
            int seconds = windows * 10;
            if (seconds < 90) return seconds.ToString(CultureInfo.InvariantCulture) + " seconds";
            int minutes = (int)Math.Round(seconds / 60.0, MidpointRounding.AwayFromZero);
            return "about " + minutes.ToString(CultureInfo.InvariantCulture) + " minutes";
        }

        /// <summary>The numbers for the launcher log.</summary>
        public string LogText() => string.Format(CultureInfo.InvariantCulture,
            "session refresh: {0} Hz over {1} window(s) of 10 s{2}; held to {3} Hz in {4:0.0}%; other rates {5}; new stereo pairs/s at the refresh rate {6}; runtime '{7}', system '{8}'",
            RefreshHz, Windows, InPlay ? " in play" : " (not enough play: the whole session)", HeldHz, HeldShare * 100,
            OtherRates.Count == 0 ? "none" : string.Join(", ", OtherRates.Select(kv => kv.Key + " Hz x" + kv.Value)),
            GameRate.HasValue ? GameRate.Value.ToString("0.0", CultureInfo.InvariantCulture) : "unknown", RuntimeName, SystemName);

        private static double Median(List<double> sorted)
        {
            int n = sorted.Count;
            return n % 2 == 1 ? sorted[n / 2] : (sorted[n / 2 - 1] + sorted[n / 2]) / 2.0;
        }
    }
}
