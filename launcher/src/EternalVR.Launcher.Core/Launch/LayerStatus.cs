using System;
using System.Collections.Generic;

namespace EternalVR.Launcher.Core.Launch
{
    /// <summary>The layer's state for the session, from <c>eternalvr-status.txt</c>.</summary>
    public enum LayerState { Starting, Waiting, Vr, Flat, Unknown }

    /// <summary>
    /// <c>&lt;ETERNALVR_LOG_DIR&gt;\eternalvr-status.txt</c>, rewritten atomically by the layer on every change:
    /// <c>state=starting|waiting|vr|flat</c>, <c>reason=</c> (a sentence for the player), <c>stereo=on|off: reason</c> (empty until
    /// decided), <c>version=</c>, <c>pid=</c>. <c>flat</c> is final for the session. Also <c>ssr_follow=0|1</c> and
    /// <c>ssdo_follow=0|1</c>: 1 while the layer held r_SSR or r_SSDO at the player's own game setting, at <c>ssr_value=</c>
    /// and <c>ssdo_value=</c> (<see cref="Followed"/>).
    /// </summary>
    public sealed class LayerStatusFile
    {
        public const string FileName = "eternalvr-status.txt";
        /// <summary>Written by the layer when the loader loads it (before the status file).</summary>
        public const string LoadedMarker = "LAYER_LOADED";

        public LayerState State { get; private set; } = LayerState.Unknown;
        public string Reason { get; private set; } = string.Empty;
        /// <summary>True for <c>stereo=on</c>, false for <c>stereo=off...</c>, null while undecided.</summary>
        public bool? Stereo { get; private set; }
        public string StereoReason { get; private set; } = string.Empty;
        public string Version { get; private set; } = string.Empty;
        public string Pid { get; private set; } = string.Empty;
        /// <summary>
        /// The cvars the layer last reported as held at the player's own game setting with per-eye TAA on, with the value held
        /// (<c>r_SSR</c> for <c>ssr_follow=1</c> and <c>ssr_value=</c>, <c>r_SSDO</c> for <c>ssdo_follow=1</c> and <c>ssdo_value=</c>):
        /// an r_SSR or r_SSDO the game saved at that value is the player's, and the settings restore keeps it
        /// (<see cref="Safety.SettingsSnapshot.Restore"/>). A follow without its value (an older layer) is not in it.
        /// </summary>
        public IReadOnlyDictionary<string, string> Followed => followed;

        private readonly Dictionary<string, string> followed = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        private readonly Dictionary<string, bool> follows = new Dictionary<string, bool>();
        private readonly Dictionary<string, string> values = new Dictionary<string, string>();

        public static LayerStatusFile Parse(string text)
        {
            var s = new LayerStatusFile();
            foreach (var raw in (text ?? string.Empty).Replace("\r\n", "\n").Split('\n'))
            {
                int eq = raw.IndexOf('=');
                if (eq <= 0) continue;
                var key = raw.Substring(0, eq).Trim().ToLowerInvariant();
                var value = raw.Substring(eq + 1).Trim();
                switch (key)
                {
                    case "state": s.State = ParseState(value); break;
                    case "reason": s.Reason = value; break;
                    case "version": s.Version = value; break;
                    case "pid": s.Pid = value; break;
                    case "ssr_follow": s.follows["r_SSR"] = value == "1"; break;
                    case "ssdo_follow": s.follows["r_SSDO"] = value == "1"; break;
                    case "ssr_value": s.values["r_SSR"] = value; break;
                    case "ssdo_value": s.values["r_SSDO"] = value; break;
                    case "stereo":
                        if (value.StartsWith("on", StringComparison.OrdinalIgnoreCase)) s.Stereo = true;
                        else if (value.StartsWith("off", StringComparison.OrdinalIgnoreCase)) s.Stereo = false;
                        int colon = value.IndexOf(':');
                        s.StereoReason = colon >= 0 ? value.Substring(colon + 1).Trim() : string.Empty;
                        break;
                }
            }
            foreach (var f in s.follows)
                if (f.Value && s.values.TryGetValue(f.Key, out var held) && held.Length > 0) s.followed[f.Key] = held;
            return s;
        }

        private static LayerState ParseState(string v)
        {
            switch (v.ToLowerInvariant())
            {
                case "starting": return LayerState.Starting;
                case "waiting": return LayerState.Waiting;
                case "vr": return LayerState.Vr;
                case "flat": return LayerState.Flat;
                default: return LayerState.Unknown;
            }
        }
    }

    public enum StatusKind { Info, Good, Warning, Problem }

    /// <summary>A session outcome for the window's status line (and a dialog for problems).</summary>
    public sealed class SessionStatus : IEquatable<SessionStatus>
    {
        public SessionStatus(StatusKind kind, string text)
        {
            Kind = kind;
            Text = text;
        }

        public StatusKind Kind { get; }
        public string Text { get; }

        public bool Equals(SessionStatus other) => other != null && other.Kind == Kind && other.Text == Text;
        public override bool Equals(object obj) => Equals(obj as SessionStatus);
        public override int GetHashCode() => (Text ?? string.Empty).GetHashCode() ^ (int)Kind;
        public override string ToString() => Kind + ": " + Text;
    }

    /// <summary>
    /// Holds back a problem from the layer until it has stood for <see cref="HoldSeconds"/> while the game runs: a game being
    /// quit ends its XR session, and the layer's "VR is off" written on the way out must not open a dialog.
    /// </summary>
    public sealed class ProblemHold
    {
        public const double HoldSeconds = 5;
        private double since;

        /// <summary>The problem waiting out its hold; null when none.</summary>
        public SessionStatus Pending { get; private set; }

        /// <summary>The status to report now, or null while a problem is held (or there is nothing).</summary>
        public SessionStatus Offer(SessionStatus s, double nowSeconds)
        {
            if (s == null || s.Kind != StatusKind.Problem)
            {
                Pending = null;
                return s;
            }
            if (!s.Equals(Pending))
            {
                Pending = s;
                since = nowSeconds;
            }
            return nowSeconds - since >= HoldSeconds ? s : null;
        }
    }

    /// <summary>What the window shows about the layer while the game runs (T-082 status surfacing).</summary>
    public static class LayerWatch
    {
        /// <summary>No loaded marker this long after the game started: the layer did not load.</summary>
        public const double LoadTimeoutSeconds = 45;

        /// <summary>
        /// Still "starting" this long after the game started: VR has not come up. A layer that refuses the session writes
        /// <c>flat</c> with its reason; this covers a layer that stopped without saying why.
        /// </summary>
        public const double StartTimeoutSeconds = 120;

        public const string StartingMessage = "The mod loaded; VR is starting.";
        public const string NotStartedMessage = "VR has not started after 2 minutes; the game runs flat until it does.";

        public const string NotLoadedMessage =
            "The mod did not load, so the game runs without VR. Check that the whole EternalVR zip was extracted (the layer folder "
            + "next to the launcher), and that your antivirus did not block or remove EternalVR.dll; then quit the game and launch again.";

        /// <param name="loaded">The loaded marker exists.</param>
        /// <param name="status">The parsed status file, or null when there is none.</param>
        /// <param name="secondsSinceStart">Since the game process started.</param>
        /// <returns>The status to show, or null while there is nothing to say yet.</returns>
        public static SessionStatus Decide(bool loaded, LayerStatusFile status, double secondsSinceStart)
        {
            if (status != null && status.State != LayerState.Unknown)
            {
                switch (status.State)
                {
                    case LayerState.Starting:
                        return Starting(secondsSinceStart);
                    case LayerState.Waiting:
                        return new SessionStatus(StatusKind.Warning, "Waiting for the headset: " + Or(status.Reason, "no headset session yet") + " (retried every second).");
                    case LayerState.Flat:
                        return new SessionStatus(StatusKind.Problem, "VR is off for this session: " + Or(status.Reason, "see the layer log") + ". The game runs flat.");
                    case LayerState.Vr:
                        if (status.Stereo == false)
                            return new SessionStatus(StatusKind.Warning, "VR is running in mono (one image for both eyes): " + Or(status.StereoReason, "stereo is off") + ".");
                        return new SessionStatus(StatusKind.Good, "VR is running" + (status.Stereo == true ? " in stereo." : "."));
                }
            }
            if (loaded) return Starting(secondsSinceStart);
            if (secondsSinceStart >= LoadTimeoutSeconds) return new SessionStatus(StatusKind.Problem, NotLoadedMessage);
            return null;
        }

        private static SessionStatus Starting(double secondsSinceStart) => secondsSinceStart >= StartTimeoutSeconds
            ? new SessionStatus(StatusKind.Warning, NotStartedMessage)
            : new SessionStatus(StatusKind.Info, StartingMessage);

        private static string Or(string text, string fallback) => string.IsNullOrWhiteSpace(text) ? fallback : text.TrimEnd('.');

        /// <summary>The kinds that need the player's attention in a dialog, not just the status line.</summary>
        public static bool NeedsDialog(SessionStatus s) => s != null && s.Kind == StatusKind.Problem;

    }
}
