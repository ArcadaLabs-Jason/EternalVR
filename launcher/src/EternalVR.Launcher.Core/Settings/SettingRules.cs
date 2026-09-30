using System;
using System.Collections.Generic;

namespace EternalVR.Launcher.Core.Settings
{
    /// <summary>Every setting the launcher window shows (Play and Advanced tabs), in window order.</summary>
    public enum Setting
    {
        // Play: comfort
        Turning, TurnSpeed, SnapAngle, Vignette, GloryKills, WalkInRoom, HeadFade, RecenterHold, SkipCutscenes,
        // Play: body
        PlayPosition, EyeHeight,
        // Play: gestures
        ThrowGesture, SwingGesture,
        // Play: controls
        AimWith, RevenantAimWith, WeaponHand, MoveToward, XButton, WeaponWheel, AimSteadiness, AimDot, Vibration, Bhaptics, ButtonLayout,
        // Play: picture
        Resolution, AntiAliasing, Foveation, TextureStreaming, CpuSaver,
        // Advanced
        VrMode, AlternateEyes, WorldSize, EyeDistance, DesktopWindow, DesktopMonitor, DesktopSize, DesktopCrop, CutsceneView, CutsceneShape,
        HudDistance, HudSize, HudHeight, HudPlace, DlssVersion, DlssPreset,
        MotionControllers, ShotsFrom, AimDotSize, MenuLaser, GameFolder, Runtime, ExtraArguments,
    }

    /// <summary>
    /// Which settings apply with the others as they are. A setting that does not apply is greyed out in the window with the
    /// reason as its tooltip; its value is kept (and still written for the layer, which ignores it).
    /// </summary>
    public static class SettingRules
    {
        public const string NeedsControllers = "Needs motion controllers (Advanced tab).";
        public const string NeedsStereo = "Only in stereo (VR mode on the Advanced tab).";
        public const string NeedsHandAim = "Only when the weapon hand aims (Aim with).";
        public const string NeedsHeadOrHandAim = "Only when the weapon hand or the head aims (Aim with).";
        public const string NeedsSmoothTurn = "Only with smooth turning.";
        public const string NeedsSnapTurn = "Only with snap turning.";
        public const string NotSitting = "Not while sitting: your body stays put (Play position).";
        public const string NeedsRenderSize =
            "Only when the game renders at the headset's size (render_size in launcher.ini): otherwise the window is the eye image.";
        public const string NeedsCinema = "Only with cutscenes on a flat screen (Cutscene view).";
        public const string NeedsDlss = "Only with DLSS (Anti-aliasing on the Play tab).";
        public const string NeedsDlssFile = "Only with a DLSS file of your own (DLSS version).";

        /// <summary>Null when <paramref name="setting"/> applies, else why not (one sentence).</summary>
        public static string WhyNot(Setting setting, LauncherSettings s)
        {
            bool stereo = s.Mode == VrMode.Stereo;
            bool handAim = s.Controllers && s.Aim == AimMode.Hand;
            switch (setting)
            {
                case Setting.Turning:
                case Setting.Vignette:
                case Setting.GloryKills:
                case Setting.RecenterHold:
                case Setting.WeaponHand:
                case Setting.MoveToward:
                case Setting.XButton:
                case Setting.WeaponWheel:
                case Setting.ThrowGesture:
                case Setting.SwingGesture:
                case Setting.Vibration:
                case Setting.Bhaptics:
                case Setting.ButtonLayout:
                    return s.Controllers ? null : NeedsControllers;
                case Setting.TurnSpeed:
                    return !s.Controllers ? NeedsControllers : s.Turn == TurnMode.Smooth ? null : NeedsSmoothTurn;
                case Setting.SnapAngle:
                    return !s.Controllers ? NeedsControllers : s.Turn == TurnMode.Snap ? null : NeedsSnapTurn;
                case Setting.WalkInRoom:
                    return !s.Controllers ? NeedsControllers : s.Posture == PostureMode.Seated ? NotSitting : null;
                case Setting.RevenantAimWith:
                    // Without the controllers, or with the mouse, there is nothing to choose between.
                    return !s.Controllers ? NeedsControllers : s.Aim == AimMode.View ? NeedsHeadOrHandAim : null;
                case Setting.AimSteadiness:
                case Setting.ShotsFrom:
                    return !s.Controllers ? NeedsControllers : handAim ? null : NeedsHandAim;
                case Setting.AimDot:
                case Setting.AimDotSize:
                    if (!s.Controllers) return NeedsControllers;
                    if (!handAim) return NeedsHandAim;
                    if (!stereo) return NeedsStereo;
                    return setting == Setting.AimDotSize && !s.AimDot ? "Only with the aim dot on (Play tab)." : null;
                case Setting.MenuLaser:
                case Setting.HudPlace:
                    return !s.Controllers ? NeedsControllers : stereo ? null : NeedsStereo;
                case Setting.Resolution:
                case Setting.AntiAliasing:
                case Setting.Foveation:
                case Setting.AlternateEyes:
                case Setting.TextureStreaming:
                case Setting.CpuSaver:
                case Setting.DesktopWindow:
                case Setting.HudDistance:
                case Setting.HudSize:
                case Setting.HudHeight:
                    return stereo ? null : NeedsStereo;
                case Setting.DesktopMonitor:
                case Setting.DesktopSize:
                case Setting.DesktopCrop:
                    if (!stereo) return NeedsStereo;
                    return LauncherSettings.NormaliseRenderSize(s.RenderSize) == LauncherSettings.RenderSizeOff ? NeedsRenderSize : null;
                case Setting.DlssVersion:
                case Setting.DlssPreset:
                    if (!stereo) return NeedsStereo;
                    if (s.AntiAliasing != AntiAliasingMode.Dlss) return NeedsDlss;
                    return setting == Setting.DlssPreset && s.DlssDll != DlssDllChoice.File ? NeedsDlssFile : null;
                case Setting.CutsceneShape:
                    if (!stereo) return NeedsStereo;
                    return s.Cutscenes == CutsceneView.Cinema ? null : NeedsCinema;
                default:
                    return null;
            }
        }

        /// <summary>Every setting that does not apply, with the reason.</summary>
        public static IReadOnlyDictionary<Setting, string> Inapplicable(LauncherSettings s)
        {
            var map = new Dictionary<Setting, string>();
            foreach (Setting setting in Enum.GetValues(typeof(Setting)))
            {
                var why = WhyNot(setting, s);
                if (why != null) map[setting] = why;
            }
            return map;
        }
    }

    /// <summary>"Aim steadiness": named steps of the hand-aim smoothing (0 off to 1 the strongest).</summary>
    public static class AimSteadiness
    {
        public static readonly string[] Names = { "Off", "Low", "Medium", "High" };
        public static readonly double[] Values = { 0.0, 0.15, 0.3, 0.5 };

        /// <summary>The step of <paramref name="smoothing"/>, or -1 for a value that is none of them (set in the file by hand).</summary>
        public static int IndexOf(double smoothing)
        {
            for (int i = 0; i < Values.Length; i++)
                if (Math.Abs(Values[i] - smoothing) < 0.005) return i;
            return -1;
        }
    }

    /// <summary>"Vibration": named steps of the controllers' vibration strength (0 off to 1 the strongest).</summary>
    public static class Vibration
    {
        public static readonly string[] Names = { "Off", "Light", "Medium", "Strong" };
        public static readonly double[] Values = { 0.0, 0.35, 0.6, 1.0 };

        /// <summary>The step of <paramref name="strength"/>, or -1 for a value that is none of them (set in the file by hand).</summary>
        public static int IndexOf(double strength)
        {
            for (int i = 0; i < Values.Length; i++)
                if (Math.Abs(Values[i] - strength) < 0.005) return i;
            return -1;
        }
    }
}
