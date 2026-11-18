using System.Collections.Generic;
using System.Text;

namespace EternalVR.Launcher.Core.Settings
{
    /// <summary>
    /// Wait for a late image (Play tab, Picture group, under Frame pacing; docs/VR_STEREO.md "Frame pacing", Late images):
    /// while the game is matched to the headset, a headset frame that begins while the game is still drawing its image waits
    /// for it, within that frame's own time, instead of showing the image before it again (the layer's
    /// <c>ETERNALVR_PACE_LATE_WAIT=1</c>, DECISIONS T-235). For testing: off by default until it has been looked at in a
    /// headset. Only where the game is matched to the headset: in stereo, Frame pacing Matched to the headset, without
    /// Alternate eyes (on Auto the launch turns pacing off; on, not measured) and without Parallel Eye Rendering (not measured).
    /// launcher.ini: <c>late_wait = 0|1</c>.
    /// </summary>
    public sealed partial class LauncherSettings
    {
        /// <summary>A headset frame waits for the image the matched game is still drawing; off by default.</summary>
        public bool LateWait { get; set; } = false;

        /// <summary>Whether the layer is asked for it: the box ticked, in stereo, Frame pacing Matched to the headset, Alternate
        /// eyes off and Parallel Eye Rendering not running (<see cref="ParallelEyesOn"/> on this game build,
        /// <paramref name="parallelEyesGame"/>): the settings the window does not grey it out for (SettingRules).</summary>
        public static bool LateWaitOn(LauncherSettings s, bool parallelEyesGame) =>
            s.LateWait && s.Mode == VrMode.Stereo && s.Pacing == FramePacing.Headset
            && s.AlternateEyes == AlternateEyesMode.Off && !s.ParallelEyesOn(parallelEyesGame);

        private void ReadLateWait(IDictionary<string, string> map)
        {
            if (map.TryGetValue("late_wait", out var v)) LateWait = On(v);
        }

        private void WriteLateWait(StringBuilder sb) => sb.AppendLine("late_wait = " + (LateWait ? "1" : "0"));
    }
}
