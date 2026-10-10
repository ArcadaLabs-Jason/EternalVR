using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;

namespace EternalVR.Launcher.Core.Settings
{
    /// <summary>What drives the aim: the weapon hand (motion controllers), the head, or the game's own (mouse).</summary>
    public enum AimMode { Hand, Head, View }

    /// <summary>What aims the Revenant while the player pilots it in Cultist Base (the layer's <c>ETERNALVR_DEMON_AIM</c>): the
    /// same as <see cref="AimMode"/>, the weapon hand, or the head.</summary>
    public enum RevenantAimMode { Same, Hand, Head }

    /// <summary>Stereo (Route S: one render per eye, docs/VR_STEREO.md) or head-tracked mono.</summary>
    public enum VrMode { Stereo, Mono }

    /// <summary>Alternate eyes (the layer's <c>ETERNALVR_ALTERNATE_EYES</c>, docs/rig-findings/alternate-eye.md): never,
    /// only while the processor cannot keep up with the headset (auto), or always (one eye per game tick).</summary>
    public enum AlternateEyesMode { Off, Auto, On }

    /// <summary>Posture override (REQ-06, docs/VR_ROOMSCALE.md): detected, or forced seated or standing.</summary>
    public enum PostureMode { Auto, Seated, Standing }

    /// <summary>Eye height: the game's (Slayer) or the player's own above the floor (Real).</summary>
    public enum HeightMode { Slayer, Real }

    /// <summary>How the right stick turns (the layer's <c>ETERNALVR_TURN</c>).</summary>
    public enum TurnMode { Smooth, Snap, Off }

    /// <summary>The comfort vignette (the layer's <c>ETERNALVR_VIGNETTE</c>): the edges of the view darken while the stick moves or turns you.</summary>
    public enum VignetteMode { Off, Light, Strong }

    /// <summary>The weapon hand (the layer's <c>ETERNALVR_HANDEDNESS</c>): left swaps the buttons, left mirrored also the sticks.</summary>
    public enum Handedness { Right, Left, LeftMirrored }

    /// <summary>Which press of the X button opens the Dossier (the layer's <c>ETERNALVR_DOSSIER</c>); the other one switches equipment.</summary>
    public enum DossierPress { Hold, Tap }

    /// <summary>What points at the weapon wheel (the layer's <c>ETERNALVR_WHEEL_SELECT</c>): the stick that holds it open, or the weapon hand.</summary>
    public enum WheelSelect { Stick, Hand }

    /// <summary>Fixed foveated rendering in stereo (the layer's <c>ETERNALVR_FOVEATION</c>, experimental, NVIDIA RTX only): the edges
    /// of each eye shaded at a lower rate, from the gentlest preset to the strongest.</summary>
    public enum FoveationMode { Off, Subtle, Balanced, Aggressive, Maximum }

    /// <summary>What the game's desktop window shows during stereo (the layer's <c>ETERNALVR_MIRROR</c>): one eye, or black.</summary>
    public enum MirrorMode { Left, Right, Off }

    /// <summary>Where cutscenes play (the layer's <c>ETERNALVR_CUTSCENES</c>): on a flat screen in front of you, or around you.</summary>
    public enum CutsceneView { Cinema, Immersive }

    /// <summary>Where shots start under hand aim (the layer's <c>ETERNALVR_SHOT_ORIGIN</c>).</summary>
    public enum ShotOrigin { Hand, Eye }

    /// <summary>Where the HUD is (the layer's <c>ETERNALVR_HUD</c>, docs/VR_HANDS_HUD.md): all on the panel in front, health, armor and ammo on the off hand's wrist, or the ammo above the gun in the weapon hand.</summary>
    public enum HudMode { Panel, Wrist, Weapon }

    /// <summary>
    /// The launcher's own per-machine settings (<c>%LOCALAPPDATA%\EternalVR\launcher.ini</c>), a flat
    /// <c>key = value</c> file with a <c>schema_version</c> (T-093, T-106). A newer schema is refused and
    /// the file left untouched. This is not the layer's TOML settings file (ARCHITECTURE section 12),
    /// which a later launcher version writes.
    /// Schema 2 added the mode, the controllers and hand aim. The room-scale keys (posture, height, ipd_mm,
    /// recenter_hold) are optional in schema 2: a file without them takes the defaults, and an older launcher
    /// ignores them. The render size keys (render_size, render_scale) are optional the same way, and so are the controls keys
    /// (turn, snap_degrees, turn_rate, handedness, locomotion, aim_dot) and anti_aliasing, and so are body_follow,
    /// aim_smoothing, hud_distance, hud_width, hud_height, mirror, cutscene_view, cutscene_arms, shot_origin, aim_dot_size, menu_beam, dossier, map_sticks,
    /// wheel_select, thumb_rest_wheel, thumb_rest_pick, thumb_rest_face_touch, thumb_rest_slowdown, weapon_directions, throw_gesture, swing_gesture, mirror_display, mirror_size, mirror_crop, cinema_aspect, hud, vibration, vignette, alternate_eyes, profile,
    /// revenant_aim, melee_aim, equipment_aim, bhaptics, bhaptics_intensity, foveation, glory_kills, dlss_version, sharpening, resolution_base, parallel_eyes, punch_speed and hold_time
    /// (dlss_version replaced dlss_dll, which is still read once).
    /// Keys this launcher does not know (a newer launcher's optional ones) are kept and written back as they were.
    /// A schema 1 file keeps its paths, runtime, world
    /// scale, cutscene and argument choices and takes the new defaults for the rest (stereo, controllers on,
    /// hand aim); its <c>aim = view</c> is kept, its <c>aim = head</c> (schema 1's default) becomes hand aim.
    /// </summary>
    public sealed partial class LauncherSettings
    {
        public const int SchemaVersion = 2;
        public const double MinWorldScale = 0.85;
        public const double MaxWorldScale = 1.20;
        /// <summary>IPD override range in millimetres; 0 means the runtime's.</summary>
        public const double MinIpdMm = 50.0;
        public const double MaxIpdMm = 80.0;
        /// <summary>Runtime value meaning "the system's active OpenXR runtime" (no XR_RUNTIME_JSON).</summary>
        public const string SystemRuntime = "system";
        /// <summary>Per-eye image in stereo: the game window's client size (docs/VR_STEREO.md, S4).</summary>
        public const int DefaultEyeWidth = 2064;
        public const int DefaultEyeHeight = 2100;
        public const int MinEyeSize = 640;
        public const int MaxEyeSize = 4096;
        /// <summary>The layer's render size: the headset's recommended eye size (docs/rig-findings/render-size.md).</summary>
        public const string RenderSizeAuto = "auto";
        /// <summary>The game renders at its window's size (<see cref="EyeWidth"/> fitted to a display), as before.</summary>
        public const string RenderSizeOff = "off";
        public const double MinRenderScale = 0.5;
        public const double MaxRenderScale = 2.0;
        /// <summary>The layer's ranges (src/features/input/turn_policy.hpp) and defaults.</summary>
        public const double MinSnapDegrees = 15.0;
        public const double MaxSnapDegrees = 90.0;
        public const double DefaultSnapDegrees = 45.0;
        public const double MinTurnRate = 150.0;
        public const double MaxTurnRate = 400.0;
        public const double DefaultTurnRate = 230.0;
        /// <summary>Hand-aim smoothing, 0 (off) to 1 (the strongest); the layer's default.</summary>
        public const double DefaultAimSmoothing = 0.3;
        /// <summary>Controller vibration strength, 0 (off) to 1 (the strongest); the layer's default.</summary>
        public const double DefaultVibration = 0.6;
        /// <summary>How fast a hand must move to punch, metres per second (the layer's <c>ETERNALVR_PUNCH_SPEED</c>).</summary>
        public const double MinPunchSpeed = 1.0, MaxPunchSpeed = 4.0, DefaultPunchSpeed = 2.8;
        /// <summary>How long a button is held before its hold action starts, seconds (<c>ETERNALVR_HOLD_SECONDS</c>).</summary>
        public const double MinHoldTime = 0.1, MaxHoldTime = 1.0, DefaultHoldTime = 0.25;
        /// <summary>The bHaptics effects' strength, 0 to 1 (the layer's <c>ETERNALVR_BHAPTICS_INTENSITY</c>); set in the file only.</summary>
        public const double DefaultBhapticsIntensity = 1.0;
        /// <summary>The HUD panel (src/ui_layer/ui_settings.hpp): distance ahead, width, height offset, in metres.</summary>
        public const double MinHudDistance = 0.3, MaxHudDistance = 10.0, DefaultHudDistance = 1.5;
        public const double MinHudWidth = 0.1, MaxHudWidth = 10.0, DefaultHudWidth = 2.0;
        public const double MinHudHeight = -2.0, MaxHudHeight = 2.0;
        /// <summary>The aim dot's size in degrees (the layer takes 0.1 to 10).</summary>
        public const double MinAimDotSize = 0.2, MaxAimDotSize = 5.0, DefaultAimDotSize = 1.0;

        /// <summary>Game folder override; empty means discover through Steam.</summary>
        public string GameDir { get; set; } = string.Empty;
        /// <summary>Layer folder override; empty means the <c>layer</c> folder next to the launcher.</summary>
        public string LayerDir { get; set; } = string.Empty;
        /// <summary><see cref="SystemRuntime"/> or the path of an OpenXR runtime manifest.</summary>
        public string Runtime { get; set; } = SystemRuntime;
        public double WorldScale { get; set; } = 1.0;
        public VrMode Mode { get; set; } = VrMode.Stereo;
        /// <summary>Stereo renders one eye per game tick, each eye every other tick, for slower processors (the layer's
        /// <c>ETERNALVR_ALTERNATE_EYES</c>, docs/rig-findings/alternate-eye.md): always, only while the processor cannot keep
        /// up with the headset (auto), or never. Off by default.</summary>
        public AlternateEyesMode AlternateEyes { get; set; } = AlternateEyesMode.Off;
        /// <summary>Stereo starts both eyes' rendering work at the same time instead of eye L's frame and then eye R's
        /// (<c>ETERNALVR_PARALLEL_EYES=1</c>: the layer's two-view renderer on the game version it knows, docs/VR_STEREO.md
        /// "Parallel Eye Rendering"). Experimental, off by default.</summary>
        public bool ParallelEyes { get; set; } = false;
        /// <summary>Parallel Eye Rendering asked of the layer in this launch: on and in stereo (with DLSS each view runs its
        /// own DLSS feature, src/vkcore/view_dlss.hpp).</summary>
        public bool ParallelEyesAsked => ParallelEyes && Mode == VrMode.Stereo;
        /// <summary>Parallel Eye Rendering runs in this launch: asked (<see cref="ParallelEyesAsked"/>) on a game build it
        /// runs on (<paramref name="gameRuns"/>, <see cref="Game.BuildCheck.RunsParallelEyes"/>). On any other build the layer
        /// would refuse it and keep the standard renderer: the launch does not ask for it, and Alternate eyes and Foveation
        /// apply as without it.</summary>
        public bool ParallelEyesOn(bool gameRuns) => gameRuns && ParallelEyesAsked;
        /// <summary>Motion controllers drive the game (docs/VR_CONTROLLERS.md); off leaves keyboard, mouse and pad only.</summary>
        public bool Controllers { get; set; } = true;
        public AimMode Aim { get; set; } = AimMode.Hand;
        /// <summary>What aims the Revenant while piloting it: the same as <see cref="Aim"/> (default), the weapon hand, or the head.</summary>
        public RevenantAimMode RevenantAim { get; set; } = RevenantAimMode.Same;
        /// <summary>
        /// Stereo: <see cref="RenderSizeAuto"/> (each eye at the headset's recommended size times the render scale,
        /// whatever the displays), <c>WxH</c> (that size), or <see cref="RenderSizeOff"/> (the window's size).
        /// </summary>
        public string RenderSize { get; set; } = RenderSizeAuto;
        /// <summary>Multiplies the headset's recommended size (1.0: the recommendation within the default pixel budget).</summary>
        public double RenderScale { get; set; } = 1.0;
        /// <summary>Stereo per-eye size before it is fitted to the display (<c>StereoWindow</c>) when the render size is off.</summary>
        public int EyeWidth { get; set; } = DefaultEyeWidth;
        public int EyeHeight { get; set; } = DefaultEyeHeight;
        public bool SkipCinematics { get; set; } = true;
        public PostureMode Posture { get; set; } = PostureMode.Auto;
        public HeightMode Height { get; set; } = HeightMode.Slayer;
        /// <summary>The eye separation the game renders with, in millimetres; 0 = the runtime's.</summary>
        public double IpdMm { get; set; } = 0.0;
        /// <summary>Both sticks held pressed (2 s) recenter the room.</summary>
        public bool RecenterLongPress { get; set; } = true;
        public TurnMode Turn { get; set; } = TurnMode.Smooth;
        /// <summary>Degrees per snap turn.</summary>
        public double SnapDegrees { get; set; } = DefaultSnapDegrees;
        /// <summary>Smooth turn speed at full deflection, degrees per second.</summary>
        public double TurnRate { get; set; } = DefaultTurnRate;
        /// <summary>The comfort vignette while the stick moves or turns you; off by default.</summary>
        public VignetteMode Vignette { get; set; } = VignetteMode.Off;
        public Handedness Hand { get; set; } = Handedness.Right;
        /// <summary>The first-person arms drawn (default) or hidden, the weapon alone (the layer's <c>ETERNALVR_ARMS</c>).</summary>
        public bool ShowArms { get; set; } = true;
        public DossierPress Dossier { get; set; } = DossierPress.Hold;
        /// <summary>What points at the weapon wheel: the stick (default) or the weapon hand.</summary>
        public WheelSelect Wheel { get; set; } = WheelSelect.Stick;
        /// <summary>The dot at the end of the weapon hand's aim ray (the layer's <c>ETERNALVR_UI_RETICLE</c>).</summary>
        public bool AimDot { get; set; } = true;
        /// <summary>Fixed foveated rendering in stereo (<see cref="FoveationMode"/>); off by default.</summary>
        public FoveationMode Foveation { get; set; } = FoveationMode.Off;
        /// <summary>The layer's <c>ETERNALVR_AIM_SMOOTHING</c>, 0 to 1.</summary>
        public double AimSmoothing { get; set; } = DefaultAimSmoothing;
        /// <summary>The layer's <c>ETERNALVR_HAPTICS</c>, 0 (off) to 1.</summary>
        public double Vibration { get; set; } = DefaultVibration;
        /// <summary>The layer's <c>ETERNALVR_PUNCH_SPEED</c>, metres per second.</summary>
        public double PunchSpeed { get; set; } = DefaultPunchSpeed;
        /// <summary>The layer's <c>ETERNALVR_HOLD_SECONDS</c>.</summary>
        public double HoldTime { get; set; } = DefaultHoldTime;
        /// <summary>bHaptics suits and sleeves through the bHaptics Player (the layer's <c>ETERNALVR_BHAPTICS</c>); off by default.</summary>
        public bool Bhaptics { get; set; } = false;
        /// <summary>The layer's <c>ETERNALVR_BHAPTICS_INTENSITY</c>, 0 to 1.</summary>
        public double BhapticsIntensity { get; set; } = DefaultBhapticsIntensity;
        public double HudDistance { get; set; } = DefaultHudDistance;
        public double HudWidth { get; set; } = DefaultHudWidth;
        /// <summary>Metres up (negative: down) from eye level.</summary>
        public double HudHeight { get; set; } = 0.0;
        public MirrorMode Mirror { get; set; } = MirrorMode.Left;
        public CutsceneView Cutscenes { get; set; } = CutsceneView.Cinema;
        /// <summary>The first-person arms in cutscenes around you: hidden (default) or drawn as the game has them (the layer's <c>ETERNALVR_CUTSCENE_ARMS</c>).</summary>
        public bool CutsceneArms { get; set; } = false;
        /// <summary>The cutscene screen's shape (<see cref="MirrorSettings"/>).</summary>
        public CinemaShape Cinema { get; set; } = CinemaShape.Wide16x9;
        /// <summary>The desktop mirror's display: <c>auto</c>, <c>primary</c> or a desktop point <c>x,y</c> (<see cref="MirrorSettings"/>).</summary>
        public string MirrorDisplay { get; set; } = MirrorSettings.DisplayAuto;
        /// <summary>The desktop mirror's client size, <c>WxH</c>.</summary>
        public string MirrorSize { get; set; } = MirrorSettings.DefaultSize;
        /// <summary>The desktop mirror shows only the eye image's centred 16:9 band.</summary>
        public bool MirrorCrop { get; set; } = false;
        public ShotOrigin Shots { get; set; } = ShotOrigin.Hand;
        public double AimDotSize { get; set; } = DefaultAimDotSize;
        /// <summary>The laser from the hand to the menu panel (the layer's <c>ETERNALVR_MENU_BEAM</c>); off keeps only the dot.</summary>
        public bool MenuBeam { get; set; } = true;
        /// <summary>The whole HUD on the panel (default), health, armor and ammo on the off hand's wrist, or the ammo on the weapon.</summary>
        public HudMode Hud { get; set; } = HudMode.Panel;
        /// <summary>Extra command-line text, checked by the argument policy.</summary>
        public string ExtraArguments { get; set; } = string.Empty;
        /// <summary>The player profile these settings are saved to as well (<see cref="ProfileStore"/>); empty for none.</summary>
        public string Profile { get; set; } = string.Empty;
        /// <summary>Keys of the file this launcher does not know, in file order: written back unchanged.</summary>
        public IReadOnlyList<KeyValuePair<string, string>> UnknownKeys { get; private set; } = new KeyValuePair<string, string>[0];

        public LauncherSettings Clone() => (LauncherSettings)MemberwiseClone();

        /// <summary>The defaults, keeping the folders, the runtime and the keys this launcher does not know ("Reset to defaults").</summary>
        public LauncherSettings WithDefaults() => new LauncherSettings
        {
            GameDir = GameDir, LayerDir = LayerDir, Runtime = Runtime, UnknownKeys = UnknownKeys, Profile = Profile, DlssDllPath = DlssDllPath,
            // Written by hand only, so the window has nothing to reset it to.
            WeaponDirections = WeaponDirections,
        };

        private static readonly HashSet<string> KnownKeys = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
        {
            "schema_version", "game_dir", "layer_dir", "runtime", "world_scale", "mode", "controllers", "aim", "revenant_aim", "melee_aim", "equipment_aim", "render_size",
            "render_scale", "eye_size", "skip_cinematics", "posture", "height", "ipd_mm", "recenter_hold", "turn", "snap_degrees",
            "turn_rate", "handedness", "show_arms", "locomotion", "aim_dot", "anti_aliasing", "dlss_quality", "dlss_dll", "dlss_version", "dlss_dll_path", "dlss_preset", "sharpening", "screen_reflections", "resolution_base", "cpu_saver", "body_follow", "head_fade", "aim_smoothing", "hud_distance",
            "hud_width", "hud_height", "mirror", "cutscene_view", "cutscene_arms", "shot_origin", "aim_dot_size", "menu_beam", "dossier", "map_sticks", "wheel_select", "thumb_rest_wheel", "thumb_rest_pick", "thumb_rest_face_touch", "thumb_rest_slowdown", "weapon_directions", "throw_gesture", "swing_gesture", "hands_jump", "mirror_display",
            "mirror_size", "mirror_crop", "cinema_aspect", "hud", "vibration", "bhaptics", "bhaptics_intensity", "vignette", "glory_kills", "alternate_eyes", "parallel_eyes", "foveation", "pace", "frame_pacing", "extra_args", "profile",
            "punch_speed", "hold_time",
        };

        /// <summary><paramref name="v"/> within [min, max]; <paramref name="fallback"/> when it is not a number.</summary>
        public static double Clamp(double v, double min, double max, double fallback) =>
            double.IsNaN(v) || double.IsInfinity(v) ? fallback : Math.Max(min, Math.Min(max, v));

        public static double ClampRenderScale(double v) =>
            double.IsNaN(v) ? 1.0 : Math.Max(MinRenderScale, Math.Min(MaxRenderScale, v));

        /// <summary>The render size setting normalised (<c>auto</c>, <c>off</c>, <c>WxH</c>), or null when it is none of them.</summary>
        public static string NormaliseRenderSize(string text)
        {
            var t = (text ?? string.Empty).Trim().ToLowerInvariant();
            if (t == RenderSizeAuto || t == RenderSizeOff) return t;
            var parts = t.Split('x');
            if (parts.Length == 2
                && int.TryParse(parts[0], NumberStyles.None, CultureInfo.InvariantCulture, out var w)
                && int.TryParse(parts[1], NumberStyles.None, CultureInfo.InvariantCulture, out var h)
                && w >= 256 && h >= 256 && w <= 8192 && h <= 8192)
                return w.ToString(CultureInfo.InvariantCulture) + "x" + h.ToString(CultureInfo.InvariantCulture);
            return null;
        }

        public static double ClampWorldScale(double v) =>
            double.IsNaN(v) ? 1.0 : Math.Max(MinWorldScale, Math.Min(MaxWorldScale, v));

        public static double ClampSnapDegrees(double v) =>
            double.IsNaN(v) ? DefaultSnapDegrees : Math.Max(MinSnapDegrees, Math.Min(MaxSnapDegrees, v));

        public static double ClampTurnRate(double v) =>
            double.IsNaN(v) ? DefaultTurnRate : Math.Max(MinTurnRate, Math.Min(MaxTurnRate, v));

        /// <summary>0 (the runtime's), or a value within the IPD range.</summary>
        public static double ClampIpd(double v) =>
            double.IsNaN(v) || v <= 0.0 ? 0.0 : Math.Max(MinIpdMm, Math.Min(MaxIpdMm, v));

        public static LauncherSettings Parse(string text)
        {
            var map = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            var order = new List<string>();
            foreach (var raw in (text ?? string.Empty).Replace("\r\n", "\n").Split('\n'))
            {
                var line = raw.Trim();
                if (line.Length == 0 || line.StartsWith("#", StringComparison.Ordinal) || line.StartsWith(";", StringComparison.Ordinal)) continue;
                int eq = line.IndexOf('=');
                if (eq <= 0) continue;
                var key = line.Substring(0, eq).Trim();
                if (!map.ContainsKey(key)) order.Add(key);
                map[key] = line.Substring(eq + 1).Trim();
            }

            int version = 0;
            if (map.TryGetValue("schema_version", out var v) && !int.TryParse(v, NumberStyles.Integer, CultureInfo.InvariantCulture, out version))
                throw new SettingsException("schema_version is not a number: " + v);
            if (version > SchemaVersion)
                throw new SettingsException($"The settings file has schema_version {version}, newer than this launcher understands ({SchemaVersion}). Update the launcher; the file was left unchanged.");

            var s = new LauncherSettings();
            if (map.TryGetValue("game_dir", out var g)) s.GameDir = g;
            if (map.TryGetValue("layer_dir", out var l)) s.LayerDir = l;
            if (map.TryGetValue("runtime", out var r) && r.Length > 0) s.Runtime = r;
            if (map.TryGetValue("world_scale", out var w) && double.TryParse(w, NumberStyles.Float, CultureInfo.InvariantCulture, out var scale))
                s.WorldScale = ClampWorldScale(scale);
            if (map.TryGetValue("aim", out var a)) s.Aim = ParseAim(a, version);
            if (map.TryGetValue("revenant_aim", out var ra))
                s.RevenantAim = Pick(ra, RevenantAimMode.Same, ("hand", RevenantAimMode.Hand), ("head", RevenantAimMode.Head));
            s.ReadActionAim(map);
            if (version >= 2)
            {
                if (map.TryGetValue("mode", out var m)) s.Mode = string.Equals(m, "mono", StringComparison.OrdinalIgnoreCase) ? VrMode.Mono : VrMode.Stereo;
                if (map.TryGetValue("controllers", out var k)) s.Controllers = !(k == "0" || string.Equals(k, "false", StringComparison.OrdinalIgnoreCase));
                if (map.TryGetValue("eye_size", out var z) && TryParseSize(z, out var ew, out var eh))
                {
                    s.EyeWidth = ew;
                    s.EyeHeight = eh;
                }
            }
            if (map.TryGetValue("alternate_eyes", out var ae)) s.AlternateEyes = ParseAlternateEyes(ae);
            if (map.TryGetValue("parallel_eyes", out var pe)) s.ParallelEyes = On(pe);
            if (map.TryGetValue("render_size", out var rs) && NormaliseRenderSize(rs) is string renderSize) s.RenderSize = renderSize;
            if (map.TryGetValue("render_scale", out var sc))
            {
                if (string.Equals(sc.Trim(), "auto", StringComparison.OrdinalIgnoreCase)) s.RenderScale = 1.0;
                else if (double.TryParse(sc, NumberStyles.Float, CultureInfo.InvariantCulture, out var scaleValue)) s.RenderScale = ClampRenderScale(scaleValue);
            }
            if (map.TryGetValue("posture", out var p)) s.Posture = ParsePosture(p);
            if (map.TryGetValue("height", out var h)) s.Height = string.Equals(h, "real", StringComparison.OrdinalIgnoreCase) ? HeightMode.Real : HeightMode.Slayer;
            if (map.TryGetValue("ipd_mm", out var ipd) && double.TryParse(ipd, NumberStyles.Float, CultureInfo.InvariantCulture, out var ipdMm))
                s.IpdMm = ClampIpd(ipdMm);
            if (map.TryGetValue("recenter_hold", out var rh)) s.RecenterLongPress = !(rh == "0" || string.Equals(rh, "false", StringComparison.OrdinalIgnoreCase));
            if (map.TryGetValue("turn", out var tm)) s.Turn = Pick(tm, TurnMode.Smooth, ("snap", TurnMode.Snap), ("off", TurnMode.Off));
            if (map.TryGetValue("snap_degrees", out var sd) && double.TryParse(sd, NumberStyles.Float, CultureInfo.InvariantCulture, out var snap))
                s.SnapDegrees = ClampSnapDegrees(snap);
            if (map.TryGetValue("turn_rate", out var tr) && double.TryParse(tr, NumberStyles.Float, CultureInfo.InvariantCulture, out var rate))
                s.TurnRate = ClampTurnRate(rate);
            if (map.TryGetValue("vignette", out var vg)) s.Vignette = Pick(vg, VignetteMode.Off, ("light", VignetteMode.Light), ("strong", VignetteMode.Strong));
            if (map.TryGetValue("glory_kills", out var gk)) s.GloryKills = ParseGloryKills(gk);
            if (map.TryGetValue("handedness", out var hd)) s.Hand = Pick(hd, Handedness.Right, ("left", Handedness.Left), ("left_mirror", Handedness.LeftMirrored));
            if (map.TryGetValue("show_arms", out var sa)) s.ShowArms = !(sa == "0" || string.Equals(sa, "false", StringComparison.OrdinalIgnoreCase));
            if (map.TryGetValue("aim_dot", out var ad)) s.AimDot = !(ad == "0" || string.Equals(ad, "false", StringComparison.OrdinalIgnoreCase));
            s.ReadLocomotion(map);
            if (map.TryGetValue("dossier", out var dp)) s.Dossier = Pick(dp, DossierPress.Hold, ("tap", DossierPress.Tap));
            s.ReadMapSticks(map);
            if (map.TryGetValue("wheel_select", out var ws)) s.Wheel = Pick(ws, WheelSelect.Stick, ("hand", WheelSelect.Hand));
            s.ReadThumbRest(map);
            s.ReadGestures(map);
            s.ReadPicture(map);
            s.ReadPacing(map);
            if (map.TryGetValue("foveation", out var fv))
                s.Foveation = Pick(fv, FoveationMode.Off, ("subtle", FoveationMode.Subtle), ("balanced", FoveationMode.Balanced), ("aggressive", FoveationMode.Aggressive), ("maximum", FoveationMode.Maximum));
            if (map.TryGetValue("cpu_saver", out var cs)) s.CpuSaverAllOn = On(cs);
            s.CpuSaverChoices = order.Where(IsCpuSaverKey).Select(k => (Key: k.Substring(CpuSaverKeyPrefix.Length).ToLowerInvariant(), On: Switch(map[k])))
                .Where(x => x.On.HasValue).Select(x => new KeyValuePair<string, bool>(x.Key, x.On.Value)).ToList();
            if (map.TryGetValue("skip_cinematics", out var c)) s.SkipCinematics = c == "1" || string.Equals(c, "true", StringComparison.OrdinalIgnoreCase);
            s.ReadRoom(map);
            if (map.TryGetValue("aim_smoothing", out var sm)) s.AimSmoothing = Number(sm, 0.0, 1.0, DefaultAimSmoothing);
            if (map.TryGetValue("vibration", out var vb)) s.Vibration = Number(vb, 0.0, 1.0, DefaultVibration);
            if (map.TryGetValue("punch_speed", out var ps)) s.PunchSpeed = Number(ps, MinPunchSpeed, MaxPunchSpeed, DefaultPunchSpeed);
            if (map.TryGetValue("hold_time", out var ht)) s.HoldTime = Number(ht, MinHoldTime, MaxHoldTime, DefaultHoldTime);
            if (map.TryGetValue("bhaptics", out var bh)) s.Bhaptics = On(bh);
            if (map.TryGetValue("bhaptics_intensity", out var bi)) s.BhapticsIntensity = Number(bi, 0.0, 1.0, DefaultBhapticsIntensity);
            if (map.TryGetValue("hud_distance", out var hd2)) s.HudDistance = Number(hd2, MinHudDistance, MaxHudDistance, DefaultHudDistance);
            if (map.TryGetValue("hud_width", out var hw)) s.HudWidth = Number(hw, MinHudWidth, MaxHudWidth, DefaultHudWidth);
            if (map.TryGetValue("hud_height", out var hh)) s.HudHeight = Number(hh, MinHudHeight, MaxHudHeight, 0.0);
            if (map.TryGetValue("mirror", out var mi)) s.Mirror = Pick(mi, MirrorMode.Left, ("right", MirrorMode.Right), ("off", MirrorMode.Off));
            if (map.TryGetValue("cutscene_view", out var cv)) s.Cutscenes = Pick(cv, CutsceneView.Cinema, ("immersive", CutsceneView.Immersive));
            if (map.TryGetValue("cutscene_arms", out var cuta)) s.CutsceneArms = On(cuta);
            if (map.TryGetValue("shot_origin", out var so)) s.Shots = Pick(so, ShotOrigin.Hand, ("eye", ShotOrigin.Eye));
            if (map.TryGetValue("aim_dot_size", out var ds)) s.AimDotSize = Number(ds, MinAimDotSize, MaxAimDotSize, DefaultAimDotSize);
            if (map.TryGetValue("menu_beam", out var mb)) s.MenuBeam = Flag(mb);
            if (map.TryGetValue("mirror_display", out var md) && MirrorSettings.NormaliseDisplay(md) is string display) s.MirrorDisplay = display;
            if (map.TryGetValue("mirror_size", out var ms) && MirrorSettings.NormaliseSize(ms) is string size) s.MirrorSize = size;
            if (map.TryGetValue("mirror_crop", out var mc)) s.MirrorCrop = mc.Trim() == "16:9";
            if (map.TryGetValue("cinema_aspect", out var ca)) s.Cinema = MirrorSettings.ParseCinema(ca);
            if (map.TryGetValue("hud", out var hu)) s.Hud = Pick(hu, HudMode.Panel, ("wrist", HudMode.Wrist), ("weapon", HudMode.Weapon));
            if (map.TryGetValue("extra_args", out var e)) s.ExtraArguments = e;
            if (map.TryGetValue("profile", out var pr)) s.Profile = ProfileStore.NormaliseName(pr) ?? string.Empty;
            s.UnknownKeys = order.Where(k => !KnownKeys.Contains(k) && !IsCpuSaverKey(k)).Select(k => new KeyValuePair<string, string>(k, map[k])).ToList();
            return s;
        }

        /// <summary>A switch: off for 0 or false, on for anything else.</summary>
        private static bool Flag(string text) => !(text == "0" || string.Equals(text, "false", StringComparison.OrdinalIgnoreCase));

        /// <summary>A switch that is off by default: on only for 1, true or on.</summary>
        private static bool On(string text)
        {
            var t = text.Trim();
            return t == "1" || string.Equals(t, "true", StringComparison.OrdinalIgnoreCase) || string.Equals(t, "on", StringComparison.OrdinalIgnoreCase);
        }

        private static double Number(string text, double min, double max, double fallback) =>
            double.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out var v) ? Clamp(v, min, max, fallback) : fallback;

        private static string Metres(double v) => v.ToString("0.00", CultureInfo.InvariantCulture);

        public string Serialize()
        {
            var sb = new StringBuilder();
            sb.AppendLine("# EternalVR launcher settings. Written by the launcher.");
            sb.AppendLine("schema_version = " + SchemaVersion.ToString(CultureInfo.InvariantCulture));
            sb.AppendLine("game_dir = " + OneLine(GameDir));
            sb.AppendLine("layer_dir = " + OneLine(LayerDir));
            sb.AppendLine("runtime = " + OneLine(Runtime));
            sb.AppendLine("world_scale = " + ClampWorldScale(WorldScale).ToString("0.00", CultureInfo.InvariantCulture));
            sb.AppendLine("mode = " + (Mode == VrMode.Mono ? "mono" : "stereo"));
            sb.AppendLine("alternate_eyes = " + AlternateEyesValue(AlternateEyes));
            sb.AppendLine("parallel_eyes = " + (ParallelEyes ? "1" : "0"));
            sb.AppendLine("controllers = " + (Controllers ? "1" : "0"));
            sb.AppendLine("aim = " + AimName(Aim));
            sb.AppendLine("revenant_aim = " + RevenantAimName(RevenantAim));
            WriteActionAim(sb);
            sb.AppendLine("render_size = " + (NormaliseRenderSize(RenderSize) ?? RenderSizeAuto));
            sb.AppendLine("render_scale = " + ClampRenderScale(RenderScale).ToString("0.00", CultureInfo.InvariantCulture));
            sb.AppendLine("eye_size = " + EyeWidth.ToString(CultureInfo.InvariantCulture) + "x" + EyeHeight.ToString(CultureInfo.InvariantCulture));
            sb.AppendLine("skip_cinematics = " + (SkipCinematics ? "1" : "0"));
            sb.AppendLine("posture = " + PostureName(Posture));
            sb.AppendLine("height = " + HeightName(Height));
            sb.AppendLine("ipd_mm = " + ClampIpd(IpdMm).ToString("0.0", CultureInfo.InvariantCulture));
            sb.AppendLine("recenter_hold = " + (RecenterLongPress ? "1" : "0"));
            sb.AppendLine("turn = " + TurnName(Turn));
            sb.AppendLine("snap_degrees = " + ClampSnapDegrees(SnapDegrees).ToString("0", CultureInfo.InvariantCulture));
            sb.AppendLine("turn_rate = " + ClampTurnRate(TurnRate).ToString("0", CultureInfo.InvariantCulture));
            sb.AppendLine("vignette = " + VignetteName(Vignette));
            sb.AppendLine("glory_kills = " + GloryKillName(GloryKills));
            sb.AppendLine("handedness = " + HandednessName(Hand));
            sb.AppendLine("show_arms = " + (ShowArms ? "1" : "0"));
            WriteLocomotion(sb);
            sb.AppendLine("dossier = " + DossierName(Dossier));
            WriteMapSticks(sb);
            sb.AppendLine("wheel_select = " + WheelSelectName(Wheel));
            WriteThumbRest(sb);
            WriteGestures(sb);
            sb.AppendLine("aim_dot = " + (AimDot ? "1" : "0"));
            WritePicture(sb);
            sb.AppendLine("foveation = " + FoveationName(Foveation));
            WritePacing(sb);
            if (CpuSaverAllOn) sb.AppendLine("cpu_saver = on");
            foreach (var kv in CpuSaverChoices) sb.AppendLine(CpuSaverKeyPrefix + kv.Key + " = " + (kv.Value ? "on" : "off"));
            WriteRoom(sb);
            sb.AppendLine("aim_smoothing = " + Metres(Clamp(AimSmoothing, 0.0, 1.0, DefaultAimSmoothing)));
            sb.AppendLine("vibration = " + Metres(Clamp(Vibration, 0.0, 1.0, DefaultVibration)));
            sb.AppendLine("punch_speed = " + Metres(Clamp(PunchSpeed, MinPunchSpeed, MaxPunchSpeed, DefaultPunchSpeed)));
            sb.AppendLine("hold_time = " + Metres(Clamp(HoldTime, MinHoldTime, MaxHoldTime, DefaultHoldTime)));
            sb.AppendLine("bhaptics = " + (Bhaptics ? "1" : "0"));
            sb.AppendLine("bhaptics_intensity = " + Metres(Clamp(BhapticsIntensity, 0.0, 1.0, DefaultBhapticsIntensity)));
            sb.AppendLine("hud_distance = " + Metres(Clamp(HudDistance, MinHudDistance, MaxHudDistance, DefaultHudDistance)));
            sb.AppendLine("hud_width = " + Metres(Clamp(HudWidth, MinHudWidth, MaxHudWidth, DefaultHudWidth)));
            sb.AppendLine("hud_height = " + Metres(Clamp(HudHeight, MinHudHeight, MaxHudHeight, 0.0)));
            sb.AppendLine("mirror = " + MirrorName(Mirror));
            sb.AppendLine("cutscene_view = " + CutsceneName(Cutscenes));
            sb.AppendLine("cutscene_arms = " + (CutsceneArms ? "1" : "0"));
            sb.AppendLine("shot_origin = " + ShotOriginName(Shots));
            sb.AppendLine("aim_dot_size = " + Metres(Clamp(AimDotSize, MinAimDotSize, MaxAimDotSize, DefaultAimDotSize)));
            sb.AppendLine("menu_beam = " + (MenuBeam ? "1" : "0"));
            sb.AppendLine("mirror_display = " + (MirrorSettings.NormaliseDisplay(MirrorDisplay) ?? MirrorSettings.DisplayAuto));
            sb.AppendLine("mirror_size = " + (MirrorSettings.NormaliseSize(MirrorSize) ?? MirrorSettings.DefaultSize));
            sb.AppendLine("mirror_crop = " + (MirrorCrop ? "16:9" : "full"));
            sb.AppendLine("cinema_aspect = " + MirrorSettings.CinemaName(Cinema));
            sb.AppendLine("hud = " + HudName(Hud));
            sb.AppendLine("extra_args = " + OneLine(ExtraArguments));
            if (Profile.Length > 0) sb.AppendLine("profile = " + OneLine(Profile));
            if (UnknownKeys.Count > 0)
            {
                sb.AppendLine("# Kept as found (not known to this launcher version):");
                foreach (var kv in UnknownKeys) sb.AppendLine(kv.Key + " = " + OneLine(kv.Value));
            }
            return sb.ToString();
        }

        public static LauncherSettings Load(string path) =>
            File.Exists(path) ? Parse(File.ReadAllText(path)) : new LauncherSettings();

        /// <summary>Writes atomically (temporary file, then replace).</summary>
        public void Save(string path)
        {
            if (File.Exists(path))
            {
                // Never overwrite a file from a newer launcher.
                Parse(File.ReadAllText(path));
            }
            Directory.CreateDirectory(Path.GetDirectoryName(path));
            FileUtil.WriteAllTextAtomic(path, Serialize());
        }

        /// <summary>The layer's <c>ETERNALVR_AIM</c> value.</summary>
        public static string AimName(AimMode aim) => aim == AimMode.View ? "view" : aim == AimMode.Head ? "head" : "hand";

        /// <summary>The settings file's <c>revenant_aim</c> value: <c>same</c>, <c>hand</c> or <c>head</c>.</summary>
        public static string RevenantAimName(RevenantAimMode aim) =>
            aim == RevenantAimMode.Hand ? "hand" : aim == RevenantAimMode.Head ? "head" : "same";

        private static AimMode ParseAim(string text, int version)
        {
            if (string.Equals(text, "view", StringComparison.OrdinalIgnoreCase)) return AimMode.View;
            // Schema 1 knew only head and view, and wrote head as its default.
            if (version >= 2 && string.Equals(text, "head", StringComparison.OrdinalIgnoreCase)) return AimMode.Head;
            return AimMode.Hand;
        }

        /// <summary>The layer's <c>ETERNALVR_TURN</c> value.</summary>
        public static string TurnName(TurnMode t) => t == TurnMode.Snap ? "snap" : t == TurnMode.Off ? "off" : "smooth";

        /// <summary>The layer's <c>ETERNALVR_VIGNETTE</c> value.</summary>
        public static string VignetteName(VignetteMode v) => v == VignetteMode.Strong ? "strong" : v == VignetteMode.Light ? "light" : "off";

        /// <summary>The layer's <c>ETERNALVR_HANDEDNESS</c> value.</summary>
        public static string HandednessName(Handedness h) => h == Handedness.Left ? "left" : h == Handedness.LeftMirrored ? "left_mirror" : "right";

        /// <summary>The layer's <c>ETERNALVR_DOSSIER</c> value.</summary>
        public static string DossierName(DossierPress d) => d == DossierPress.Tap ? "tap" : "hold";

        /// <summary>The layer's <c>ETERNALVR_WHEEL_SELECT</c> value.</summary>
        public static string WheelSelectName(WheelSelect w) => w == WheelSelect.Hand ? "hand" : "stick";

        /// <summary>The layer's <c>ETERNALVR_MIRROR</c> value.</summary>
        public static string MirrorName(MirrorMode m) => m == MirrorMode.Right ? "right" : m == MirrorMode.Off ? "off" : "left";

        /// <summary>The settings file's and the layer's <c>ETERNALVR_FOVEATION</c> value: off, subtle, balanced, aggressive or maximum.</summary>
        public static string FoveationName(FoveationMode f) =>
            f == FoveationMode.Subtle ? "subtle" : f == FoveationMode.Balanced ? "balanced" : f == FoveationMode.Aggressive ? "aggressive" : f == FoveationMode.Maximum ? "maximum" : "off";

        /// <summary>The layer's <c>ETERNALVR_CUTSCENES</c> value.</summary>
        public static string CutsceneName(CutsceneView c) => c == CutsceneView.Immersive ? "immersive" : "cinema";

        /// <summary>The layer's <c>ETERNALVR_SHOT_ORIGIN</c> value.</summary>
        public static string ShotOriginName(ShotOrigin o) => o == ShotOrigin.Eye ? "eye" : "hand";

        /// <summary>The layer's <c>ETERNALVR_HUD</c> value.</summary>
        public static string HudName(HudMode h) => h == HudMode.Wrist ? "wrist" : h == HudMode.Weapon ? "weapon" : "panel";

        private static T Pick<T>(string text, T fallback, params (string Name, T Value)[] choices)
        {
            foreach (var c in choices)
                if (string.Equals(text?.Trim(), c.Name, StringComparison.OrdinalIgnoreCase)) return c.Value;
            return fallback;
        }

        /// <summary>The layer's <c>ETERNALVR_POSTURE</c> value.</summary>
        public static string PostureName(PostureMode posture) => posture == PostureMode.Seated ? "seated" : posture == PostureMode.Standing ? "standing" : "auto";

        /// <summary>The layer's <c>ETERNALVR_HEIGHT</c> value.</summary>
        public static string HeightName(HeightMode height) => height == HeightMode.Real ? "real" : "slayer";

        /// <summary>The ini's and the layer's <c>ETERNALVR_ALTERNATE_EYES</c> value: 0, auto or 1.</summary>
        public static string AlternateEyesValue(AlternateEyesMode mode) =>
            mode == AlternateEyesMode.On ? "1" : mode == AlternateEyesMode.Auto ? "auto" : "0";

        /// <summary>1 or true: on; auto: auto; anything else (an older launcher's 0, an unknown value): off.</summary>
        private static AlternateEyesMode ParseAlternateEyes(string text)
        {
            var t = (text ?? string.Empty).Trim();
            if (t == "1" || string.Equals(t, "true", StringComparison.OrdinalIgnoreCase)) return AlternateEyesMode.On;
            return string.Equals(t, "auto", StringComparison.OrdinalIgnoreCase) ? AlternateEyesMode.Auto : AlternateEyesMode.Off;
        }

        private static PostureMode ParsePosture(string text) =>
            string.Equals(text, "seated", StringComparison.OrdinalIgnoreCase) ? PostureMode.Seated
            : string.Equals(text, "standing", StringComparison.OrdinalIgnoreCase) ? PostureMode.Standing
            : PostureMode.Auto;

        /// <summary>Parses <c>WIDTHxHEIGHT</c> within the eye size limits.</summary>
        public static bool TryParseSize(string text, out int width, out int height)
        {
            width = height = 0;
            var parts = (text ?? string.Empty).Trim().Split('x', 'X');
            return parts.Length == 2
                && int.TryParse(parts[0].Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out width)
                && int.TryParse(parts[1].Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out height)
                && width >= MinEyeSize && width <= MaxEyeSize && height >= MinEyeSize && height <= MaxEyeSize;
        }

        private static string OneLine(string s) => (s ?? string.Empty).Replace('\r', ' ').Replace('\n', ' ').Trim();
    }

    public sealed class SettingsException : Exception
    {
        public SettingsException(string message) : base(message) { }
    }
}
