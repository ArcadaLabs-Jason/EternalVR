using System.Collections.Generic;
using System.Text;

namespace EternalVR.Launcher.Core.Settings
{
    /// <summary>
    /// The controller angles in launcher.ini: <c>weapon_hand_angle</c> and <c>off_hand_angle</c>, degrees, and
    /// <c>index_angle_reset</c>, this machine's marker that the one-time reset of 0.1.87's Index advice has run
    /// (<see cref="IndexAngleMigration"/>).
    /// </summary>
    public sealed partial class LauncherSettings
    {
        /// <summary>Each controller turned about its grip's sideways axis, degrees, + lifts its front (the layer's
        /// <c>ETERNALVR_WEAPON_HAND_TILT</c> and <c>ETERNALVR_OFF_HAND_TILT</c>).</summary>
        public const double MinControllerAngle = -60.0, MaxControllerAngle = 60.0, DefaultControllerAngle = 0.0;

        /// <summary>The layer's <c>ETERNALVR_WEAPON_HAND_TILT</c>, degrees.</summary>
        public double WeaponHandAngle { get; set; } = DefaultControllerAngle;
        /// <summary>The layer's <c>ETERNALVR_OFF_HAND_TILT</c>, degrees.</summary>
        public double OffHandAngle { get; set; } = DefaultControllerAngle;

        /// <summary>
        /// The one-time reset of the angles 0.1.87 told Index players to set has run on this machine (<see cref="IndexAngleMigration"/>):
        /// it never runs again. This machine's, like the folders: never in a profile (<see cref="SettingKeys.Machine"/>).
        /// </summary>
        public bool IndexAngleReset { get; set; } = false;

        /// <summary>Reads both angles; 0 for a missing key or one that is not a number.</summary>
        private void ReadControllerAngles(IDictionary<string, string> map)
        {
            if (map.TryGetValue("weapon_hand_angle", out var wa)) WeaponHandAngle = Number(wa, MinControllerAngle, MaxControllerAngle, DefaultControllerAngle);
            if (map.TryGetValue("off_hand_angle", out var oa)) OffHandAngle = Number(oa, MinControllerAngle, MaxControllerAngle, DefaultControllerAngle);
            if (map.TryGetValue("index_angle_reset", out var ir)) IndexAngleReset = On(ir);
        }

        private void WriteControllerAngles(StringBuilder sb)
        {
            sb.AppendLine("weapon_hand_angle = " + Metres(Clamp(WeaponHandAngle, MinControllerAngle, MaxControllerAngle, DefaultControllerAngle)));
            sb.AppendLine("off_hand_angle = " + Metres(Clamp(OffHandAngle, MinControllerAngle, MaxControllerAngle, DefaultControllerAngle)));
            sb.AppendLine("index_angle_reset = " + (IndexAngleReset ? "1" : "0"));
        }
    }
}
