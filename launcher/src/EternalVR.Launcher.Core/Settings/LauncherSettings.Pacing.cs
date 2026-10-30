using System.Collections.Generic;
using System.Text;

namespace EternalVR.Launcher.Core.Settings
{
    /// <summary>How the game's frames are timed in stereo (the layer's <c>ETERNALVR_PACE</c>): as fast as the game runs, each
    /// headset frame showing the newest pair of eye images; held to one pair per headset frame, timed to the headset; or each
    /// pair shown once, the headset's frames waiting for the game's (new game frames only, <c>new</c>).</summary>
    public enum FramePacing { Off, Headset, NewFrames }

    /// <summary>
    /// The Frame pacing setting in launcher.ini: <c>frame_pacing = headset|off|new</c>, matched to the headset by default.
    /// Launcher 0.1.11 wrote <c>pace = off|headset</c> with off as its default, so its <c>pace = off</c> cannot tell a
    /// choice from the old default: only its <c>pace = headset</c> is read, and anything else takes the new default.
    /// </summary>
    public sealed partial class LauncherSettings
    {
        /// <summary>One pair of eye images per headset frame (default), or the game's frames as fast as it runs.</summary>
        public FramePacing Pacing { get; set; } = FramePacing.Headset;

        /// <summary>The settings file's and the layer's <c>ETERNALVR_PACE</c> value: off, headset or new.</summary>
        public static string PacingName(FramePacing p) => p == FramePacing.Headset ? "headset" : p == FramePacing.NewFrames ? "new" : "off";

        private void ReadPacing(IDictionary<string, string> map)
        {
            if (map.TryGetValue("frame_pacing", out var fp)) Pacing = Pick(fp, FramePacing.Headset, ("off", FramePacing.Off), ("new", FramePacing.NewFrames));
            else if (map.TryGetValue("pace", out var pc)) Pacing = Pick(pc, FramePacing.Headset, ("headset", FramePacing.Headset));
        }

        private void WritePacing(StringBuilder sb)
        {
            sb.AppendLine("frame_pacing = " + PacingName(Pacing));
        }
    }
}
