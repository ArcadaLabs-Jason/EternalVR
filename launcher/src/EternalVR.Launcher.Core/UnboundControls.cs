using System;
using EternalVR.Launcher.Core.Headsets;
using EternalVR.Launcher.Core.Preflight;

namespace EternalVR.Launcher.Core
{
    /// <summary>Why the mod's controls did nothing in a session, as far as the launcher can tell (<see cref="UnboundControls"/>).</summary>
    public enum UnboundCause
    {
        /// <summary>Controls were bound, or the session's log does not show that none were.</summary>
        None,
        /// <summary>SteamVR ran the session with a controller binding chosen for DOOM Eternal, and it bound none of the mod's actions.</summary>
        SteamVrBinding,
        /// <summary>No action was bound and no hand pose arrived in play, for a cause the launcher cannot name.</summary>
        Unknown,
    }

    /// <summary>
    /// The status line after a session in which none of the mod's controller actions was bound (the layer's
    /// <c>controllers: 0 of 12 action(s) bound</c>), named for the cause. SteamVR keeps controller bindings per game, and a
    /// binding chosen there for DOOM Eternal (a workshop one, or a file) replaces the mod's suggested bindings: only then
    /// are SteamVR's binding steps shown. Some runtimes list no bound source even for working bindings (the rig's OpenXR
    /// simulator does), so elsewhere zero bound counts only when the layer also warned that no hand pose arrived in play.
    /// </summary>
    public static class UnboundControls
    {
        /// <summary>The status sentence when SteamVR's chosen binding bound nothing. SteamVR's binding page lists the game only while it runs.</summary>
        public const string SteamVrText = "Controllers did nothing: SteamVR's controller binding for DOOM Eternal maps none of EternalVR's controls. "
            + "While the game runs: " + PreflightEvaluator.SteamVrBindingsHelp + " > DOOM Eternal > Default.";

        /// <summary>The status sentence for any other cause.</summary>
        public const string UnknownText = "No controller input reached EternalVR this session. " + Report.ReportHint.Ask;

        /// <summary>
        /// The cause for a finished session's <paramref name="summary"/> (null: no VR session). <paramref name="steamVrBindingChosen"/>
        /// says whether SteamVR's settings choose a controller binding for the game (<see cref="Report.SteamVrSummary.ReadCustomBindings"/>);
        /// it is asked only for a SteamVR session in which nothing was bound.
        /// </summary>
        public static UnboundCause Decide(SessionSummary summary, Func<bool> steamVrBindingChosen)
        {
            if (summary == null || summary.ControlsBound != 0 || !(summary.Controls > 0)) return UnboundCause.None;
            if (summary.Route == RouteKind.SteamVr && steamVrBindingChosen != null && steamVrBindingChosen()) return UnboundCause.SteamVrBinding;
            return summary.NoHandPose ? UnboundCause.Unknown : UnboundCause.None;
        }

        /// <summary>The sentence for the status line; empty for <see cref="UnboundCause.None"/>.</summary>
        public static string StatusText(UnboundCause cause)
        {
            switch (cause)
            {
                case UnboundCause.SteamVrBinding: return SteamVrText;
                case UnboundCause.Unknown: return UnknownText;
                default: return string.Empty;
            }
        }

        /// <summary>The cause for the report and headset.txt: "SteamVR binding for DOOM Eternal", "unknown", or null for none.</summary>
        public static string ReportText(UnboundCause cause)
        {
            switch (cause)
            {
                case UnboundCause.SteamVrBinding: return "SteamVR binding for DOOM Eternal";
                case UnboundCause.Unknown: return "unknown";
                default: return null;
            }
        }
    }
}
