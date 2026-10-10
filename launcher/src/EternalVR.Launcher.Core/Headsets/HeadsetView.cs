using System;
using System.Collections.Generic;
using System.Globalization;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;

namespace EternalVR.Launcher.Core.Headsets
{
    /// <summary>
    /// The Play tab's Headset box in words (<c>MainForm.Headset.cs</c>): the headset and its route, its native panel, the size
    /// the runtime asks for and when it was read, and the last session's refresh rate, with the state of what is shown (never
    /// read, read with another runtime than the one set now, or old because the last try found no headset). Also the Each eye
    /// line, Resolution's choices and Frame pacing's matched choice, which name the same route and refresh rate.
    /// </summary>
    public sealed class HeadsetView
    {
        private HeadsetView() { }

        /// <summary>"Meta Quest 3 via Virtual Desktop (VDXR)", or "Not read yet (runtime: SteamVR)".</summary>
        public string HeadsetLine { get; private set; }
        /// <summary>A line under it saying what to do, or that the values are old; null when there is nothing to say.</summary>
        public string State { get; private set; }
        /// <summary>True when <see cref="State"/> says the values shown may no longer be right (the window shows it in amber).</summary>
        public bool Old { get; private set; }
        /// <summary>"2064 x 2208 per eye", "not in our list" or "not known yet".</summary>
        public string PanelLine { get; private set; }
        /// <summary>"VD asks for", "SteamVR asks for", "The runtime asks for".</summary>
        public string AsksLabel { get; private set; }
        /// <summary>"2496 x 2688 per eye (1.21x the panel)", then on a second line "read at Launch VR, today 09:12".</summary>
        public string AsksLine { get; private set; }
        /// <summary>"90 Hz, steady (last session)", or "shown after your first session".</summary>
        public string RefreshLine { get; private set; }
        public string PanelTip { get; private set; }
        public string AsksTip { get; private set; }
        public string RefreshTip { get; private set; }

        /// <summary>What the state line adds when a value is old or missing: the headset is read at every launch.</summary>
        public const string ReadAtLaunch = "Read again at Launch VR.";

        /// <summary>
        /// The box for <paramref name="facts"/> (never null) and their <paramref name="identity"/>, with the runtime manifest the
        /// next launch will use (<paramref name="currentManifest"/>, and its <c>runtime.name</c>; either null when none is set).
        /// <paramref name="notReadAtStart"/>: why the launcher did not read the headset when it opened (<see cref="HeadsetAutoRead"/>),
        /// null when it did or tried; <paramref name="reading"/>: a read runs now.
        /// </summary>
        public static HeadsetView For(HeadsetFacts facts, HeadsetIdentity identity, string currentManifest, string currentRuntimeName, DateTime now,
            string notReadAtStart = null, bool reading = false)
        {
            facts = facts ?? new HeadsetFacts();
            identity = identity ?? HeadsetIdentity.Identify(facts.RuntimeName, facts.SystemName, null, HeadsetTable.Empty);
            var current = HeadsetIdentity.RouteOf(currentRuntimeName);
            var currentName = current == RouteKind.Unknown ? null : HeadsetIdentity.RouteNameOf(current, currentRuntimeName);
            bool read = facts.Limits != null || facts.RuntimeName != null;
            bool failedSince = facts.FailedAt.HasValue && (!facts.ReadAt.HasValue || facts.FailedAt > facts.ReadAt);
            var v = new HeadsetView();
            var failed = failedSince
                ? "the last try (" + When(facts.FailedAt.Value, now) + ") failed: " + (facts.FailedReason ?? "unknown") + ". " + ReadAtLaunch
                : null;
            var skipped = notReadAtStart == null ? null : "not read when the launcher opened (" + notReadAtStart + "). " + ReadAtLaunch;

            if (!read)
            {
                v.HeadsetLine = "Not read yet" + (currentName == null ? string.Empty : " (runtime: " + currentName + ")");
                v.State = failed != null ? HeadsetIdentity.Capitalised(failed)
                    : notReadAtStart != null ? "Not read when the launcher opened (" + notReadAtStart + "). Read at Launch VR."
                    : "Read at Launch VR, or with Detect again.";
                v.Old = failed != null;
            }
            else if (facts.RuntimeName == null)
            {
                // A file from launcher 0.1.11 or older: only the sizes.
                v.HeadsetLine = "Not named yet (read by an older version of the launcher)";
                v.State = failed != null ? "Old values: " + failed : skipped != null ? "Old values: " + skipped
                    : "Named the next time it is read.";
                v.Old = failed != null || skipped != null;
            }
            else
            {
                v.HeadsetLine = identity.Describe();
                bool otherRuntime = facts.RuntimeManifest != null && currentManifest != null
                    && !string.Equals(Normalised(facts.RuntimeManifest), Normalised(currentManifest), StringComparison.OrdinalIgnoreCase);
                if (failed != null) v.State = "Old values: " + failed;
                else if (otherRuntime)
                    v.State = "Last read with " + identity.RouteName + "; the runtime is now " + (currentName ?? "another one") + ". " + ReadAtLaunch;
                else if (skipped != null) v.State = "Old values: " + skipped;
                v.Old = v.State != null;
            }
            if (reading)
            {
                v.State = "Reading the headset from " + (currentName ?? "its runtime") + "...";
                v.Old = false;
            }

            var panel = identity?.Known;
            if (panel != null)
            {
                v.PanelLine = Spaced(panel.Panel) + " per eye"
                    + (identity.Model != null && identity.Model.StartsWith(panel.Name, StringComparison.Ordinal) ? string.Empty : " (" + panel.Name + ")");
            }
            else v.PanelLine = identity?.ModelKnown == true || identity?.TrackingSystem != null ? "not in our list" : "not known yet";

            v.AsksLabel = identity?.AsksLabel ?? "The runtime asks for";
            if (facts.Limits != null)
            {
                var asked = facts.Limits.Recommended;
                var ratio = panel == null ? string.Empty
                    : " (" + ((double)asked.Width / panel.Panel.Width).ToString("0.00", CultureInfo.InvariantCulture) + "x the panel)";
                var when = facts.ReadAt.HasValue
                    ? "read " + ReadByText(facts.ReadBy) + ", " + When(facts.ReadAt.Value, now)
                    : "read at an earlier Launch VR";
                v.AsksLine = Spaced(asked) + " per eye" + ratio + "\n" + when + (v.Old ? " (old)" : string.Empty);
            }
            else v.AsksLine = "not read yet";

            if (facts.SessionRefresh != null)
            {
                var sessionRoute = HeadsetIdentity.RouteOf(facts.SessionRuntime);
                bool otherRoute = identity != null && sessionRoute != RouteKind.Unknown && identity.Route != RouteKind.Unknown && sessionRoute != identity.Route;
                v.RefreshLine = facts.SessionRefresh + " (last session"
                    + (otherRoute ? ", with " + HeadsetIdentity.RouteNameOf(sessionRoute, facts.SessionRuntime) : string.Empty) + ")";
            }
            else v.RefreshLine = "shown after your first session";

            var places = Places(identity.Route != RouteKind.Unknown ? identity.Route : current);
            var asker = identity?.Asker ?? "the runtime";
            v.PanelTip = panel != null
                ? "Your headset's own screen per eye, from the launcher's list of headsets (no runtime reports it)."
                : "This headset is not in the launcher's list of headsets (or not named), so Resolution has no headset native choice.";
            v.AsksTip = "The size " + asker + " asks the game to render each eye at, set in " + places.Size + places.Untested + ".";
            v.RefreshTip = "Your headset's refresh rate in your last session, set in " + places.Refresh + places.Untested + ". When "
                + places.Smoothing + " takes over, the headset asks for half the frames (or a third).";
            return v;
        }

        /// <summary>
        /// The Each eye line for these settings: the size each eye renders at, per side against what the runtime asks for and the
        /// native panel; with DLSS, about what it draws; past a quarter more pixels than Auto at 1.00, how many more. Lines are
        /// separated by '\n'. When the last session rendered below its plan (<paramref name="cap"/>: the graphics driver held
        /// each eye at the window's size), it first says what that session really got, then the plan as "Planned: ...".
        /// </summary>
        public static string EachEye(LauncherSettings s, HeadsetFacts facts, HeadsetIdentity identity, RenderCap cap = null)
        {
            if (s.Mode != VrMode.Stereo) return "The game's own resolution (mono)";
            var setting = LauncherSettings.NormaliseRenderSize(s.RenderSize) ?? LauncherSettings.RenderSizeAuto;
            if (setting == LauncherSettings.RenderSizeOff) return "The game window's size (render_size off)";
            var plan = Plan(s, setting, facts, identity, out bool sized);
            if (cap == null || !cap.Capped) return plan;
            return cap.EachEyeText() + "\n" + (sized ? "Planned: " : string.Empty) + plan;
        }

        // The Each eye line's plan for these settings; sized: it starts with the size.
        private static string Plan(LauncherSettings s, string setting, HeadsetFacts facts, HeadsetIdentity identity, out bool sized)
        {
            sized = false;
            var limits = facts?.Limits;
            var probe = limits == null ? null : new OpenXrProbeResult { Limits = limits };
            var panel = identity?.Known?.Panel;
            var choice = RenderSizeChoice.Decide(setting, s.RenderScale, probe, s.ResolutionBase, panel);
            if (choice.Size == null)
                return limits == null ? "Set from your headset at Launch VR" : "Decided in-game (too large for a fixed size)";
            var size = choice.Size.Value;
            sized = true;
            if (!choice.IsAuto) return Spaced(size) + " (fixed size, render_size in launcher.ini)";

            var lines = new List<string>();
            var first = Spaced(size) + "   " + Percent(size.Width, limits.Recommended.Width) + " of what " + (identity?.Asker ?? "the runtime") + " asks for";
            if (panel != null) first += ", " + Percent(size.Width, panel.Value.Width) + " of the native panel";
            lines.Add(first);
            if (s.ResolutionBase == ResolutionBase.Panel && choice.Base != ResolutionBase.Panel)
                lines.Add("This headset's panel is not known: Auto is used");
            if (s.AntiAliasing == AntiAliasingMode.Dlss)
            {
                int q = Math.Max(0, Math.Min(DlssDll.QualityNames.Length - 1, (int)s.Dlss));
                double f = DlssDll.QualityFactors[q];
                var drawn = new Extent((uint)Math.Round(size.Width * f, MidpointRounding.AwayFromZero), (uint)Math.Round(size.Height * f, MidpointRounding.AwayFromZero));
                lines.Add((s.Dlss == DlssQuality.Dlaa ? "DLAA" : "DLSS " + DlssDll.QualityNames[q]) + " draws about " + Spaced(drawn));
            }
            var auto = RenderSizeChoice.Decide(setting, 1.0, probe).Size;
            if (auto != null && !auto.Value.IsEmpty)
            {
                double extra = (double)size.Width * size.Height / ((double)auto.Value.Width * auto.Value.Height) - 1.0;
                if (extra > MoreThanAutoNote)
                    lines.Add(Math.Round(extra * 100, MidpointRounding.AwayFromZero).ToString("0", CultureInfo.InvariantCulture)
                        + "% more pixels than Auto");
            }
            return string.Join("\n", lines);
        }

        /// <summary>
        /// The headset as read and the last session, for the report's system.txt: what the Headset box shows, with the runtime's
        /// and the system's names as the runtime gave them, the limits, when and with which runtime manifest they were read.
        /// </summary>
        public static IReadOnlyList<KeyValuePair<string, string>> ReportLines(HeadsetFacts facts, HeadsetIdentity identity)
        {
            facts = facts ?? new HeadsetFacts();
            identity = identity ?? HeadsetIdentity.Identify(facts.RuntimeName, facts.SystemName, null, HeadsetTable.Empty);
            var lines = new List<KeyValuePair<string, string>>();
            void Add(string key, string value) => lines.Add(new KeyValuePair<string, string>(key, value));
            string Time(DateTime? t) => t?.ToString("yyyy-MM-dd HH:mm:ss", CultureInfo.InvariantCulture);

            if (facts.Limits == null && facts.RuntimeName == null) Add("headset", "not read yet");
            else
            {
                Add("headset", facts.RuntimeName == null ? "not named (read by an older launcher)" : identity.Describe());
                Add("headset as read", "runtime '" + (facts.RuntimeName ?? "unknown") + "', system '" + (facts.SystemName ?? "unknown") + "'");
                Add("headset native panel", identity.Known == null ? "not in the list" : identity.Known.Name + ", " + identity.Known.Panel + " per eye");
                if (facts.Limits != null)
                    Add("headset asks for", facts.Limits.Recommended + " per eye (max image " + facts.Limits.MaxImageRect + ", max swapchain "
                        + facts.Limits.MaxSwapchain + ")");
                Add("headset read", (Time(facts.ReadAt) ?? "at an earlier launch") + (facts.ReadAt.HasValue ? " " + ReadByText(facts.ReadBy) : string.Empty)
                    + (facts.RuntimeManifest == null ? string.Empty : " with " + facts.RuntimeManifest));
            }
            if (facts.FailedAt.HasValue) Add("headset last failed try", Time(facts.FailedAt) + ": " + (facts.FailedReason ?? "unknown"));
            if (facts.Session == null) Add("last session", "none with a display period in its log");
            else
            {
                Add("last session", facts.Session + (facts.SessionAt.HasValue ? ", ended " + Time(facts.SessionAt) : string.Empty)
                    + (facts.SessionRuntime == null ? string.Empty : ", runtime '" + facts.SessionRuntime + "'")
                    + (facts.SessionRefresh == null ? string.Empty : ": " + facts.SessionRefresh));
                if (facts.SessionText != null) Add("last session summary", facts.SessionText);
                Add("last session controls", facts.SessionControls ?? "not logged");
                if (facts.SessionParallelEyes != null) Add("last session parallel eyes", facts.SessionParallelEyes);
            }
            return lines;
        }

        /// <summary>The Each eye line says the share of pixels more than Auto at 1.00 past this one.</summary>
        public const double MoreThanAutoNote = 0.25;

        /// <summary>
        /// Resolution's choices in the window, each with its base: Auto, the runtime's native size ("Virtual Desktop native"), and
        /// the headset's ("Quest 3 native") only when its model is known (or the setting is the headset's already, which then
        /// says Auto is used). <paramref name="currentRuntimeName"/>: the <c>runtime.name</c> of the runtime the next launch uses.
        /// </summary>
        public static IReadOnlyList<KeyValuePair<ResolutionBase, string>> ResolutionChoices(HeadsetIdentity identity, ResolutionBase current,
            string currentRuntimeName = null)
        {
            var texts = SettingTexts.For(Setting.Resolution).Choices;
            var list = new List<KeyValuePair<ResolutionBase, string>>
            {
                new KeyValuePair<ResolutionBase, string>(ResolutionBase.Auto, texts[(int)ResolutionBase.Auto]),
                new KeyValuePair<ResolutionBase, string>(ResolutionBase.Ask, RuntimeChoice(identity, currentRuntimeName)),
            };
            var native = NativeChoice(identity);
            if (native != null) list.Add(new KeyValuePair<ResolutionBase, string>(ResolutionBase.Panel, native));
            else if (current == ResolutionBase.Panel)
                list.Add(new KeyValuePair<ResolutionBase, string>(ResolutionBase.Panel, texts[(int)ResolutionBase.Panel] + " (not known: Auto is used)"));
            return list;
        }

        /// <summary>
        /// Resolution's choice of the size the runtime asks for, "Virtual Desktop native", "SteamVR native", "Quest Link native":
        /// for the runtime the next launch uses, else the one that last answered; "Runtime native" when neither is known.
        /// </summary>
        public static string RuntimeChoice(HeadsetIdentity identity, string currentRuntimeName) => RuntimeName(identity, currentRuntimeName, out _) + " native";

        /// <summary>
        /// Resolution's choice of the headset's own panel, named after it ("Quest 3 native", "Index native", "Steam Frame
        /// native"); null when the launcher does not know the model's panel (a SteamVR tracking system alone names none).
        /// </summary>
        public static string NativeChoice(HeadsetIdentity identity) => identity?.Known == null ? null : ShortModel(identity.Known.Name) + " native";

        /// <summary>
        /// The Resolution list's own tooltip: the runtime's native size is what it asks for at its current quality setting, not
        /// the panel; the headset's native size is its panel, or there is none.
        /// </summary>
        public static string ResolutionTip(HeadsetIdentity identity, string currentRuntimeName)
        {
            var runtime = RuntimeName(identity, currentRuntimeName, out var route);
            var tip = runtime + " native: what " + (route == RouteKind.Unknown ? "the runtime" : runtime) + " asks for at its current "
                + "quality setting (" + QualitySetting(route) + "), not the panel.";
            var native = NativeChoice(identity);
            return native != null
                ? tip + " " + native + ": your headset's own panel, " + Spaced(identity.Known.Panel) + " per eye."
                : tip + " No headset native choice: the launcher does not know your headset's model.";
        }

        /// <summary>The runtime's own setting of the size it asks for, in a few words.</summary>
        private static string QualitySetting(RouteKind route)
        {
            switch (route)
            {
                case RouteKind.VirtualDesktop: return "Virtual Desktop's VR Graphics Quality";
                case RouteKind.MetaLink: return "the Link app's render resolution";
                case RouteKind.SteamVr: return "SteamVR's resolution slider";
                case RouteKind.PimaxPlay: return "Pimax Play's render quality";
                case RouteKind.VarjoBase: return "Varjo Base's resolution quality";
                default: return "the runtime's own setting";
            }
        }

        /// <summary>The runtime's name in Resolution's choice: "Virtual Desktop", "Quest Link", "SteamVR", or its own name.</summary>
        private static string RuntimeName(HeadsetIdentity identity, string currentRuntimeName, out RouteKind route)
        {
            route = HeadsetIdentity.RouteOf(currentRuntimeName);
            var name = currentRuntimeName;
            if (route == RouteKind.Unknown && identity != null)
            {
                route = identity.Route;
                name = identity.RouteName;
            }
            switch (route)
            {
                case RouteKind.VirtualDesktop: return "Virtual Desktop";
                case RouteKind.MetaLink: return "Quest Link";
                case RouteKind.SteamVr: return "SteamVR";
                case RouteKind.PimaxPlay: return "Pimax Play";
                case RouteKind.VarjoBase: return "Varjo Base";
                case RouteKind.Other: return string.IsNullOrWhiteSpace(name) ? "Runtime" : name.Trim();
                default: return "Runtime";
            }
        }

        /// <summary>Makers left out of a native choice's name, whose models go by their own name ("Quest 3", "Index", "Vive Pro").</summary>
        private static readonly string[] Makers = { "Meta ", "Oculus ", "Valve ", "HTC " };

        /// <summary>"Quest 3" for "Meta Quest 3"; other names as they are.</summary>
        private static string ShortModel(string name)
        {
            foreach (var maker in Makers)
                if (name.StartsWith(maker, StringComparison.Ordinal) && name.Length > maker.Length) return name.Substring(maker.Length);
            return name;
        }

        /// <summary>Frame pacing's matched choice, with the last session's refresh rate when there was one.</summary>
        public static string PacingChoice(HeadsetFacts facts)
        {
            var hz = facts?.SessionRefreshHz;
            return hz.HasValue
                ? "Matched to the headset (" + hz.Value.ToString(CultureInfo.InvariantCulture) + " Hz last session)"
                : SettingTexts.For(Setting.FramePacing).Choices[(int)FramePacing.Headset];
        }

        /// <summary>The status line at start: the last session's sentences; null without them.</summary>
        public static string LastSessionStatus(HeadsetFacts facts, DateTime now)
        {
            if (facts?.SessionText == null) return null;
            return "Last session" + (facts.SessionAt.HasValue ? " (" + When(facts.SessionAt.Value, now) + ")" : string.Empty) + ": " + facts.SessionText;
        }

        /// <summary>"at Launch VR", "by Detect again", "when the launcher opened".</summary>
        private static string ReadByText(HeadsetReadBy by) =>
            by == HeadsetReadBy.Detect ? "by Detect again" : by == HeadsetReadBy.Start ? "when the launcher opened" : "at Launch VR";

        /// <summary>"today 09:12", "yesterday 21:40", "28 Sep 21:40", "28 Sep 2025 21:40".</summary>
        public static string When(DateTime t, DateTime now)
        {
            var time = t.ToString("HH:mm", CultureInfo.InvariantCulture);
            if (t.Date == now.Date) return "today " + time;
            if (t.Date == now.Date.AddDays(-1)) return "yesterday " + time;
            return t.ToString(t.Year == now.Year ? "d MMM" : "d MMM yyyy", CultureInfo.InvariantCulture) + " " + time;
        }

        /// <summary>"2496 x 2688".</summary>
        public static string Spaced(Extent e) =>
            e.Width.ToString(CultureInfo.InvariantCulture) + " x " + e.Height.ToString(CultureInfo.InvariantCulture);

        private static string Percent(uint part, uint whole) =>
            Math.Round(100.0 * part / whole, MidpointRounding.AwayFromZero).ToString("0", CultureInfo.InvariantCulture) + "%";

        private static string Normalised(string path) => path.Trim().Replace('/', '\\').TrimEnd('\\');

        /// <summary>Where the route sets the size, the refresh rate and its smoothing, for the tooltips.</summary>
        private static (string Size, string Refresh, string Smoothing, string Untested) Places(RouteKind route)
        {
            const string Untested = " (untested with EternalVR so far)";
            switch (route)
            {
                case RouteKind.VirtualDesktop:
                    return ("Virtual Desktop's VR Graphics Quality, in the headset", "Virtual Desktop's Frame rate, in the headset", "Virtual Desktop's SSW", string.Empty);
                case RouteKind.MetaLink:
                    return ("the Meta Horizon Link app (Devices, your headset, Graphics preferences, Render resolution)",
                        "the same page's Refresh rate", "Meta's ASW", Untested);
                case RouteKind.SteamVr:
                    return ("SteamVR's Settings (Video, Render resolution, for all games or per game)", "SteamVR's Settings (Video, Refresh rate)",
                        "SteamVR's Motion Smoothing or its throttling", string.Empty);
                case RouteKind.PimaxPlay:
                    return ("Pimax Play (Render quality)", "Pimax Play (Refresh rate)", "Pimax's Smart Smoothing", Untested);
                case RouteKind.VarjoBase:
                    return ("Varjo Base (Resolution quality)", "Varjo Base", "Varjo Base's motion reprojection", Untested);
                default:
                    return ("the runtime's own settings", "the runtime's own settings", "the runtime's own smoothing", Untested);
            }
        }
    }
}
