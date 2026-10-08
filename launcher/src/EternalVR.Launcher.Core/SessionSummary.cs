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
    /// rate. The period is a snapshot of the last frame of each window, so the shares are of windows, not of exact time: the
    /// layer's time-based summary (<c>xr: refresh summary at session end: base 11.11 ms (90 Hz); 1x 90.6%, 2x 9.3%, ...</c>,
    /// also every 60 s) gives the held share instead when it is logged, and a share of windows from a short session says so.
    /// Also the mod's controller actions the runtime bound (<c>controllers: 0 of 12 action(s) bound</c>) and whether the layer
    /// warned that no hand pose arrived in play (<see cref="UnboundControls"/>), and how often the game's video memory was over
    /// the budget Windows gives it, or near it (<c>vram: the process uses U MB ..., budget B MB ...; ... N of M reading(s) over
    /// the budget</c>, also every 10 s).
    /// </summary>
    public sealed class SessionSummary
    {
        /// <summary>A game this close to the refresh rate (its share) kept up.</summary>
        public const double KeptUpShare = 0.95;
        /// <summary>From this share of the session's vram windows over the budget, the summary says so.</summary>
        public const double VramOverShareToSay = 0.10;
        /// <summary>A vram window using this share of the budget or more is near it.</summary>
        public const double VramNearBudget = 0.95;
        /// <summary>From this share of the session's vram windows near the budget (and the over-budget sentence not said), the
        /// summary says so: a runtime menu opening over the game can then reset the graphics driver (issue #19: SteamVR's
        /// dashboard at 97 to 99% of the budget, fixed by a lower texture pool).</summary>
        public const double VramNearShareToSay = 0.50;
        /// <summary>Fewer 10 s windows than this (10 minutes) make a short session: a share of so few one-frame snapshots can
        /// be far from the time it took (a player's 13 minutes: 16% of the windows, 9.3% of the time).</summary>
        public const int ShortSessionWindows = 60;

        private static readonly Regex RuntimeLine = new Regex(@"\] xr: runtime '(?<name>[^']*)'", RegexOptions.CultureInvariant);
        private static readonly Regex SystemLine = new Regex(@"\] xr: system '(?<name>.*)', max swapchain", RegexOptions.CultureInvariant);
        private static readonly Regex PeriodLine = new Regex(
            @"\] xr: (?<frames>[0-9]+) frame\(s\), .*display period (?<period>[0-9.]+) ms", RegexOptions.CultureInvariant);
        private static readonly Regex RatesLine = new Regex(
            @"\] rates: game [0-9.]+ present\(s\)/s, [0-9.]+ tick\(s\)/s, (?<pairs>[0-9.]+) stereo pair\(s\)/s shown", RegexOptions.CultureInvariant);
        private static readonly Regex RefreshSummaryLine = new Regex(
            @"\] xr: refresh summary(?: at session end)?: base (?<base>[0-9.]+) ms \([^)]*\); (?<shares>[^;]*); [0-9]+ change\(s\) in [0-9.]+ s",
            RegexOptions.CultureInvariant);
        private static readonly Regex MultipleShare = new Regex(@"\b(?<k>[0-9]+)x (?<share>[0-9.]+)%", RegexOptions.CultureInvariant);
        private const string ControllersTag = "] controllers: ";
        private static readonly Regex BoundLine = new Regex(
            @"\] controllers: (?<bound>[0-9]+) of (?<all>[0-9]+) action\(s\) bound", RegexOptions.CultureInvariant);
        private static readonly Regex NoPoseLine = new Regex(@"\] controllers: WARNING no hand pose has been valid", RegexOptions.CultureInvariant);
        private static readonly Regex VramLine = new Regex(
            @"\] vram: the process uses (?<use>[0-9]+) MB of local video memory, budget (?<budget>[0-9]+) MB.*; last 10 s: peak [0-9.]+ MB, (?<over>[0-9]+) of (?<all>[0-9]+) reading\(s\) over the budget",
            RegexOptions.CultureInvariant);

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
        /// <summary>True when <see cref="HeldShare"/> and <see cref="HeldHz"/> are of the session's time, from the layer's last
        /// refresh summary (its base the refresh rate), false when they are of <see cref="Windows"/>.</summary>
        public bool HeldByTime { get; private set; }
        /// <summary>Other refresh rates the headset held for two windows or more, with their windows, most first.</summary>
        public IReadOnlyList<KeyValuePair<int, int>> OtherRates { get; private set; } = new KeyValuePair<int, int>[0];
        /// <summary>The game's new pairs of eye images a second while the headset ran at its refresh rate (the median of those
        /// windows in play); null without enough play.</summary>
        public double? GameRate { get; private set; }
        /// <summary>The mod's controller actions with a source bound to them, from the layer's last <c>N of M action(s) bound</c>
        /// line; null when not logged (the controllers never reported, or an older layer).</summary>
        public int? ControlsBound { get; private set; }
        /// <summary>The controller actions that line counted (M); null when not logged.</summary>
        public int? Controls { get; private set; }
        /// <summary>True when the layer warned that no hand pose became valid in play with the headset tracked.</summary>
        public bool NoHandPose { get; private set; }
        /// <summary>The layer's 10 s vram windows with a reading; 0 when it logged none (no budget, or an older layer).</summary>
        public int VramWindows { get; private set; }
        /// <summary>The share (0 to 1) of <see cref="VramWindows"/> with most of their readings over the budget.</summary>
        public double VramOverShare { get; private set; }
        /// <summary>The share (0 to 1) of <see cref="VramWindows"/> whose last reading used <see cref="VramNearBudget"/> of the
        /// budget or more (over-budget windows included).</summary>
        public double VramNearShare { get; private set; }

        /// <summary>The summary of a layer log's lines; null when it holds no display period (no VR session).</summary>
        public static SessionSummary FromLines(IEnumerable<string> lines)
        {
            var s = new SessionSummary();
            var windows = new List<Window>();
            int? firstHz = null;
            int vramOver = 0;
            int vramNear = 0;
            int? timeHz = null;
            Dictionary<int, double> timeShares = null;
            foreach (var line in lines ?? Enumerable.Empty<string>())
            {
                if (line == null) continue;
                var v = VramLine.Match(line);
                if (v.Success)
                {
                    if (int.TryParse(v.Groups["over"].Value, NumberStyles.None, CultureInfo.InvariantCulture, out var over)
                        && int.TryParse(v.Groups["all"].Value, NumberStyles.None, CultureInfo.InvariantCulture, out var readings) && readings > 0)
                    {
                        s.VramWindows++;
                        if (over * 2 > readings) vramOver++;
                        if (long.TryParse(v.Groups["use"].Value, NumberStyles.None, CultureInfo.InvariantCulture, out var use)
                            && long.TryParse(v.Groups["budget"].Value, NumberStyles.None, CultureInfo.InvariantCulture, out var budget)
                            && budget > 0 && use >= VramNearBudget * budget)
                            vramNear++;
                    }
                    continue;
                }
                if (line.IndexOf(ControllersTag, StringComparison.Ordinal) >= 0)
                {
                    var b = BoundLine.Match(line);
                    if (b.Success && int.TryParse(b.Groups["bound"].Value, NumberStyles.None, CultureInfo.InvariantCulture, out var bound)
                        && int.TryParse(b.Groups["all"].Value, NumberStyles.None, CultureInfo.InvariantCulture, out var all))
                    {
                        s.ControlsBound = bound;
                        s.Controls = all;
                    }
                    else if (NoPoseLine.IsMatch(line)) s.NoHandPose = true;
                    continue;
                }
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
                var rs = RefreshSummaryLine.Match(line);
                if (rs.Success)
                {
                    // The last one counts: each sums up the whole session so far.
                    if (double.TryParse(rs.Groups["base"].Value, NumberStyles.Float, CultureInfo.InvariantCulture, out var baseMs) && baseMs > 0.5)
                    {
                        timeHz = (int)Math.Round(1000.0 / baseMs, MidpointRounding.AwayFromZero);
                        timeShares = new Dictionary<int, double>();
                        foreach (Match m in MultipleShare.Matches(rs.Groups["shares"].Value))
                            if (int.TryParse(m.Groups["k"].Value, NumberStyles.None, CultureInfo.InvariantCulture, out var k)
                                && double.TryParse(m.Groups["share"].Value, NumberStyles.Float, CultureInfo.InvariantCulture, out var share))
                                timeShares[k] = share / 100.0;
                    }
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
            if (s.VramWindows > 0)
            {
                s.VramOverShare = (double)vramOver / s.VramWindows;
                s.VramNearShare = (double)vramNear / s.VramWindows;
            }

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
            // The layer's time-based shares, when its base is the refresh rate found here: a share under half a percent is none.
            if (timeShares != null && timeHz == s.RefreshHz)
            {
                var heldTime = timeShares.Where(kv => kv.Key >= 2 && kv.Key <= 6).ToList();
                double share = heldTime.Sum(kv => kv.Value);
                s.HeldByTime = true;
                s.HeldShare = share >= 0.005 ? share : 0.0;
                s.HeldHz = s.HeldShare > 0
                    ? (int)Math.Round((double)s.RefreshHz / heldTime.OrderByDescending(kv => kv.Value).ThenBy(kv => kv.Key).First().Key, MidpointRounding.AwayFromZero)
                    : 0;
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

        /// <summary>"of play", "of the session" (also the time-based share) or "of a short session".</summary>
        private string Scope => HeldByTime ? "of the session" : Windows < ShortSessionWindows ? "of a short session" : InPlay ? "of play" : "of the session";

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
            if (VramWindows >= SessionRates.MinWindows && VramOverShare >= VramOverShareToSay)
                parts.Add(string.Format(CultureInfo.InvariantCulture,
                    "The game used more video memory than your graphics card had free for {0}% of the session: a lower Resolution, "
                    + "or ray tracing off in the game, makes it smoother.",
                    Math.Max(1, (int)Math.Round(VramOverShare * 100, MidpointRounding.AwayFromZero))));
            else if (VramWindows >= SessionRates.MinWindows && VramNearShare >= VramNearShareToSay)
                parts.Add(string.Format(CultureInfo.InvariantCulture,
                    "The game used nearly all the video memory Windows gives it for {0}% of the session, so a headset menu opening "
                    + "over it (like SteamVR's dashboard) can reset the graphics driver: a lower Texture Pool Size in the game's "
                    + "video settings makes room.",
                    (int)Math.Round(VramNearShare * 100, MidpointRounding.AwayFromZero)));
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

        /// <summary>The controls: "0 of 12 bound, no hand pose in play"; "not logged" when the layer did not count them.</summary>
        public string ControlsText()
        {
            var text = Controls.HasValue
                ? string.Format(CultureInfo.InvariantCulture, "{0} of {1} bound", ControlsBound, Controls)
                : "not logged";
            return NoHandPose ? text + ", no hand pose in play" : text;
        }

        /// <summary>The numbers for the launcher log.</summary>
        public string LogText() => string.Format(CultureInfo.InvariantCulture,
            "session refresh: {0} Hz over {1} window(s) of 10 s{2}; held to {3} Hz in {4:0.0}%{13}; other rates {5}; new stereo pairs/s at the refresh rate {6}; runtime '{7}', system '{8}'; video memory over the budget in {10} of {11} window(s), at 95% of it or more in {12}; controls {9}",
            RefreshHz, Windows, InPlay ? " in play" : " (not enough play: the whole session)", HeldHz, HeldShare * 100,
            OtherRates.Count == 0 ? "none" : string.Join(", ", OtherRates.Select(kv => kv.Key + " Hz x" + kv.Value)),
            GameRate.HasValue ? GameRate.Value.ToString("0.0", CultureInfo.InvariantCulture) : "unknown", RuntimeName, SystemName, ControlsText(),
            (int)Math.Round(VramOverShare * VramWindows, MidpointRounding.AwayFromZero), VramWindows,
            (int)Math.Round(VramNearShare * VramWindows, MidpointRounding.AwayFromZero), HeldByTime ? " of the time (the layer's refresh summary)" : " of the windows");

        private static double Median(List<double> sorted)
        {
            int n = sorted.Count;
            return n % 2 == 1 ? sorted[n / 2] : (sorted[n / 2 - 1] + sorted[n / 2]) / 2.0;
        }
    }
}
