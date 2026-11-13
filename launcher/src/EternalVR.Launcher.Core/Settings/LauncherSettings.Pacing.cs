using System.Collections.Generic;
using System.Text;

namespace EternalVR.Launcher.Core.Settings
{
    /// <summary>How the game's frames are timed in stereo (the layer's <c>ETERNALVR_PACE</c>): as fast as the game runs, each
    /// headset frame showing the newest pair of eye images; held to one pair per headset frame, timed to the headset; or each
    /// pair shown once, the headset's frames waiting for the game's (new game frames only, <c>new</c>).</summary>
    public enum FramePacing { Off, Headset, NewFrames }

    /// <summary>
    /// The Frame pacing setting in launcher.ini: <c>frame_pacing = headset|off|new</c>, matched to the headset by default; and
    /// <c>depth_layer = 0|1</c> (Depth for your headset's smoothing, off by default).
    /// Launcher 0.1.11 wrote <c>pace = off|headset</c> with off as its default, so its <c>pace = off</c> cannot tell a
    /// choice from the old default: only its <c>pace = headset</c> is read, and anything else takes the new default.
    /// </summary>
    public sealed partial class LauncherSettings
    {
        /// <summary>One pair of eye images per headset frame (default), or the game's frames as fast as it runs.</summary>
        public FramePacing Pacing { get; set; } = FramePacing.Headset;

        /// <summary>Each eye's depth handed to the headset's runtime with its picture, for its reprojection when frames are late
        /// (the layer's <c>ETERNALVR_DEPTH_LAYER</c>); off by default, experimental.</summary>
        public bool DepthLayer { get; set; } = false;

        /// <summary>Whether the layer is asked for the depth: the box ticked, in stereo, with neither Parallel Eye Rendering
        /// running (<see cref="ParallelEyesOn"/> on this game build, <paramref name="parallelEyesGame"/>) nor Alternate eyes
        /// (on or auto): the settings the window does not grey it out for (SettingRules).</summary>
        public static bool DepthLayerOn(LauncherSettings s, bool parallelEyesGame) =>
            s.DepthLayer && s.Mode == VrMode.Stereo && !s.ParallelEyesOn(parallelEyesGame)
            && s.AlternateEyes == AlternateEyesMode.Off;

        /// <summary>The settings file's and the layer's <c>ETERNALVR_PACE</c> value: off, headset or new.</summary>
        public static string PacingName(FramePacing p) => p == FramePacing.Headset ? "headset" : p == FramePacing.NewFrames ? "new" : "off";

        private void ReadPacing(IDictionary<string, string> map)
        {
            if (map.TryGetValue("frame_pacing", out var fp)) Pacing = Pick(fp, FramePacing.Headset, ("off", FramePacing.Off), ("new", FramePacing.NewFrames));
            else if (map.TryGetValue("pace", out var pc)) Pacing = Pick(pc, FramePacing.Headset, ("headset", FramePacing.Headset));
            if (map.TryGetValue("depth_layer", out var dl)) DepthLayer = On(dl);
        }

        private void WritePacing(StringBuilder sb)
        {
            sb.AppendLine("frame_pacing = " + PacingName(Pacing));
            sb.AppendLine("depth_layer = " + (DepthLayer ? "1" : "0"));
        }
    }
}
