using System;
using System.Collections.Generic;

namespace EternalVR.Launcher.Core.Settings
{
    /// <summary>The launcher.ini keys this launcher knows: read or retired; any other key is kept and written back as it was.</summary>
    public sealed partial class LauncherSettings
    {
        private static readonly HashSet<string> KnownKeys = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
        {
            "schema_version", "game_dir", "layer_dir", "runtime", "world_scale", "mode", "controllers", "aim", "revenant_aim", "melee_aim", "equipment_aim", "render_size",
            "render_scale", "eye_size", "skip_cinematics", "skip_cutscenes", "posture", "height", "ipd_mm", "recenter_hold", "turn", "snap_degrees",
            "turn_rate", "handedness", "show_arms", "locomotion", "aim_dot", "anti_aliasing", "dlss_quality", "dlss_dll", "dlss_version", "dlss_dll_path", "dlss_preset", "sharpening", "screen_reflections", "resolution_base", "cpu_saver", "body_follow", "head_fade", "aim_smoothing", "hud_distance",
            "hud_width", "hud_height", "mirror", "cutscene_view", "cutscene_arms", "cutscene_3d", "shot_origin", "aim_dot_size", "menu_beam", "dossier", "map_sticks", "wheel_select", "thumb_rest_wheel", "thumb_rest_pick", "thumb_rest_face_touch", "thumb_rest_slowdown", "weapon_directions", "roll_wrist", "swing_gesture", "hands_jump", "mirror_display",
            "mirror_size", "mirror_crop", "cinema_aspect", "hud", "vibration", "bhaptics", "bhaptics_intensity", "vignette", "glory_kills", "alternate_eyes", "parallel_eyes", "foveation", "pace", "frame_pacing", "extra_args", "profile",
            "punch_speed", "hold_time", "preset", "profile_unsaved", "profile_check", "adapter_steer",
            // Settings the launcher no longer has: not read, dropped on the next save.
            "throw_gesture", "holsters", "holster_shoulder", "holster_other", "holster_hip", "reach_use", "rumble_route", "new_arms", "wrist_flick",
        };
    }
}
