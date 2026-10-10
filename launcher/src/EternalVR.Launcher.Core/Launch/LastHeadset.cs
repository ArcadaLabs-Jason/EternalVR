using System;
using System.Globalization;
using System.IO;
using System.Text;

namespace EternalVR.Launcher.Core.Launch
{
    /// <summary>What read the headset: Launch VR's probe, Detect again, or the launcher's own read when it opened.</summary>
    public enum HeadsetReadBy { Launch, Detect, Start }

    /// <summary>
    /// What the launcher remembers about the headset (<c>headset.txt</c>, <see cref="LastHeadset"/>): the last runtime
    /// probe that answered (its view limits, the runtime's and the system's names, when it was read and with which runtime
    /// manifest), a probe that failed since, and the last session's refresh rate (<see cref="SessionSummary"/>).
    /// </summary>
    public sealed class HeadsetFacts
    {
        /// <summary>The views' limits (<see cref="ViewLimits.EyesSideBySide"/> 1); null before the first probe that answered.</summary>
        public ViewLimits Limits { get; set; }
        /// <summary>The runtime's own name (<c>xrGetInstanceProperties</c>), such as <c>VirtualDesktopXR</c>; null when not known.</summary>
        public string RuntimeName { get; set; }
        /// <summary>The system's name (<c>xrGetSystemProperties</c>): the headset's model on a vendor's runtime, <c>SteamVR/OpenXR :
        /// &lt;tracking system&gt;</c> on SteamVR; null when not known.</summary>
        public string SystemName { get; set; }
        /// <summary>The runtime manifest the probe ran with (the chosen one, else the system's active one); null when not known.</summary>
        public string RuntimeManifest { get; set; }
        /// <summary>When the probe answered (local time); null when not known (a file from an older launcher).</summary>
        public DateTime? ReadAt { get; set; }
        /// <summary>What read it (<c>read_by</c>: launch, detect or start).</summary>
        public HeadsetReadBy ReadBy { get; set; }
        /// <summary>When a probe last failed after the one that answered (the values above are then old); null when none did.</summary>
        public DateTime? FailedAt { get; set; }
        /// <summary>Why it failed (for the window and the report).</summary>
        public string FailedReason { get; set; }

        /// <summary>The last VR session's log folder name; null before the first session with a display period in its log.</summary>
        public string Session { get; set; }
        /// <summary>When that session ended (local time).</summary>
        public DateTime? SessionAt { get; set; }
        /// <summary>The runtime that session ran on, as the layer logged it.</summary>
        public string SessionRuntime { get; set; }
        /// <summary>Its refresh rate in Hz; null when not known.</summary>
        public int? SessionRefreshHz { get; set; }
        /// <summary>The Play tab's Refresh value for it (<see cref="SessionSummary.Compact"/>), such as "90 Hz, steady".</summary>
        public string SessionRefresh { get; set; }
        /// <summary>Its sentences for the status line and the report (<see cref="SessionSummary.Describe"/>).</summary>
        public string SessionText { get; set; }
        /// <summary>Its controls (<see cref="SessionSummary.ControlsText"/>) and, when none was bound, the cause the launcher named
        /// (<see cref="UnboundControls"/>), such as "0 of 12 bound; cause: SteamVR binding for DOOM Eternal".</summary>
        public string SessionControls { get; set; }
        /// <summary>Whether Parallel Eye Rendering ran in it, or fell back and why (<see cref="ParallelEyesRun.Text"/>), such as
        /// "off: not with DLSS"; null when the launch did not ask for it.</summary>
        public string SessionParallelEyes { get; set; }

        public HeadsetFacts Clone() => (HeadsetFacts)MemberwiseClone();
    }

    /// <summary>
    /// The headset's facts from the last runtime probe that answered, kept in <c>headset.txt</c> in the data folder (not
    /// launcher.ini: they are this machine's headset, not a setting, and no profile or Reset touches them). The Play tab reads
    /// them, since the probe itself runs only at Launch VR, at Detect again, or when the launcher opens with the headset's
    /// runtime already running (it can take 30 s, and would start SteamVR). A file from
    /// an older launcher holds only the sizes; the rest is then unknown.
    /// </summary>
    public static class LastHeadset
    {
        /// <summary>The time format of <c>read_at</c> and <c>failed_at</c>.</summary>
        private const string TimeFormat = "yyyy-MM-dd HH:mm:ss";

        /// <summary>The remembered facts; empty (never null) when there are none or the file is unreadable.</summary>
        public static HeadsetFacts Read(string path)
        {
            string text;
            try
            {
                if (!File.Exists(path)) return new HeadsetFacts();
                text = File.ReadAllText(path);
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException)
            {
                return new HeadsetFacts();
            }
            return Parse(text);
        }

        /// <summary>The facts in a file's text: <c>key = value</c> lines, <c>#</c> comments; unknown keys are ignored.</summary>
        public static HeadsetFacts Parse(string text)
        {
            var facts = new HeadsetFacts();
            var limits = new ViewLimits();
            foreach (var raw in (text ?? string.Empty).Replace("\r\n", "\n").Split('\n'))
            {
                var line = raw.Trim();
                int eq = line.IndexOf('=');
                if (line.StartsWith("#", StringComparison.Ordinal) || eq <= 0) continue;
                var key = line.Substring(0, eq).Trim().ToLowerInvariant();
                var value = line.Substring(eq + 1).Trim();
                switch (key)
                {
                    case "recommended": if (ParseExtent(value) is Extent r) limits.Recommended = r; break;
                    case "max_image": if (ParseExtent(value) is Extent i) limits.MaxImageRect = i; break;
                    case "max_swapchain": if (ParseExtent(value) is Extent s) limits.MaxSwapchain = s; break;
                    case "runtime": facts.RuntimeName = NullIfEmpty(value); break;
                    case "system": facts.SystemName = NullIfEmpty(value); break;
                    case "runtime_manifest": facts.RuntimeManifest = NullIfEmpty(value); break;
                    case "read_at": facts.ReadAt = ParseTime(value); break;
                    case "read_by": facts.ReadBy = ParseReadBy(value); break;
                    case "failed_at": facts.FailedAt = ParseTime(value); break;
                    case "failed": facts.FailedReason = NullIfEmpty(value); break;
                    case "session": facts.Session = NullIfEmpty(value); break;
                    case "session_at": facts.SessionAt = ParseTime(value); break;
                    case "session_runtime": facts.SessionRuntime = NullIfEmpty(value); break;
                    case "session_refresh_hz":
                        if (int.TryParse(value, NumberStyles.None, CultureInfo.InvariantCulture, out var hz) && hz > 0 && hz < 1000) facts.SessionRefreshHz = hz;
                        break;
                    case "session_refresh": facts.SessionRefresh = NullIfEmpty(value); break;
                    case "session_summary": facts.SessionText = NullIfEmpty(value); break;
                    case "session_controls": facts.SessionControls = NullIfEmpty(value); break;
                    case "session_parallel_eyes": facts.SessionParallelEyes = NullIfEmpty(value); break;
                }
            }
            facts.Limits = limits.Recommended.IsEmpty ? null : limits;
            return facts;
        }

        /// <summary>Writes atomically (temporary file, then replace).</summary>
        public static void Save(string path, HeadsetFacts facts)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(path));
            FileUtil.WriteAllTextAtomic(path, Serialize(facts));
        }

        public static string Serialize(HeadsetFacts facts)
        {
            var sb = new StringBuilder();
            sb.Append("# EternalVR launcher: the headset as the last runtime probe that answered read it, and its last session (the Play tab's Headset box)\n");
            void Add(string key, string value)
            {
                if (!string.IsNullOrWhiteSpace(value)) sb.Append(key).Append(" = ").Append(OneLine(value)).Append('\n');
            }
            if (facts.Limits != null)
            {
                Add("recommended", facts.Limits.Recommended.ToString());
                Add("max_image", facts.Limits.MaxImageRect.ToString());
                Add("max_swapchain", facts.Limits.MaxSwapchain.ToString());
            }
            Add("runtime", facts.RuntimeName);
            Add("system", facts.SystemName);
            Add("runtime_manifest", facts.RuntimeManifest);
            Add("read_at", Time(facts.ReadAt));
            if (facts.ReadAt.HasValue) Add("read_by", facts.ReadBy == HeadsetReadBy.Detect ? "detect" : facts.ReadBy == HeadsetReadBy.Start ? "start" : "launch");
            Add("failed_at", Time(facts.FailedAt));
            if (facts.FailedAt.HasValue) Add("failed", facts.FailedReason ?? "unknown");
            Add("session", facts.Session);
            Add("session_at", Time(facts.SessionAt));
            Add("session_runtime", facts.SessionRuntime);
            Add("session_refresh_hz", facts.SessionRefreshHz?.ToString(CultureInfo.InvariantCulture));
            Add("session_refresh", facts.SessionRefresh);
            Add("session_summary", facts.SessionText);
            Add("session_controls", facts.SessionControls);
            Add("session_parallel_eyes", facts.SessionParallelEyes);
            return sb.ToString();
        }

        /// <summary>
        /// The facts with session <paramref name="session"/>'s summary as the last session's, with the cause named when none of
        /// its controls was bound (<paramref name="unbound"/>); unchanged without one.
        /// </summary>
        public static HeadsetFacts AfterSession(HeadsetFacts old, SessionSummary summary, string session, DateTime endedAt,
            UnboundCause unbound = UnboundCause.None)
        {
            var facts = (old ?? new HeadsetFacts()).Clone();
            if (summary == null || summary.RefreshHz <= 0) return facts;
            facts.Session = session;
            facts.SessionAt = endedAt;
            facts.SessionRuntime = summary.RuntimeName;
            facts.SessionRefreshHz = summary.RefreshHz;
            facts.SessionRefresh = summary.Compact();
            var text = summary.Describe();
            facts.SessionText = text.Length == 0 ? null : text;
            var cause = UnboundControls.ReportText(unbound);
            facts.SessionControls = summary.ControlsText() + (cause == null ? string.Empty : "; cause: " + cause);
            facts.SessionParallelEyes = summary.ParallelEyes.Text();
            return facts;
        }

        /// <summary>
        /// The facts after a probe made at <paramref name="now"/> with <paramref name="manifest"/> (by <paramref name="by"/>): an
        /// answer replaces what the probe reads; a failure keeps the old values and says they are old.
        /// </summary>
        public static HeadsetFacts After(HeadsetFacts old, OpenXrProbeResult probe, string manifest, DateTime now, HeadsetReadBy by)
        {
            var facts = (old ?? new HeadsetFacts()).Clone();
            if (probe != null && probe.Ok && !probe.Limits.Recommended.IsEmpty)
            {
                facts.Limits = probe.Limits.WithEyesSideBySide(1);
                facts.RuntimeName = NullIfEmpty(probe.RuntimeName);
                facts.SystemName = NullIfEmpty(probe.SystemName);
                facts.RuntimeManifest = NullIfEmpty(manifest);
                facts.ReadAt = now;
                facts.ReadBy = by;
                facts.FailedAt = null;
                facts.FailedReason = null;
            }
            else
            {
                facts.FailedAt = now;
                facts.FailedReason = probe?.HeadsetUnavailable == true ? "the runtime reports no headset" : probe?.Error ?? "the runtime was not asked";
            }
            return facts;
        }

        // An older launcher wrote only launch and detect; anything else is Launch VR's.
        private static HeadsetReadBy ParseReadBy(string text) =>
            string.Equals(text, "detect", StringComparison.OrdinalIgnoreCase) ? HeadsetReadBy.Detect
            : string.Equals(text, "start", StringComparison.OrdinalIgnoreCase) ? HeadsetReadBy.Start : HeadsetReadBy.Launch;

        // "WxH" with decimal sides (0 allowed: no limit), or null.
        private static Extent? ParseExtent(string text)
        {
            var parts = text.ToLowerInvariant().Split('x');
            if (parts.Length != 2
                || !uint.TryParse(parts[0].Trim(), NumberStyles.None, CultureInfo.InvariantCulture, out var w)
                || !uint.TryParse(parts[1].Trim(), NumberStyles.None, CultureInfo.InvariantCulture, out var h))
                return null;
            return new Extent(w, h);
        }

        private static DateTime? ParseTime(string text) =>
            DateTime.TryParseExact(text, TimeFormat, CultureInfo.InvariantCulture, DateTimeStyles.None, out var t) ? t : (DateTime?)null;

        private static string Time(DateTime? t) => t?.ToString(TimeFormat, CultureInfo.InvariantCulture);

        private static string NullIfEmpty(string s) => string.IsNullOrWhiteSpace(s) ? null : s.Trim();

        private static string OneLine(string s) => s.Replace('\r', ' ').Replace('\n', ' ').Trim();
    }
}
