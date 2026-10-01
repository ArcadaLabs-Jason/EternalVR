using System.Collections.Generic;
using System.Text;

namespace EternalVR.Launcher.Core.Settings
{
    /// <summary>How the game's frames are timed in stereo (the layer's <c>ETERNALVR_PACE</c>): as fast as the game runs, each
    /// headset frame showing the newest pair of eye images, or held to one pair per headset frame, timed to the headset.</summary>
    public enum FramePacing { Off, Headset }

    /// <summary>The Frame pacing setting in launcher.ini: <c>pace = off|headset</c>, off by default.</summary>
    public sealed partial class LauncherSettings
    {
        /// <summary>The game's frames as fast as it runs (default), or one pair per headset frame (experimental).</summary>
        public FramePacing Pacing { get; set; } = FramePacing.Off;

        /// <summary>The settings file's and the layer's <c>ETERNALVR_PACE</c> value: off or headset.</summary>
        public static string PacingName(FramePacing p) => p == FramePacing.Headset ? "headset" : "off";

        private void ReadPacing(IDictionary<string, string> map)
        {
            if (map.TryGetValue("pace", out var pc)) Pacing = Pick(pc, FramePacing.Off, ("headset", FramePacing.Headset));
        }

        private void WritePacing(StringBuilder sb)
        {
            sb.AppendLine("pace = " + PacingName(Pacing));
        }
    }
}
