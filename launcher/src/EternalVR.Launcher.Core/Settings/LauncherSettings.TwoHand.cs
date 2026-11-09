using System.Collections.Generic;
using System.Text;

namespace EternalVR.Launcher.Core.Settings
{
    /// <summary>
    /// The Two-handed aim setting (Advanced tab, its own group; docs/VR_HANDS_HUD.md "Two-handed aim"): off by default until
    /// it has been tried in a headset. On, the off hand's grip held on the gun's fore-end aims the gun along the line
    /// between the hands (the layer's <c>ETERNALVR_TWO_HAND</c>). It needs the arms the layer poses (the controllers on and
    /// the weapon hand aiming, <see cref="ArmsPosed"/>): without them it is not passed on, and the row says why. launcher.ini: <c>two_hand_aim = 0|1</c>.
    /// </summary>
    public sealed partial class LauncherSettings
    {
        /// <summary>The off hand on the gun's support point aims it; off by default.</summary>
        public bool TwoHandAim { get; set; } = false;

        /// <summary>Whether the layer gets two-handed aim: with the setting on and the arms posed (<see cref="ArmsPosed"/>).</summary>
        public bool TwoHandInUse => TwoHandAim && ArmsPosed;

        /// <summary>The layer's <c>ETERNALVR_TWO_HAND</c>, always passed: 1 while in use, else 0.</summary>
        public string TwoHandName() => TwoHandInUse ? "1" : "0";

        /// <summary>Why the Two-handed aim row does not apply (<see cref="SettingRules.WhyNot"/>), or null.</summary>
        internal string TwoHandWhyNot()
        {
            if (!Controllers) return SettingRules.NeedsControllers;
            return Aim != AimMode.Hand ? SettingRules.NeedsHandAim : null;
        }

        private void ReadTwoHand(IDictionary<string, string> map)
        {
            if (map.TryGetValue("two_hand_aim", out var v)) TwoHandAim = On(v);
        }

        private void WriteTwoHand(StringBuilder sb) => sb.AppendLine("two_hand_aim = " + (TwoHandAim ? "1" : "0"));
    }
}
