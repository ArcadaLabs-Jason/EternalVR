using System;
using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>The Play and Advanced tabs' settings: their keys and variables, the grey-out rules, the texts and Reset.</summary>
    public class SettingsUxTests
    {
        private static Dictionary<string, string> Env(LauncherSettings s = null) =>
            LaunchPlanBuilder.Build(new LaunchInputs
            {
                GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
                LayerDir = @"E:\EternalVR\layer",
                Settings = s ?? new LauncherSettings(),
                ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
            }).Environment.ToDictionary(e => e.Key, e => e.Value);

        [Fact]
        public void NewSettingsAreAlwaysPassedExplicitly()
        {
            var env = Env();
            Assert.Equal("1", env["ETERNALVR_BODY_FOLLOW"]);
            Assert.Equal("0.30", env["ETERNALVR_AIM_SMOOTHING"]);
            Assert.Equal("0.60", env["ETERNALVR_HAPTICS"]);
            Assert.Equal("1.50", env["ETERNALVR_UI_DISTANCE"]);
            Assert.Equal("2.00", env["ETERNALVR_UI_WIDTH"]);
            Assert.Equal("0.00", env["ETERNALVR_UI_OFFSET_Y"]);
            Assert.Equal("1.00", env["ETERNALVR_UI_RETICLE_SIZE"]);
            Assert.Equal("left", env["ETERNALVR_MIRROR"]);
            Assert.Equal("cinema", env["ETERNALVR_CUTSCENES"]);
            Assert.Equal("hand", env["ETERNALVR_SHOT_ORIGIN"]);
            Assert.Equal("1", env["ETERNALVR_MENU_BEAM"]);
            Assert.Equal("off", env["ETERNALVR_VIGNETTE"]);
            // Mono too: the layer ignores what does not apply.
            Assert.Equal("left", Env(new LauncherSettings { Mode = VrMode.Mono })["ETERNALVR_MIRROR"]);
        }

        [Fact]
        public void NewSettingsMapToTheLayersValuesWithinItsRanges()
        {
            var env = Env(new LauncherSettings
            {
                BodyFollow = false, AimSmoothing = 3.0, HudDistance = 0.1, HudWidth = 12, HudHeight = -0.35, AimDotSize = 0.6,
                Mirror = MirrorMode.Off, Cutscenes = CutsceneView.Immersive, Shots = ShotOrigin.Eye, MenuBeam = false, Vibration = -2.0,
                Vignette = VignetteMode.Strong,
            });
            Assert.Equal("0", env["ETERNALVR_BODY_FOLLOW"]);
            Assert.Equal("1.00", env["ETERNALVR_AIM_SMOOTHING"]);
            Assert.Equal("0.00", env["ETERNALVR_HAPTICS"]);
            Assert.Equal("0.30", env["ETERNALVR_UI_DISTANCE"]);
            Assert.Equal("10.00", env["ETERNALVR_UI_WIDTH"]);
            Assert.Equal("-0.35", env["ETERNALVR_UI_OFFSET_Y"]);
            Assert.Equal("0.60", env["ETERNALVR_UI_RETICLE_SIZE"]);
            Assert.Equal("off", env["ETERNALVR_MIRROR"]);
            Assert.Equal("immersive", env["ETERNALVR_CUTSCENES"]);
            Assert.Equal("eye", env["ETERNALVR_SHOT_ORIGIN"]);
            Assert.Equal("0", env["ETERNALVR_MENU_BEAM"]);
            Assert.Equal("strong", env["ETERNALVR_VIGNETTE"]);
            Assert.Equal("light", Env(new LauncherSettings { Vignette = VignetteMode.Light })["ETERNALVR_VIGNETTE"]);
            Assert.Equal("right", Env(new LauncherSettings { Mirror = MirrorMode.Right })["ETERNALVR_MIRROR"]);
            Assert.Equal("1.00", Env(new LauncherSettings { Vibration = 5.0 })["ETERNALVR_HAPTICS"]);
            Assert.Equal("0.35", Env(new LauncherSettings { Vibration = 0.35 })["ETERNALVR_HAPTICS"]);
        }

        [Fact]
        public void NewKeysRoundTripAndAreOptional()
        {
            var s = new LauncherSettings
            {
                BodyFollow = false, AimSmoothing = 0.15, HudDistance = 2.25, HudWidth = 1.5, HudHeight = -0.2, AimDotSize = 1.5,
                Mirror = MirrorMode.Right, Cutscenes = CutsceneView.Immersive, Shots = ShotOrigin.Eye, MenuBeam = false, Vibration = 0.35,
                Vignette = VignetteMode.Light,
            };
            var back = LauncherSettings.Parse(s.Serialize());
            Assert.False(back.BodyFollow);
            Assert.Equal(0.15, back.AimSmoothing, 3);
            Assert.Equal(0.35, back.Vibration, 3);
            Assert.Equal(2.25, back.HudDistance, 3);
            Assert.Equal(1.5, back.HudWidth, 3);
            Assert.Equal(-0.2, back.HudHeight, 3);
            Assert.Equal(1.5, back.AimDotSize, 3);
            Assert.Equal(MirrorMode.Right, back.Mirror);
            Assert.Equal(CutsceneView.Immersive, back.Cutscenes);
            Assert.Equal(ShotOrigin.Eye, back.Shots);
            Assert.False(back.MenuBeam);
            Assert.Equal(VignetteMode.Light, back.Vignette);
            Assert.Equal(VignetteMode.Strong, LauncherSettings.Parse("schema_version = 2\nvignette = STRONG\n").Vignette);
            // A file without them (an older launcher's), or with junk, takes the defaults.
            var old = LauncherSettings.Parse("schema_version = 2\naim_smoothing = lots\nvibration = lots\nmirror = sideways\nhud_width = -4\n");
            Assert.True(old.BodyFollow);
            Assert.Equal(LauncherSettings.DefaultAimSmoothing, old.AimSmoothing);
            Assert.Equal(LauncherSettings.DefaultVibration, old.Vibration);
            Assert.Equal(LauncherSettings.DefaultVibration, LauncherSettings.Parse("schema_version = 2\n").Vibration);
            Assert.Equal(1.0, LauncherSettings.Parse("schema_version = 2\nvibration = 3\n").Vibration);
            Assert.Equal(MirrorMode.Left, old.Mirror);
            Assert.Equal(LauncherSettings.MinHudWidth, old.HudWidth);
            Assert.Equal(CutsceneView.Cinema, old.Cutscenes);
            Assert.True(old.MenuBeam);
            Assert.Equal(VignetteMode.Off, old.Vignette);
            Assert.Equal(VignetteMode.Off, LauncherSettings.Parse("schema_version = 2\nvignette = maximum\n").Vignette);
            // Still schema 2: the new keys are optional, and an older launcher keeps them (it does too).
            Assert.Equal(2, LauncherSettings.SchemaVersion);
        }

        [Fact]
        public void UnknownKeysAreKeptInFileOrder()
        {
            var s = LauncherSettings.Parse("schema_version = 2\nfuture_b = 2\nturn = snap\nfuture_a = some text\n");
            Assert.Equal(new[] { "future_b", "future_a" }, s.UnknownKeys.Select(k => k.Key));
            var text = s.Serialize();
            var back = LauncherSettings.Parse(text);
            Assert.Equal(TurnMode.Snap, back.Turn);
            Assert.Equal("2", back.UnknownKeys.Single(k => k.Key == "future_b").Value);
            Assert.Equal("some text", back.UnknownKeys.Single(k => k.Key == "future_a").Value);
            Assert.True(text.IndexOf("future_b", StringComparison.Ordinal) < text.IndexOf("future_a", StringComparison.Ordinal));
            // Known keys are never counted as unknown.
            Assert.Empty(LauncherSettings.Parse(new LauncherSettings().Serialize()).UnknownKeys);
        }

        [Fact]
        public void UnknownKeysSurviveASave()
        {
            using (var t = new TempDir())
            {
                var path = t.Write("launcher.ini", "schema_version = 2\nnewer_setting = 7\n");
                var s = LauncherSettings.Load(path);
                s.Turn = TurnMode.Off;
                s.Save(path);
                var back = LauncherSettings.Load(path);
                Assert.Equal(TurnMode.Off, back.Turn);
                Assert.Equal("7", back.UnknownKeys.Single(k => k.Key == "newer_setting").Value);
            }
        }

        [Fact]
        public void ResetKeepsTheFoldersTheRuntimeAndUnknownKeys()
        {
            var s = LauncherSettings.Parse("schema_version = 2\ngame_dir = D:\\Games\\DOOMEternal\nlayer_dir = D:\\EVR\\layer\n"
                + "runtime = C:\\rt\\openxr.json\nturn = off\nvignette = strong\nbody_follow = 0\nmirror = off\nextra_args = +com_showFPS 1\nnewer = x\n");
            var reset = s.WithDefaults();
            Assert.Equal(@"D:\Games\DOOMEternal", reset.GameDir);
            Assert.Equal(@"D:\EVR\layer", reset.LayerDir);
            Assert.Equal(@"C:\rt\openxr.json", reset.Runtime);
            Assert.Equal("x", reset.UnknownKeys.Single().Value);
            var defaults = new LauncherSettings();
            Assert.Equal(defaults.Turn, reset.Turn);
            Assert.Equal(VignetteMode.Off, reset.Vignette);
            Assert.Equal(defaults.BodyFollow, reset.BodyFollow);
            Assert.Equal(defaults.Mirror, reset.Mirror);
            Assert.Equal(string.Empty, reset.ExtraArguments);
            // The original is untouched.
            Assert.Equal(TurnMode.Off, s.Turn);
        }

        [Fact]
        public void EverythingAppliesWithTheDefaultsExceptTheSnapAngleAndDlss()
        {
            var inapplicable = SettingRules.Inapplicable(new LauncherSettings());
            Assert.Equal(new[] { Setting.SnapAngle, Setting.DlssQuality, Setting.DlssVersion, Setting.DlssPreset, Setting.DlssInHeadset },
                inapplicable.Keys);
            Assert.Equal(SettingRules.NeedsSnapTurn, inapplicable[Setting.SnapAngle]);
            // TAA is the default anti-aliasing: the DLSS rows wait for DLSS.
            Assert.Equal(SettingRules.NeedsDlss, inapplicable[Setting.DlssVersion]);
        }

        [Fact]
        public void TurnSpeedAndSnapAngleFollowTheTurnMode()
        {
            Assert.Null(SettingRules.WhyNot(Setting.SnapAngle, new LauncherSettings { Turn = TurnMode.Snap }));
            Assert.Equal(SettingRules.NeedsSmoothTurn, SettingRules.WhyNot(Setting.TurnSpeed, new LauncherSettings { Turn = TurnMode.Snap }));
            Assert.NotNull(SettingRules.WhyNot(Setting.TurnSpeed, new LauncherSettings { Turn = TurnMode.Off }));
            Assert.NotNull(SettingRules.WhyNot(Setting.SnapAngle, new LauncherSettings { Turn = TurnMode.Off }));
        }

        [Fact]
        public void WithoutControllersTheirSettingsDoNotApply()
        {
            var s = new LauncherSettings { Controllers = false };
            foreach (var setting in new[]
            {
                Setting.Turning, Setting.TurnSpeed, Setting.Vignette, Setting.GloryKills, Setting.WalkInRoom, Setting.RecenterHold, Setting.WeaponHand, Setting.MoveToward,
                Setting.XButton, Setting.DossierMapSticks, Setting.WeaponWheel, Setting.ThrowGesture, Setting.SwingGesture, Setting.AimSteadiness, Setting.AimDot, Setting.ButtonLayout, Setting.ShotsFrom, Setting.AimDotSize,
                Setting.MenuLaser, Setting.HudPlace, Setting.Vibration, Setting.Bhaptics, Setting.RevenantAimWith,
            })
                Assert.Equal(SettingRules.NeedsControllers, SettingRules.WhyNot(setting, s));
            Assert.Null(SettingRules.WhyNot(Setting.AimWith, s));
            Assert.Null(SettingRules.WhyNot(Setting.MotionControllers, s));
        }

        [Fact]
        public void HandAimSettingsNeedHandAimAndTheDotNeedsStereo()
        {
            var head = new LauncherSettings { Aim = AimMode.Head };
            foreach (var setting in new[] { Setting.AimSteadiness, Setting.AimDot, Setting.ShotsFrom, Setting.AimDotSize })
                Assert.Equal(SettingRules.NeedsHandAim, SettingRules.WhyNot(setting, head));
            var mono = new LauncherSettings { Mode = VrMode.Mono };
            Assert.Equal(SettingRules.NeedsStereo, SettingRules.WhyNot(Setting.AimDot, mono));
            Assert.Null(SettingRules.WhyNot(Setting.AimSteadiness, mono));
            Assert.Null(SettingRules.WhyNot(Setting.ShotsFrom, mono));
            Assert.NotNull(SettingRules.WhyNot(Setting.AimDotSize, new LauncherSettings { AimDot = false }));
        }

        [Fact]
        public void WalkingDoesNotApplySitting()
        {
            Assert.Equal(SettingRules.NotSitting, SettingRules.WhyNot(Setting.WalkInRoom, new LauncherSettings { Posture = PostureMode.Seated }));
            Assert.Null(SettingRules.WhyNot(Setting.WalkInRoom, new LauncherSettings { Posture = PostureMode.Standing }));
        }

        [Fact]
        public void StereoOnlySettingsAreOffInMono()
        {
            var mono = new LauncherSettings { Mode = VrMode.Mono };
            foreach (var setting in new[]
            {
                Setting.Resolution, Setting.EachEye, Setting.AntiAliasing, Setting.TextureStreaming, Setting.CpuSaver, Setting.DesktopWindow, Setting.HudDistance, Setting.HudSize,
                Setting.HudHeight, Setting.MenuLaser, Setting.HudPlace, Setting.Foveation, Setting.FramePacing,
            })
                Assert.Equal(SettingRules.NeedsStereo, SettingRules.WhyNot(setting, mono));
            foreach (var setting in new[] { Setting.VrMode, Setting.WorldSize, Setting.EyeDistance, Setting.CutsceneView, Setting.Runtime })
                Assert.Null(SettingRules.WhyNot(setting, mono));
        }

        [Fact]
        public void EverySettingHasALabelATooltipAndOneChoicePerValue()
        {
            foreach (Setting setting in Enum.GetValues(typeof(Setting)))
            {
                var text = SettingTexts.For(setting);
                Assert.NotNull(text);
                Assert.False(string.IsNullOrWhiteSpace(text.Label), setting.ToString());
                Assert.False(string.IsNullOrWhiteSpace(text.Tooltip), setting.ToString());
            }
            // The window maps a list's index to the enum value: one choice per value, in enum order.
            void Choices<T>(Setting setting) => Assert.Equal(Enum.GetValues(typeof(T)).Length, SettingTexts.For(setting).Choices.Count);
            Choices<TurnMode>(Setting.Turning);
            Choices<VignetteMode>(Setting.Vignette);
            Choices<GloryKillView>(Setting.GloryKills);
            Choices<PostureMode>(Setting.PlayPosition);
            Choices<HeightMode>(Setting.EyeHeight);
            Choices<AimMode>(Setting.AimWith);
            Choices<RevenantAimMode>(Setting.RevenantAimWith);
            Choices<Handedness>(Setting.WeaponHand);
            Choices<LocomotionMode>(Setting.MoveToward);
            Choices<DossierPress>(Setting.XButton);
            Choices<MapPanStick>(Setting.DossierMapSticks);
            Choices<WheelSelect>(Setting.WeaponWheel);
            Choices<AntiAliasingMode>(Setting.AntiAliasing);
            Choices<SharpeningMode>(Setting.Sharpening);
            Choices<DlssQuality>(Setting.DlssQuality);
            Choices<FoveationMode>(Setting.Foveation);
            Choices<FramePacing>(Setting.FramePacing);
            Choices<VrMode>(Setting.VrMode);
            Choices<MirrorMode>(Setting.DesktopWindow);
            Choices<CutsceneView>(Setting.CutsceneView);
            Choices<ShotOrigin>(Setting.ShotsFrom);
            Choices<HudMode>(Setting.HudPlace);
            Assert.Equal(AimSteadiness.Values.Length, SettingTexts.For(Setting.AimSteadiness).Choices.Count);
            Assert.Equal(Vibration.Values.Length, SettingTexts.For(Setting.Vibration).Choices.Count);
            Assert.Contains("cannot be skipped", SettingTexts.For(Setting.SkipCutscenes).Tooltip);
        }

        [Fact]
        public void AimSteadinessStepsIncludeTheDefault()
        {
            Assert.Equal(new[] { 0.0, 0.15, 0.3, 0.5 }, AimSteadiness.Values);
            Assert.Equal(2, AimSteadiness.IndexOf(LauncherSettings.DefaultAimSmoothing));
            Assert.Equal(0, AimSteadiness.IndexOf(0.0));
            Assert.Equal(-1, AimSteadiness.IndexOf(0.42));
        }

        [Fact]
        public void VibrationStepsIncludeTheDefault()
        {
            Assert.Equal(new[] { 0.0, 0.35, 0.6, 1.0 }, Vibration.Values);
            Assert.Equal(new[] { "Off", "Light", "Medium", "Strong" }, Vibration.Names);
            Assert.Equal(2, Vibration.IndexOf(LauncherSettings.DefaultVibration));
            Assert.Equal(0, Vibration.IndexOf(0.0));
            Assert.Equal(-1, Vibration.IndexOf(0.8));
            // Head aim or mono: the controllers still vibrate.
            Assert.Null(SettingRules.WhyNot(Setting.Vibration, new LauncherSettings { Aim = AimMode.Head, Mode = VrMode.Mono }));
        }
    }
}
