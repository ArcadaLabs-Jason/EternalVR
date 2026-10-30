using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace EternalVR.Launcher.Core.Settings
{
    /// <summary>The game's own Performance Metrics in VR (its <c>com_showFPS</c>), just above the HUD panel: none, the frame rate and
    /// frame time, or those with the average frame rate, the resolution and the game's HDR, ray tracing and DLSS settings.</summary>
    public enum PerformanceOverlay { Off, FrameRate, Detailed }

    /// <summary>
    /// The Performance overlay setting in launcher.ini: <c>perf_overlay = off|frame_rate|detailed</c>, off by default. It
    /// passes the game's own Performance Metrics level on the command line and to the layer (<c>ETERNALVR_PERF_OVERLAY</c>), which
    /// shows the corner the game draws them in above the HUD panel; off passes no cvar, so the game keeps its own setting. Also
    /// reads <c>perf_log</c> (the Advanced tab's Performance in the log), next to it in the window.
    /// </summary>
    public sealed partial class LauncherSettings
    {
        /// <summary>The game's Performance Metrics in VR; off by default.</summary>
        public PerformanceOverlay PerfOverlay { get; set; } = PerformanceOverlay.Off;

        /// <summary>The settings file's value: off, frame_rate or detailed.</summary>
        public static string PerfOverlayName(PerformanceOverlay p) =>
            p == PerformanceOverlay.FrameRate ? "frame_rate" : p == PerformanceOverlay.Detailed ? "detailed" : "off";

        /// <summary>The game's <c>com_showFPS</c> level for <paramref name="p"/>, or null for off: nothing is passed then. Level 3 (the
        /// cvar's "retail version") draws the frame rate and frame time alone, level 2 also the average frame rate, HDR, ray tracing,
        /// DLSS, the resolution and the tick mode (the test PC's GUI captures; docs/VR_STEREO.md, "Performance overlay").</summary>
        public static int? PerfOverlayLevel(PerformanceOverlay p) =>
            p == PerformanceOverlay.FrameRate ? 3 : p == PerformanceOverlay.Detailed ? 2 : (int?)null;

        /// <summary>The game arguments for the overlay: <c>+com_showFPS n</c>, or none when off.</summary>
        public IReadOnlyList<string> PerfOverlayArguments() =>
            PerfOverlayLevel(PerfOverlay) is int level
                ? new[] { "+com_showFPS", level.ToString(CultureInfo.InvariantCulture) }
                : new string[0];

        private void ReadPerformance(IDictionary<string, string> map)
        {
            if (map.TryGetValue("perf_log", out var pl)) PerfLog = Flag(pl);
            if (map.TryGetValue("perf_overlay", out var po))
                PerfOverlay = Pick(po, PerformanceOverlay.Off, ("frame_rate", PerformanceOverlay.FrameRate), ("detailed", PerformanceOverlay.Detailed));
        }

        private void WritePerformance(StringBuilder sb)
        {
            sb.AppendLine("perf_log = " + (PerfLog ? "1" : "0"));
            sb.AppendLine("perf_overlay = " + PerfOverlayName(PerfOverlay));
        }
    }
}
