using System;
using System.Collections.Generic;

namespace EternalVR.Launcher.Core.Settings
{
    /// <summary>
    /// The settings file keys behind each row of the window (<see cref="Setting"/>), so a row can be marked as changed from
    /// its preset and put back by itself (<see cref="ProfileEdits"/>). The CPU Saver's rows are one per item of
    /// <c>data\cpu-saver.txt</c>, each with its own key (<see cref="Launch.CpuSaverItem.Key"/>), and are not listed here.
    /// </summary>
    public static class SettingKeys
    {
        /// <summary>
        /// This machine's keys and the file's own: never part of a profile, never compared with a preset (a profile file has
        /// <c>schema_version</c> and <c>preset</c> of its own).
        /// </summary>
        public static readonly IReadOnlyCollection<string> Machine = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
        {
            "schema_version", "game_dir", "layer_dir", "runtime", "dlss_dll_path", "adapter_steer", "profile", "preset", "profile_unsaved", "profile_check",
        };

        /// <summary>Keys a profile carries that no row shows (set in the file by hand).</summary>
        public static readonly IReadOnlyCollection<string> FileOnly = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
        {
            "render_size", "eye_size", "bhaptics_intensity",
        };

        private static readonly string[] None = new string[0];

        private static readonly Dictionary<Setting, string[]> Keys = new Dictionary<Setting, string[]>
        {
            [Setting.Turning] = new[] { "turn" },
            [Setting.TurnSpeed] = new[] { "turn_rate" },
            [Setting.SnapAngle] = new[] { "snap_degrees" },
            [Setting.Vignette] = new[] { "vignette" },
            [Setting.GloryKills] = new[] { "glory_kills" },
            [Setting.WalkInRoom] = new[] { "body_follow" },
            [Setting.HeadFade] = new[] { "head_fade" },
            [Setting.RecenterHold] = new[] { "recenter_hold" },
            [Setting.SkipCutscenes] = new[] { "skip_cinematics" },
            [Setting.PlayPosition] = new[] { "posture" },
            [Setting.EyeHeight] = new[] { "height" },
            [Setting.ThrowGesture] = new[] { "throw_gesture" },
            [Setting.SwingGesture] = new[] { "swing_gesture" },
            [Setting.HandsJump] = new[] { "hands_jump" },
            [Setting.PunchSpeed] = new[] { "punch_speed" },
            [Setting.AimWith] = new[] { "aim" },
            [Setting.RevenantAimWith] = new[] { "revenant_aim" },
            [Setting.MeleeAimWith] = new[] { "melee_aim" },
            [Setting.EquipmentAimWith] = new[] { "equipment_aim" },
            [Setting.WeaponHand] = new[] { "handedness" },
            [Setting.Arms] = new[] { "show_arms" },
            [Setting.MoveToward] = new[] { "locomotion" },
            [Setting.XButton] = new[] { "dossier" },
            [Setting.DossierMapSticks] = new[] { "map_sticks" },
            [Setting.WeaponWheel] = new[] { "wheel_select" },
            [Setting.ThumbRestWheel] = new[] { "thumb_rest_wheel" },
            [Setting.ThumbRestPicks] = new[] { "thumb_rest_pick" },
            [Setting.WeaponDirections] = new[] { "weapon_directions" },
            [Setting.ThumbRestFaceTouch] = new[] { "thumb_rest_face_touch" },
            [Setting.ThumbRestSlowdown] = new[] { "thumb_rest_slowdown" },
            [Setting.HoldTime] = new[] { "hold_time" },
            [Setting.AimSteadiness] = new[] { "aim_smoothing" },
            [Setting.AimDot] = new[] { "aim_dot" },
            [Setting.AimDotSize] = new[] { "aim_dot_size" },
            [Setting.Vibration] = new[] { "vibration" },
            [Setting.Bhaptics] = new[] { "bhaptics" },
            [Setting.Resolution] = new[] { "resolution_base", "render_scale" },
            [Setting.AntiAliasing] = new[] { "anti_aliasing" },
            [Setting.Sharpening] = new[] { "sharpening" },
            [Setting.ScreenReflections] = new[] { "screen_reflections" },
            [Setting.Foveation] = new[] { "foveation" },
            [Setting.FramePacing] = new[] { "frame_pacing" },
            [Setting.ParallelEyes] = new[] { "parallel_eyes" },
            [Setting.DlssQuality] = new[] { "dlss_quality" },
            [Setting.DlssVersion] = new[] { "dlss_version" },
            [Setting.DlssPreset] = new[] { "dlss_preset" },
            [Setting.VrMode] = new[] { "mode" },
            [Setting.AlternateEyes] = new[] { "alternate_eyes" },
            [Setting.WorldSize] = new[] { "world_scale" },
            [Setting.EyeDistance] = new[] { "ipd_mm" },
            [Setting.DesktopWindow] = new[] { "mirror" },
            [Setting.DesktopMonitor] = new[] { "mirror_display" },
            [Setting.DesktopSize] = new[] { "mirror_size" },
            [Setting.DesktopCrop] = new[] { "mirror_crop" },
            [Setting.CutsceneView] = new[] { "cutscene_view" },
            [Setting.CutsceneShape] = new[] { "cinema_aspect" },
            [Setting.CutsceneArms] = new[] { "cutscene_arms" },
            [Setting.HudDistance] = new[] { "hud_distance" },
            [Setting.HudSize] = new[] { "hud_width" },
            [Setting.HudHeight] = new[] { "hud_height" },
            [Setting.HudPlace] = new[] { "hud" },
            [Setting.MotionControllers] = new[] { "controllers" },
            [Setting.ShotsFrom] = new[] { "shot_origin" },
            [Setting.MenuLaser] = new[] { "menu_beam" },
            [Setting.ExtraArguments] = new[] { "extra_args" },
        };

        /// <summary>
        /// The keys of <paramref name="setting"/>'s row; none for a row that only shows something (the headset, Each eye, What
        /// runs), this machine's (the game folder, the runtime, the headset's graphics card), the controls (kept per profile in their own folder) and the CPU
        /// Saver's rows (one key per item).
        /// </summary>
        public static IReadOnlyList<string> Of(Setting setting) => Keys.TryGetValue(setting, out var keys) ? keys : None;
    }
}
