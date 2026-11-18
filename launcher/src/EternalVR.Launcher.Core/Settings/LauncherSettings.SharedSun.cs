using System.Collections.Generic;
using System.Text;

namespace EternalVR.Launcher.Core.Settings
{
    /// <summary>
    /// Shared sun shadows (Advanced tab, View group; docs/VR_STEREO.md "Shared sun cascades"): the right eye shades with the
    /// sun's shadow cascades the left eye rendered in the same game frame instead of rendering them again (the layer's
    /// <c>ETERNALVR_SHARED_SUN=1</c>). For testing: off by default until it has been looked at in a headset. The standard
    /// renderer's eye pairs only: in stereo, without Parallel Eye Rendering and without Alternate eyes (on or auto), where
    /// the layer does not install it. launcher.ini: <c>shared_sun = 0|1</c>.
    /// </summary>
    public sealed partial class LauncherSettings
    {
        /// <summary>The right eye on the left eye's sun cascades; off by default.</summary>
        public bool SharedSun { get; set; } = false;

        /// <summary>Whether the layer is asked for it: the box ticked, in stereo, with neither Parallel Eye Rendering running
        /// (<see cref="ParallelEyesOn"/> on this game build, <paramref name="parallelEyesGame"/>) nor Alternate eyes (on or
        /// auto): the settings the window does not grey it out for (SettingRules).</summary>
        public static bool SharedSunOn(LauncherSettings s, bool parallelEyesGame) =>
            s.SharedSun && s.Mode == VrMode.Stereo && !s.ParallelEyesOn(parallelEyesGame)
            && s.AlternateEyes == AlternateEyesMode.Off;

        private void ReadSharedSun(IDictionary<string, string> map)
        {
            if (map.TryGetValue("shared_sun", out var v)) SharedSun = On(v);
        }

        private void WriteSharedSun(StringBuilder sb) => sb.AppendLine("shared_sun = " + (SharedSun ? "1" : "0"));
    }
}
