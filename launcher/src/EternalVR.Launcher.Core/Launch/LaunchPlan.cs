using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using EternalVR.Launcher.Core.Game;
using EternalVR.Launcher.Core.Preflight;
using EternalVR.Launcher.Core.Settings;

namespace EternalVR.Launcher.Core.Launch
{
    public enum LayerRoute
    {
        /// <summary><c>VK_ADD_IMPLICIT_LAYER_PATH</c> in the game's environment only (default, proven on the rig).</summary>
        Environment,
        /// <summary>HKCU implicit-layer registration for the session (flag-gated, off by default).</summary>
        HkcuRegistration,
    }

    public sealed class LaunchInputs
    {
        public string GameRoot { get; set; }
        public string LayerDir { get; set; }
        public string LogDir { get; set; }
        public LauncherSettings Settings { get; set; } = new LauncherSettings();
        public ForcedCvars ForcedCvars { get; set; } = new ForcedCvars(new ForcedCvar[0]);
        /// <summary>The build of the game: some cvars are forced only for Steam's.</summary>
        public GamePlatform Platform { get; set; } = GamePlatform.Steam;
        /// <summary>The CPU Saver's items; the cvars of those that are on are handed to the layer in stereo.</summary>
        public CpuSaver CpuSaver { get; set; } = CpuSaver.Empty;
        /// <summary>False when no settings location was found: nothing is forced (T-094).</summary>
        public bool ForceCvars { get; set; } = true;
        public IReadOnlyList<LayerDecision> LayerDecisions { get; set; } = new LayerDecision[0];
        public LayerRoute Route { get; set; } = LayerRoute.Environment;
        /// <summary>The desktop's displays, for the stereo window (empty: placed at 0,0 at the full eye size).</summary>
        public IReadOnlyList<DisplayArea> Displays { get; set; } = new DisplayArea[0];
        /// <summary>The runtime's answer for the render size (<see cref="OpenXrProbe"/>); null when it was not asked.</summary>
        public OpenXrProbeResult RuntimeProbe { get; set; }
        /// <summary>The headset's native panel per eye (<c>data\headsets.txt</c>), for Resolution's native panel; null when not known.</summary>
        public Extent? Panel { get; set; }
        /// <summary>The player's controls folder; its maps are used when it holds any (null: the built-in controls).</summary>
        public ControlsFolder Controls { get; set; }
        /// <summary>NVIDIA's newest DLSS when the launcher has downloaded it (checked); null when it has not.</summary>
        public string NewestDlss { get; set; }
        /// <summary>The player's own <c>r_SSDO</c> from their config (<see cref="GameLayout.PlayerCvar"/>); null when it is not set there.</summary>
        public string PlayerSsdo { get; set; }
        /// <summary>The player's own <c>r_SSR</c> from their config (<see cref="GameLayout.PlayerCvar"/>); null when it is not set there.</summary>
        public string PlayerSsr { get; set; }
    }

    /// <summary>Exactly what will be started: the game's exe, folder, command line and added environment.</summary>
    public sealed class LaunchPlan
    {
        public string ExePath { get; set; }
        public string WorkingDirectory { get; set; }
        public IReadOnlyList<string> Arguments { get; set; }
        /// <summary>Variables added to (or replacing in) the launcher's own environment for the game process only.</summary>
        public IReadOnlyList<KeyValuePair<string, string>> Environment { get; set; }
        public LayerRoute Route { get; set; }
        /// <summary>The stereo game window; null in mono.</summary>
        public StereoWindowPlan Window { get; set; }
        /// <summary>The stereo render size; null in mono and with the render size off.</summary>
        public RenderSizeChoice RenderSize { get; set; }
        /// <summary>
        /// Why the headset looks unready from the runtime probe ("no headset", the runtime not answering), for the
        /// window to ask before the launch; null when the probe found it, or none was made.
        /// </summary>
        public string HeadsetProblem { get; set; }
        /// <summary>
        /// Variables of the launcher's own environment the game inherits unchanged that the loaders or the layer read
        /// (<see cref="ChildEnvironment.Inherited"/>); logged with the plan.
        /// </summary>
        public IReadOnlyList<KeyValuePair<string, string>> Inherited { get; set; } = new KeyValuePair<string, string>[0];
        /// <summary>
        /// Keys of the settings restore that this session may leave as the game saves them (recorded with the snapshot,
        /// <see cref="Safety.SettingsSnapshot.Take"/>): <c>r_SSDO</c> and <c>r_SSR</c> while the layer can hold them at the
        /// player's own Directional Occlusion and Reflections settings. The restore keeps one only when the layer's status file
        /// confirms it did (<see cref="LayerStatusFile.FollowedKeys"/>).
        /// </summary>
        public IReadOnlyList<string> KeptKeys { get; set; } = new string[0];

        public string CommandLine => string.Join(" ", Arguments.Select(QuoteIfNeeded));

        public string Describe()
        {
            var sb = new StringBuilder();
            sb.AppendLine("exe:     " + ExePath);
            sb.AppendLine("cwd:     " + WorkingDirectory);
            sb.AppendLine("args:    " + CommandLine);
            sb.AppendLine("route:   " + (Route == LayerRoute.Environment ? "environment (VK_ADD_IMPLICIT_LAYER_PATH)" : "HKCU registration"));
            foreach (var kv in Environment) sb.AppendLine("env:     " + kv.Key + "=" + kv.Value);
            foreach (var kv in Inherited ?? new KeyValuePair<string, string>[0]) sb.AppendLine("inherit: " + kv.Key + "=" + kv.Value);
            if (Window != null)
                sb.AppendLine("window:  " + Window.Width + "x" + Window.Height + (Window.Mirror ? " desktop mirror (each eye at the render size)" : " per eye")
                    + (Window.Display == null ? " (no display known)" : " on " + Window.Display)
                    + (Window.Reduced ? " (reduced to fit the display)" : string.Empty));
            if (RenderSize != null) sb.AppendLine("render:  " + RenderSize.Describe());
            return sb.ToString();
        }

        /// <summary>
        /// Quotes one argument for the Windows command line (the CommandLineToArgvW rules): backslashes
        /// are doubled before an embedded quote and before the closing quote, so a path ending in a
        /// backslash does not swallow the quote.
        /// </summary>
        public static string QuoteIfNeeded(string arg)
        {
            if (arg.Length > 0 && arg.IndexOfAny(new[] { ' ', '\t', '"' }) < 0) return arg;
            var sb = new StringBuilder("\"");
            int backslashes = 0;
            foreach (char c in arg)
            {
                if (c == '\\') { backslashes++; continue; }
                if (c == '"') sb.Append('\\', backslashes * 2 + 1).Append('"');
                else sb.Append('\\', backslashes).Append(c);
                backslashes = 0;
            }
            return sb.Append('\\', backslashes * 2).Append('"').ToString();
        }
    }

    public static class LaunchPlanBuilder
    {
        /// <summary>
        /// The desktop mirror at the chosen size, placed by the launcher (the layer then moves it to a chosen display).
        /// Fill starts at the default size; the layer then covers the display with it.
        /// </summary>
        private static StereoWindowPlan MirrorPlan(IReadOnlyList<DisplayArea> displays, LauncherSettings s) =>
            MirrorSettings.TryParseSize(s.MirrorSize, out int w, out int h)
                ? StereoWindow.Mirror(displays, w, h)
                : StereoWindow.Mirror(displays);

        public static LaunchPlan Build(LaunchInputs inputs)
        {
            if (string.IsNullOrEmpty(inputs.GameRoot)) throw new ArgumentException("game root is not set");
            if (string.IsNullOrEmpty(inputs.LayerDir)) throw new ArgumentException("layer folder is not set");
            var s = inputs.Settings;
            bool stereo = s.Mode == VrMode.Stereo;
            // With the render size (the default) the eyes do not depend on the window: it is only the desktop mirror.
            var renderSize = LauncherSettings.NormaliseRenderSize(s.RenderSize) ?? LauncherSettings.RenderSizeAuto;
            bool mirror = stereo && renderSize != LauncherSettings.RenderSizeOff;
            var window = !stereo ? null
                : mirror ? MirrorPlan(inputs.Displays, s)
                : StereoWindow.Fit(inputs.Displays, s.EyeWidth, s.EyeHeight);
            // The game starts at its final render size when it is known before the launch: the layer applies a fixed
            // size before the first swapchain and the game's video init takes its output size from r_windowWidth and
            // r_windowHeight, so nothing is resized mid-session (a resize can fail on cards with 12 GB or less).
            var renderChoice = mirror ? RenderSizeChoice.Decide(renderSize, s.RenderScale, inputs.RuntimeProbe, s.ResolutionBase, inputs.Panel) : null;
            int outputWidth = renderChoice?.Size != null ? (int)renderChoice.Size.Value.Width : window?.Width ?? 0;
            int outputHeight = renderChoice?.Size != null ? (int)renderChoice.Size.Value.Height : window?.Height ?? 0;

            var args = new List<string>();
            if (inputs.ForceCvars)
                foreach (var cvar in inputs.ForcedCvars.For(stereo, inputs.Platform))
                {
                    args.Add("+" + cvar.Name);
                    args.Add(cvar.Value == ForcedCvars.EyeWidth ? outputWidth.ToString(CultureInfo.InvariantCulture)
                        : cvar.Value == ForcedCvars.EyeHeight ? outputHeight.ToString(CultureInfo.InvariantCulture)
                        : IsAntiAliasing(cvar.Name) && stereo && s.AntiAliasing == AntiAliasingMode.Dlss ? DlssAntiAliasing
                        : IsAntiAliasing(cvar.Name) && stereo && s.AntiAliasing == AntiAliasingMode.Off ? NoAntiAliasing
                        : cvar.Value);
                }
            args.AddRange(SplitArguments(s.ExtraArguments));

            var env = new List<KeyValuePair<string, string>>();
            void Set(string k, string v)
            {
                env.RemoveAll(e => string.Equals(e.Key, k, StringComparison.OrdinalIgnoreCase));
                env.Add(new KeyValuePair<string, string>(k, v));
            }

            Set("SteamAppId", GameLayout.SteamAppId);
            if (inputs.Route == LayerRoute.Environment) Set("VK_ADD_IMPLICIT_LAYER_PATH", inputs.LayerDir);
            Set("ETERNALVR_ENABLE_LAYER", "1");
            if (!string.IsNullOrEmpty(inputs.LogDir)) Set("ETERNALVR_LOG_DIR", inputs.LogDir);
            Set("ETERNALVR_WORLD_SCALE", LauncherSettings.ClampWorldScale(s.WorldScale).ToString("0.00", CultureInfo.InvariantCulture));
            if (stereo) Set("ETERNALVR_MODE", "stereo");
            // One eye per game tick for slower processors, always or only while the processor cannot keep up
            // (docs/rig-findings/alternate-eye.md); explicit either way. Not with Parallel Eye Rendering, which ignores it.
            Set("ETERNALVR_ALTERNATE_EYES", stereo && !s.ParallelEyesOn ? LauncherSettings.AlternateEyesValue(s.AlternateEyes) : "0");
            // Parallel Eye Rendering: both eyes as two views of one render, their work at the same time (docs/VR_STEREO.md
            // "Parallel Eye Rendering"). The layer turns it on only for the game version it knows, and holds what it needs
            // (async compute off among them) itself. Stereo only and not with DLSS (the layer would keep the standard
            // renderer); absent when off.
            if (s.ParallelEyesOn) Set("ETERNALVR_PARALLEL_EYES", "1");
            // The HUD, menus and subtitles on their own quad and out of both eyes (docs/rig-findings/ui-layer.md), in
            // mono too: a menu over the game (pause, the Dossier, a tutorial popup) gets its panel and pointer from it.
            Set("ETERNALVR_UI_LAYER", "1");
            // The HUD on the off hand's wrist or the weapon needs the UI layer and the controllers (docs/VR_HANDS_HUD.md).
            Set("ETERNALVR_HUD", LauncherSettings.HudName(s.Controllers ? s.Hud : HudMode.Panel));
            Set("ETERNALVR_CONTROLLERS", s.Controllers ? "1" : "0");
            // Hand aim needs the controllers; without them the head aims.
            Set("ETERNALVR_AIM", LauncherSettings.AimName(s.Aim == AimMode.Hand && !s.Controllers ? AimMode.Head : s.Aim));
            // The Revenant's aim while piloting it, only when it is not the same as Aim with (the layer then follows ETERNALVR_AIM);
            // hand aim needs the controllers the same way. Under view aim the layer ignores it.
            if (s.RevenantAim != RevenantAimMode.Same)
                Set("ETERNALVR_DEMON_AIM", s.RevenantAim == RevenantAimMode.Hand && s.Controllers ? "hand" : "head");
            // What aims melee, and the equipment launcher and the Flame Belch, only when not the same as Aim with; the layer
            // uses them only under hand aim (features/input/action_aim.hpp).
            if (s.MeleeAim != ActionAimMode.Same) Set("ETERNALVR_MELEE_AIM", LauncherSettings.ActionAimEnvironment(s.MeleeAim));
            if (s.EquipmentAim != ActionAimMode.Same) Set("ETERNALVR_EQUIPMENT_AIM", LauncherSettings.ActionAimEnvironment(s.EquipmentAim));
            if (window != null) Set("ETERNALVR_WINDOW", window.EnvironmentValue);
            if (mirror)
            {
                // docs/rig-findings/render-size.md: each eye at the headset's recommended size times the scale (worked
                // out here from the runtime's answer, else by the layer: auto); the window stays the mirror. Without
                // the render size the game renders at the mirror's size.
                Set("ETERNALVR_RENDER_SIZE", renderChoice.EnvironmentValue);
                Set("ETERNALVR_RENDER_SCALE", RenderSizeChoice.ScaleText(s.RenderScale));
                Set("ETERNALVR_MIRROR_WINDOW", window.EnvironmentValue);
                // The layer moves, sizes and crops the mirror from the launcher's rectangle (docs/rig-findings/render-size.md).
                Set("ETERNALVR_MIRROR_DISPLAY", MirrorSettings.DisplayEnvironment(s.MirrorDisplay));
                Set("ETERNALVR_MIRROR_SIZE", MirrorSettings.NormaliseSize(s.MirrorSize) ?? MirrorSettings.DefaultSize);
                Set("ETERNALVR_MIRROR_CROP", s.MirrorCrop ? "16:9" : "full");
            }
            if (stereo) Set("ETERNALVR_CINEMA_ASPECT", MirrorSettings.CinemaName(s.Cinema));
            Set("ETERNALVR_SKIP_CINEMATICS", s.SkipCinematics ? "1" : "0");
            // Room-scale, posture and eye height (docs/VR_ROOMSCALE.md).
            Set("ETERNALVR_POSTURE", LauncherSettings.PostureName(s.Posture));
            Set("ETERNALVR_HEIGHT", LauncherSettings.HeightName(s.Height));
            Set("ETERNALVR_RECENTER_HOLD", s.RecenterLongPress ? "2.0" : "0");
            // Controls (docs/VR_CONTROLLERS.md): turning, the weapon hand, what forward means.
            Set("ETERNALVR_TURN", LauncherSettings.TurnName(s.Turn));
            Set("ETERNALVR_SNAP_DEGREES", LauncherSettings.ClampSnapDegrees(s.SnapDegrees).ToString("0", CultureInfo.InvariantCulture));
            Set("ETERNALVR_TURN_RATE", LauncherSettings.ClampTurnRate(s.TurnRate).ToString("0", CultureInfo.InvariantCulture));
            Set("ETERNALVR_VIGNETTE", LauncherSettings.VignetteName(s.Vignette));
            Set("ETERNALVR_GLORY_KILLS", LauncherSettings.GloryKillName(s.GloryKills));
            Set("ETERNALVR_HANDEDNESS", LauncherSettings.HandednessName(s.Hand));
            Set("ETERNALVR_ARMS", s.ShowArms ? "shown" : "hidden");
            Set("ETERNALVR_LOCOMOTION", LauncherSettings.LocomotionName(s.Locomotion));
            Set("ETERNALVR_DOSSIER", LauncherSettings.DossierName(s.Dossier));
            Set("ETERNALVR_MAP_STICKS", LauncherSettings.MapSticksName(s.MapSticks));
            Set("ETERNALVR_WHEEL_SELECT", LauncherSettings.WheelSelectName(s.Wheel));
            // Arm gestures (docs/VR_INTERACTIONS.md), off by default: the throw and the overhead swing.
            Set("ETERNALVR_THROW", s.ThrowGesture ? "1" : "0");
            Set("ETERNALVR_SWING", s.SwingGesture ? "1" : "0");
            Set("ETERNALVR_HANDS_JUMP", s.HandsJump ? "1" : "0");
            Set("ETERNALVR_PUNCH_SPEED", Number(s.PunchSpeed, LauncherSettings.MinPunchSpeed, LauncherSettings.MaxPunchSpeed, LauncherSettings.DefaultPunchSpeed));
            Set("ETERNALVR_HOLD_SECONDS", Number(s.HoldTime, LauncherSettings.MinHoldTime, LauncherSettings.MaxHoldTime, LauncherSettings.DefaultHoldTime));
            // The player's own maps, each in place of the built-in one for its controllers (docs/release/CONTROLS.md).
            if (s.Controllers && inputs.Controls != null && inputs.Controls.HasPlayerMaps) Set("ETERNALVR_CONTROLLER_DATA", inputs.Controls.Dir);
            Set("ETERNALVR_UI_RETICLE", s.AimDot ? "1" : "0");
            // The settings of the Play and Advanced tabs, always explicit (the layer's own defaults may change).
            Set("ETERNALVR_BODY_FOLLOW", s.BodyFollow ? "1" : "0");
            Set("ETERNALVR_HEAD_FADE", s.HeadFade ? "1" : "0");
            Set("ETERNALVR_AIM_SMOOTHING", Number(s.AimSmoothing, 0.0, 1.0, LauncherSettings.DefaultAimSmoothing));
            Set("ETERNALVR_HAPTICS", Number(s.Vibration, 0.0, 1.0, LauncherSettings.DefaultVibration));
            // bHaptics (docs/BHAPTICS.md), off by default: the layer talks to the bHaptics Player on this PC.
            Set("ETERNALVR_BHAPTICS", s.Bhaptics ? "1" : "0");
            Set("ETERNALVR_BHAPTICS_INTENSITY", Number(s.BhapticsIntensity, 0.0, 1.0, LauncherSettings.DefaultBhapticsIntensity));
            Set("ETERNALVR_UI_DISTANCE", Number(s.HudDistance, LauncherSettings.MinHudDistance, LauncherSettings.MaxHudDistance, LauncherSettings.DefaultHudDistance));
            Set("ETERNALVR_UI_WIDTH", Number(s.HudWidth, LauncherSettings.MinHudWidth, LauncherSettings.MaxHudWidth, LauncherSettings.DefaultHudWidth));
            Set("ETERNALVR_UI_OFFSET_Y", Number(s.HudHeight, LauncherSettings.MinHudHeight, LauncherSettings.MaxHudHeight, 0.0));
            Set("ETERNALVR_UI_RETICLE_SIZE", Number(s.AimDotSize, LauncherSettings.MinAimDotSize, LauncherSettings.MaxAimDotSize, LauncherSettings.DefaultAimDotSize));
            Set("ETERNALVR_MIRROR", LauncherSettings.MirrorName(s.Mirror));
            Set("ETERNALVR_CUTSCENES", LauncherSettings.CutsceneName(s.Cutscenes));
            Set("ETERNALVR_CUTSCENE_ARMS", s.CutsceneArms ? "shown" : "hidden");
            Set("ETERNALVR_SHOT_ORIGIN", LauncherSettings.ShotOriginName(s.Shots));
            Set("ETERNALVR_MENU_BEAM", s.MenuBeam ? "1" : "0");
            // The per-eye temporal set holds r_antialiasing at run time; it keeps DLSS (2) only when asked to (taa_hooks.cpp).
            if (stereo && s.AntiAliasing == AntiAliasingMode.Dlss)
            {
                Set("ETERNALVR_STEREO_DLSS", "1");
                Set("ETERNALVR_STEREO_DLSS_QUALITY", LauncherSettings.DlssQualityName(s.Dlss));
                // A newer DLSS DLL (NVIDIA's newest, downloaded, or the player's), used by the layer from where it is
                // (docs/rig-findings/dlss-dll.md). Without one the game's own runs.
                if (DlssDll.PathFor(s, inputs.NewestDlss) is string dll)
                {
                    Set("ETERNALVR_DLSS_DLL", dll);
                    Set("ETERNALVR_DLSS_PRESET", DlssDll.NormalisePreset(s.DlssPreset));
                }
            }
            // The game's post-process sharpening held at the chosen strength; absent, the player's own setting stays.
            if (stereo && LauncherSettings.SharpeningValue(s.Sharpening) is string sharpening) Set("ETERNALVR_SHARPENING", sharpening);
            // SSDO: the game turns it off itself after r_TAASafeMode 1, which stereo holds at start-up; the layer holds the
            // player's own r_SSDO instead (on, the game's default, unless their config turns it off), then what the game's
            // Directional Occlusion setting writes whenever it runs (the profile's load, the video menu).
            if (stereo) Set("ETERNALVR_STEREO_SSDO", inputs.PlayerSsdo == "0" ? "0" : "1");
            // Screen-space reflections, the same knock-on: the layer holds r_SSR while per-eye TAA runs, from the player's own
            // (on, the game's default, unless their config turns it off: Reflections at Low) and then at what the game's
            // Reflections setting writes whenever it runs (the profile's load, the video menu). A config without r_SSR cannot
            // tell Low from Medium (both write r_SSRQuality 0), so the layer's reading of the setting decides. Off: held off.
            if (stereo) Set("ETERNALVR_STEREO_SSR", s.Reflections == ReflectionsMode.Off ? "off" : inputs.PlayerSsr == "0" ? "0" : "1");
            // Off: no per-eye temporal history; the layer holds r_antialiasing 0 and r_TAASafeMode 1 (docs/VR_STEREO.md).
            if (stereo && s.AntiAliasing == AntiAliasingMode.Off) Set("ETERNALVR_STEREO_TAA", "0");
            // Fixed foveated rendering (experimental): the edges of each eye shaded at a lower rate through NVIDIA's shading
            // rate image (src/vkcore/vrs_nv.cpp; other cards log it as unsupported). Stereo only, not with Parallel Eye
            // Rendering (the layer turns it off there), absent when off.
            if (stereo && !s.ParallelEyesOn && s.Foveation != FoveationMode.Off)
                Set("ETERNALVR_FOVEATION", LauncherSettings.FoveationName(s.Foveation));
            // Frame pacing (docs/VR_STEREO.md): one pair of eye images per headset frame, timed to the headset.
            // Stereo only and not with adaptive alternate eyes (which Parallel Eye Rendering ignores); explicit either way
            // (the layer's default may change).
            var paced = stereo && (s.AlternateEyes != AlternateEyesMode.Auto || s.ParallelEyesOn) ? s.Pacing : FramePacing.Off;
            Set("ETERNALVR_PACE", LauncherSettings.PacingName(paced));
            // The CPU Saver (docs/rig-findings/perf-cpu-cvars.md): the layer holds the cvars of the items that are on
            // at run time, in stereo only (it holds none in mono). Not without a settings location, since the restore could
            // not undo them there. Absent when no item is on.
            var saver = stereo && inputs.ForceCvars && inputs.CpuSaver != null ? inputs.CpuSaver.EnvironmentValue(s) : string.Empty;
            if (saver.Length > 0) Set(CpuSaver.EnvironmentName, saver);
            var ipd = LauncherSettings.ClampIpd(s.IpdMm);
            if (ipd > 0.0) Set("ETERNALVR_IPD", ipd.ToString("0.0", CultureInfo.InvariantCulture));
            foreach (var kv in OpenXrEnvironment(s, inputs.LayerDecisions)) Set(kv.Key, kv.Value);
            foreach (var d in inputs.LayerDecisions)
                if (d.Action == LayerAction.Disable && d.Environment.HasValue) Set(d.Environment.Value.Key, d.Environment.Value.Value);

            return new LaunchPlan
            {
                ExePath = Path.Combine(inputs.GameRoot, GameLayout.RetailExe),
                WorkingDirectory = inputs.GameRoot,
                Arguments = args,
                Environment = env,
                Route = inputs.Route,
                Window = window,
                RenderSize = renderChoice,
                HeadsetProblem = HeadsetProblemOf(inputs.RuntimeProbe),
                KeptKeys = KeptKeysOf(s),
            };
        }

        /// <summary>
        /// Whether the <c>r_SSR</c> the game saves after the session can be the player's Reflections setting, so the restore may
        /// keep a change they made in the game's menu: in stereo while the layer can follow that setting (per-eye TAA or DLSS,
        /// the launcher's Screen-space reflections at the game's setting), confirmed after the session by the layer's
        /// <c>ssr_follow=1</c>. Otherwise the game's own r_SSR 0 after r_TAASafeMode 1, or the launcher's Off, is put back
        /// (session-keys.txt), and so is a mono session's, as before.
        /// </summary>
        public static bool SsrIsThePlayers(LauncherSettings s) =>
            s.Mode == VrMode.Stereo && s.AntiAliasing != AntiAliasingMode.Off && s.Reflections == ReflectionsMode.Game;

        /// <summary>
        /// Whether the <c>r_SSDO</c> the game saves after the session can be the player's Directional Occlusion setting: in
        /// stereo with TAA or DLSS (the layer holds it at that setting, and r_TAASafeMode is 0), confirmed by the layer's
        /// <c>ssdo_follow=1</c>. With anti-aliasing Off the game writes r_SSDO 0 on every render against the hold, so the
        /// restore puts it back.
        /// </summary>
        public static bool SsdoIsThePlayers(LauncherSettings s) => s.Mode == VrMode.Stereo && s.AntiAliasing != AntiAliasingMode.Off;

        /// <summary>The plan's <see cref="LaunchPlan.KeptKeys"/>.</summary>
        public static IReadOnlyList<string> KeptKeysOf(LauncherSettings s)
        {
            var kept = new List<string>();
            if (SsdoIsThePlayers(s)) kept.Add("r_SSDO");
            if (SsrIsThePlayers(s)) kept.Add("r_SSR");
            return kept;
        }

        /// <summary>What the window says before a launch whose runtime probe found no headset; null when it did, or none was made.</summary>
        public static string HeadsetProblemOf(OpenXrProbeResult probe)
        {
            if (probe == null || probe.Ok) return null;
            return probe.HeadsetUnavailable
                ? "The headset runtime reports no headset: it is off, asleep or not connected."
                : "The headset runtime did not answer (" + probe.Error + "). Is it installed and running?";
        }

        /// <summary>The <c>r_antialiasing</c> value of DLSS; the layer then runs the game's DLSS once per eye (taa_ngx.cpp).</summary>
        public const string DlssAntiAliasing = "2";

        /// <summary>The <c>r_antialiasing</c> value of no anti-aliasing.</summary>
        public const string NoAntiAliasing = "0";

        private static string Number(double v, double min, double max, double fallback) =>
            LauncherSettings.Clamp(v, min, max, fallback).ToString("0.00", CultureInfo.InvariantCulture);

        private static bool IsAntiAliasing(string cvar) => string.Equals(cvar, "r_antialiasing", StringComparison.OrdinalIgnoreCase);

        /// <summary>A stereo launch with the render size on: the one that asks the runtime for its size first.</summary>
        public static bool WantsRenderSize(LauncherSettings s) =>
            s.Mode == VrMode.Stereo
            && (LauncherSettings.NormaliseRenderSize(s.RenderSize) ?? LauncherSettings.RenderSizeAuto) != LauncherSettings.RenderSizeOff;

        /// <summary>
        /// The game's OpenXR environment: the chosen runtime (none: the system default, <see cref="EffectiveRuntime"/>) and the
        /// OpenXR API layers switched off. The runtime probe runs with it, so it asks the same runtime, through the same layers.
        /// </summary>
        public static IReadOnlyList<KeyValuePair<string, string>> OpenXrEnvironment(LauncherSettings s, IEnumerable<LayerDecision> decisions)
        {
            var env = new List<KeyValuePair<string, string>>();
            if (!IsSystemRuntime(s.Runtime)) env.Add(new KeyValuePair<string, string>(RuntimeVariable, s.Runtime));
            foreach (var d in decisions ?? new LayerDecision[0])
                if (d.Action == LayerAction.Disable && d.Environment.HasValue && d.Layer.Api == LayerApi.OpenXR) env.Add(d.Environment.Value);
            return env;
        }

        public static bool IsSystemRuntime(string runtime) =>
            string.IsNullOrWhiteSpace(runtime) || string.Equals(runtime, LauncherSettings.SystemRuntime, StringComparison.OrdinalIgnoreCase);

        /// <summary>The OpenXR loader's variable naming the runtime manifest; it wins over the system's active runtime.</summary>
        public const string RuntimeVariable = "XR_RUNTIME_JSON";

        /// <summary>
        /// The runtime manifest a launch and the probe use: the chosen one, else <see cref="RuntimeVariable"/> when the
        /// launcher's environment has it (the game inherits it, and the OpenXR loader takes it before the registry, as in
        /// every other OpenXR program started from it), else the system's active runtime.
        /// </summary>
        public static string EffectiveRuntime(string chosen, string inherited, string systemActive) =>
            !IsSystemRuntime(chosen) ? chosen : !string.IsNullOrWhiteSpace(inherited) ? inherited : systemActive;

        /// <summary>True when the system default is in use and <see cref="RuntimeVariable"/> in the environment decides it.</summary>
        public static bool RuntimeFromEnvironment(string chosen, string inherited) =>
            IsSystemRuntime(chosen) && !string.IsNullOrWhiteSpace(inherited);

        /// <summary>Virtual Desktop's own runtime (VDXR), judged by its manifest's file name.</summary>
        public static bool IsVdxr(string runtimeManifest) =>
            !string.IsNullOrEmpty(runtimeManifest)
            && Path.GetFileName(runtimeManifest.Replace('\\', '/').Split('/').Last()).StartsWith("virtualdesktop-openxr", StringComparison.OrdinalIgnoreCase);

        /// <summary>SteamVR's runtime (<c>steamxr_win64.json</c>), judged by its manifest's file name.</summary>
        public static bool IsSteamVr(string runtimeManifest) =>
            !string.IsNullOrEmpty(runtimeManifest)
            && runtimeManifest.Replace('\\', '/').Split('/').Last().StartsWith("steamxr", StringComparison.OrdinalIgnoreCase);

        /// <summary>Splits command-line text at whitespace, keeping double-quoted parts together.</summary>
        public static IReadOnlyList<string> SplitArguments(string text)
        {
            var list = new List<string>();
            if (string.IsNullOrWhiteSpace(text)) return list;
            var sb = new StringBuilder();
            bool quoted = false, any = false;
            foreach (char ch in text)
            {
                if (ch == '"') { quoted = !quoted; any = true; continue; }
                if (!quoted && char.IsWhiteSpace(ch))
                {
                    if (any) list.Add(sb.ToString());
                    sb.Clear();
                    any = false;
                    continue;
                }
                sb.Append(ch);
                any = true;
            }
            if (any) list.Add(sb.ToString());
            return list;
        }
    }

    public enum StartOutcome
    {
        Running,
        ExitedEarly,
        HandOff,
        Exited,
        /// <summary>The started process exited early; still looking for a game process Steam may start.</summary>
        WaitingForHandOff,
    }

    /// <summary>
    /// Hand-off detection (T-109): the started process exits within the watch window while another game
    /// process appears, which means Steam restarted the game without our environment: a named error,
    /// never a silent flat launch. Steam starts the new process only after the first one has exited, so
    /// an early exit is watched for <see cref="HandOffGraceSeconds"/> more before it counts as a plain
    /// early exit.
    /// </summary>
    public static class StartWatch
    {
        public const int WatchSeconds = 10;
        public const int HandOffGraceSeconds = 8;

        public static StartOutcome Decide(bool startedProcessExited, double secondsSinceStart, double secondsSinceExit, bool otherGameProcessRunning)
        {
            if (!startedProcessExited) return StartOutcome.Running;
            if (secondsSinceStart - secondsSinceExit > WatchSeconds) return StartOutcome.Exited;
            if (otherGameProcessRunning) return StartOutcome.HandOff;
            return secondsSinceExit < HandOffGraceSeconds ? StartOutcome.WaitingForHandOff : StartOutcome.ExitedEarly;
        }

        /// <summary>The status line after the game closed within the first seconds. Without the layer's
        /// <c>LAYER_LOADED</c> marker the game exited before the Vulkan loader loaded EternalVR: only the command line and the
        /// environment can have played a part (a Game Pass player's game crashed this way 22 times in a row after a
        /// flat session from the Xbox app, 2026-10-03, also with the layer turned off).</summary>
        public static string EarlyExitMessage(double seconds, int exitCode, bool layerLoaded)
        {
            if (!layerLoaded && GameExit.IsCrash(exitCode))
                // One line, the thing that matters first; the exit code is in the log line before it.
                return "The game crashed before EternalVR loaded. Start it once from the Xbox app or Steam, or restart the PC, then try again. "
                    + "Still crashing? " + Report.ReportHint.Ask;
            return $"The game closed {seconds:0} s after it started (exit code {exitCode}). "
                + "If it keeps happening: " + Report.ReportHint.Ask;
        }

        /// <summary>The status line when the game the DOOM Eternal Launcher started had ended before the launcher could open
        /// it (no exit code to read): the session's <c>LAYER_LOADED</c> marker says whether EternalVR had loaded.</summary>
        public static string GoneMessage(bool layerLoaded)
        {
            if (!layerLoaded)
                return "The game closed right after it started, before EternalVR loaded. Start it once from the Xbox app, "
                    + "or restart the PC, then try again. Still closing? " + Report.ReportHint.Ask;
            return "The game closed right after it started. If it keeps happening: " + Report.ReportHint.Ask;
        }

        public const string HandOffMessage =
            "The game was restarted by Steam without EternalVR (a hand-off). VR is not active in this game window. " +
            "Quit the game, make sure Steam is running and logged in, and launch again from the EternalVR launcher.";
    }
}
