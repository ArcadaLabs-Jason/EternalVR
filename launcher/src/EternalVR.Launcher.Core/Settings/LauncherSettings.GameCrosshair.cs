using System.Collections.Generic;
using System.Text;

namespace EternalVR.Launcher.Core.Settings
{
    /// <summary>What the game's own crosshair does under hand aim (<c>game_crosshair</c>; docs/VR_CONTROLLERS.md "The game's
    /// crosshair under hand aim", DECISIONS T-233).</summary>
    public enum GameCrosshairMode
    {
        /// <summary>Hidden: the aim dot alone marks the gun's ray (the layer's default, <c>hold</c>).</summary>
        Hidden,
        /// <summary>On the gun: the game's crosshair, with the weapon cues it draws (the Lock-On Burst's lock, charge and heat
        /// bars), around the aim dot (the layer's <c>ETERNALVR_UI_CROSSHAIR=gun</c>).</summary>
        Gun,
    }

    /// <summary>
    /// The Game's crosshair (hand aim) setting (Play tab, under the Aim dot): Hidden by default until it has been looked at in
    /// a headset. On the gun passes <c>ETERNALVR_UI_CROSSHAIR=gun</c>, and only with the aim dot on: the crosshair sits around
    /// the dot, and with the dot off the player asked for nothing in the middle (the layer holds the crosshair hidden then
    /// too). Hidden passes nothing (the layer's default). launcher.ini: <c>game_crosshair = hidden|gun</c>.
    /// </summary>
    public sealed partial class LauncherSettings
    {
        /// <summary>The game's crosshair under hand aim; <see cref="DefaultGameCrosshair"/> by default.</summary>
        public GameCrosshairMode GameCrosshair { get; set; } = DefaultGameCrosshair;

        /// <summary>The game's crosshair a fresh data folder (and a settings file without <c>game_crosshair</c>) has.</summary>
        public const GameCrosshairMode DefaultGameCrosshair = GameCrosshairMode.Hidden;

        /// <summary>Whether the launch passes <c>ETERNALVR_UI_CROSSHAIR=gun</c>: On the gun with the aim dot on.</summary>
        public bool GameCrosshairOnGun => GameCrosshair == GameCrosshairMode.Gun && AimDot;

        /// <summary>The settings file's <c>game_crosshair</c> value: hidden or gun.</summary>
        public static string GameCrosshairName(GameCrosshairMode m) => m == GameCrosshairMode.Gun ? "gun" : "hidden";

        /// <summary>Why the row does not apply (<see cref="SettingRules.WhyNot"/>), or null: it needs the controllers, hand aim
        /// and the aim dot.</summary>
        internal string GameCrosshairWhyNot()
        {
            if (!Controllers) return SettingRules.NeedsControllers;
            if (Aim != AimMode.Hand) return SettingRules.NeedsHandAim;
            return AimDot ? null : SettingRules.NeedsAimDot;
        }

        private void ReadGameCrosshair(IDictionary<string, string> map)
        {
            if (map.TryGetValue("game_crosshair", out var v))
                GameCrosshair = Pick(v, DefaultGameCrosshair, ("hidden", GameCrosshairMode.Hidden), ("gun", GameCrosshairMode.Gun));
        }

        private void WriteGameCrosshair(StringBuilder sb) => sb.AppendLine("game_crosshair = " + GameCrosshairName(GameCrosshair));
    }
}
