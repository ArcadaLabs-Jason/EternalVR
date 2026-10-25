using System;

namespace EternalVR.Launcher.Core.Settings
{
    /// <summary>
    /// The first-person arms (the layer's <c>ETERNALVR_ARMS</c>): drawn as the game has them, hidden with the weapon alone, or
    /// hands only, the gloves alone with the arms behind them folded away (docs/VR_HANDS_HUD.md, "Hands only"). The Play tab
    /// lists them in this order. The arms the layer poses are the free off hand and the bind solve (DECISIONS T-160).
    /// </summary>
    public enum ArmsView { Shown, Hidden, HandsOnly }

    /// <summary>The Show arms setting in launcher.ini: <c>show_arms = 1|0|hands</c> (1 and 0 as older launchers wrote them).</summary>
    public sealed partial class LauncherSettings
    {
        /// <summary>The first-person arms; shown by default.</summary>
        public ArmsView Arms { get; set; } = ArmsView.Shown;

        /// <summary>
        /// Whether the layer poses the arms at all: only with the controllers on and the weapon hand aiming. Under head aim,
        /// the view or a gamepad both arms are the game's, so the free off hand must not reach such a session: the layer
        /// would install its hook and its finger modifiers for nothing.
        /// </summary>
        public bool ArmsPosed => Controllers && Aim == AimMode.Hand;

        /// <summary>The layer's <c>ETERNALVR_ARM_SOLVE</c>: the bind pose's solve while the arms are posed, else the animated one.</summary>
        public string ArmSolveName() => ArmsPosed ? "bind" : "animated";

        /// <summary>The layer's <c>ETERNALVR_OFFHAND</c>: the off hand on its controller while the arms are posed, else the game's.</summary>
        public string OffhandName() => ArmsPosed ? "free" : "game";

        /// <summary>The layer's <c>ETERNALVR_ARMS</c> value: shown, hidden or hands.</summary>
        public static string ArmsName(ArmsView a) => a == ArmsView.Hidden ? "hidden" : a == ArmsView.HandsOnly ? "hands" : "shown";

        /// <summary>
        /// The <c>ETERNALVR_ARMS</c> sent to the layer: hands only only while the arms are posed (<see cref="ArmsPosed"/>),
        /// else shown, the game's arms; hidden always.
        /// </summary>
        public string ArmsEnvironment() => ArmsName(Arms == ArmsView.HandsOnly && !ArmsPosed ? ArmsView.Shown : Arms);

        /// <summary>The <c>show_arms</c> value written: 1 (shown) and 0 (hidden) as before, so an older launcher reads them as it did; hands.</summary>
        private static string ArmsFileValue(ArmsView a) => a == ArmsView.Hidden ? "0" : a == ArmsView.HandsOnly ? "hands" : "1";

        /// <summary>A <c>show_arms</c> value: hidden for 0 or false, hands only for hands (any case), shown for anything else.</summary>
        private static ArmsView ParseArms(string text)
        {
            var t = text.Trim();
            if (t == "0" || string.Equals(t, "false", StringComparison.OrdinalIgnoreCase)) return ArmsView.Hidden;
            return string.Equals(t, "hands", StringComparison.OrdinalIgnoreCase) ? ArmsView.HandsOnly : ArmsView.Shown;
        }
    }
}
