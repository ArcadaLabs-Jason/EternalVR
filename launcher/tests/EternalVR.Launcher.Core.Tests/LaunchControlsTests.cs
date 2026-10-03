using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>The controls settings (turning, weapon hand, locomotion) and the anti-aliasing choice.</summary>
    public class LaunchControlsTests
    {
        private static LaunchInputs Inputs(LauncherSettings s = null) => new LaunchInputs
        {
            GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
            LayerDir = @"E:\EternalVR\layer",
            LogDir = @"E:\data\logs\20260926-010203",
            Settings = s ?? new LauncherSettings(),
            ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
        };

        private static Dictionary<string, string> Env(LaunchPlan p) => p.Environment.ToDictionary(e => e.Key, e => e.Value);

        [Fact]
        public void ControlsSettingsMapToLayerVariables()
        {
            var env = Env(LaunchPlanBuilder.Build(Inputs()));
            Assert.Equal("smooth", env["ETERNALVR_TURN"]);
            Assert.Equal("45", env["ETERNALVR_SNAP_DEGREES"]);
            Assert.Equal("230", env["ETERNALVR_TURN_RATE"]);
            Assert.Equal("right", env["ETERNALVR_HANDEDNESS"]);
            Assert.Equal("look", env["ETERNALVR_LOCOMOTION"]);
            Assert.Equal("hold", env["ETERNALVR_DOSSIER"]);
            var s = new LauncherSettings { Turn = TurnMode.Snap, SnapDegrees = 120, TurnRate = 90, Hand = Handedness.LeftMirrored, Locomotion = LocomotionMode.LeftHand, Dossier = DossierPress.Tap };
            env = Env(LaunchPlanBuilder.Build(Inputs(s)));
            Assert.Equal("snap", env["ETERNALVR_TURN"]);
            Assert.Equal("90", env["ETERNALVR_SNAP_DEGREES"]); // clamped into the layer's range
            Assert.Equal("150", env["ETERNALVR_TURN_RATE"]);
            Assert.Equal("left_mirror", env["ETERNALVR_HANDEDNESS"]);
            Assert.Equal("left", env["ETERNALVR_LOCOMOTION"]);
            Assert.Equal("tap", env["ETERNALVR_DOSSIER"]);
        }

        [Fact]
        public void TheWeaponWheelIsPointedAtWithTheStickUnlessTheHandIsChosen()
        {
            Assert.Equal(WheelSelect.Stick, new LauncherSettings().Wheel);
            Assert.Equal("stick", Env(LaunchPlanBuilder.Build(Inputs()))["ETERNALVR_WHEEL_SELECT"]);
            var hand = new LauncherSettings { Wheel = WheelSelect.Hand };
            Assert.Equal("hand", Env(LaunchPlanBuilder.Build(Inputs(hand)))["ETERNALVR_WHEEL_SELECT"]);
            Assert.Contains("wheel_select = hand", hand.Serialize());
            Assert.Equal(WheelSelect.Hand, LauncherSettings.Parse(hand.Serialize()).Wheel);
            Assert.Equal(WheelSelect.Stick, LauncherSettings.Parse(new LauncherSettings().Serialize()).Wheel);
            // A file without the key (an older launcher's) or with a value this launcher does not know keeps the stick.
            Assert.Equal(WheelSelect.Stick, LauncherSettings.Parse("schema_version = 2\n").Wheel);
            Assert.Equal(WheelSelect.Stick, LauncherSettings.Parse("schema_version = 2\nwheel_select = feet\n").Wheel);
            Assert.Equal(WheelSelect.Hand, LauncherSettings.Parse("schema_version = 2\nwheel_select = Hand\n").Wheel);
            // A known key: not kept among the unknown ones, so it is not written twice.
            Assert.Empty(LauncherSettings.Parse("schema_version = 2\nwheel_select = hand\n").UnknownKeys);
            // Reset to defaults goes back to the stick.
            Assert.Equal(WheelSelect.Stick, hand.WithDefaults().Wheel);
        }

        [Fact]
        public void TheArmGesturesAreOffUnlessTurnedOn()
        {
            var off = new LauncherSettings();
            Assert.False(off.ThrowGesture);
            Assert.False(off.SwingGesture);
            var env = Env(LaunchPlanBuilder.Build(Inputs()));
            Assert.Equal("0", env["ETERNALVR_THROW"]);
            Assert.Equal("0", env["ETERNALVR_SWING"]);
            var on = new LauncherSettings { ThrowGesture = true, SwingGesture = true };
            env = Env(LaunchPlanBuilder.Build(Inputs(on)));
            Assert.Equal("1", env["ETERNALVR_THROW"]);
            Assert.Equal("1", env["ETERNALVR_SWING"]);
            Assert.Contains("throw_gesture = 1", on.Serialize());
            Assert.Contains("swing_gesture = 1", on.Serialize());
            var back = LauncherSettings.Parse(on.Serialize());
            Assert.True(back.ThrowGesture);
            Assert.True(back.SwingGesture);
            Assert.False(LauncherSettings.Parse(off.Serialize()).ThrowGesture);
            // A file without the keys (an older launcher's) or with a value that is not a clear yes keeps them off.
            Assert.False(LauncherSettings.Parse("schema_version = 2\n").ThrowGesture);
            Assert.False(LauncherSettings.Parse("schema_version = 2\nthrow_gesture = maybe\n").ThrowGesture);
            Assert.True(LauncherSettings.Parse("schema_version = 2\nswing_gesture = On\n").SwingGesture);
            Assert.Empty(LauncherSettings.Parse("schema_version = 2\nthrow_gesture = 1\nswing_gesture = 0\n").UnknownKeys);
            // Reset to defaults turns them off; without controllers they do not apply.
            Assert.False(on.WithDefaults().ThrowGesture);
            Assert.Equal(SettingRules.NeedsControllers, SettingRules.WhyNot(Setting.ThrowGesture, new LauncherSettings { Controllers = false }));
            Assert.Null(SettingRules.WhyNot(Setting.SwingGesture, on));
        }

        [Fact]
        public void BhapticsIsOffUnlessTurnedOn()
        {
            var off = new LauncherSettings();
            Assert.False(off.Bhaptics);
            var env = Env(LaunchPlanBuilder.Build(Inputs()));
            Assert.Equal("0", env["ETERNALVR_BHAPTICS"]);
            Assert.Equal("1.00", env["ETERNALVR_BHAPTICS_INTENSITY"]);
            var on = new LauncherSettings { Bhaptics = true, BhapticsIntensity = 0.5 };
            env = Env(LaunchPlanBuilder.Build(Inputs(on)));
            Assert.Equal("1", env["ETERNALVR_BHAPTICS"]);
            Assert.Equal("0.50", env["ETERNALVR_BHAPTICS_INTENSITY"]);
            Assert.Contains("bhaptics = 1", on.Serialize());
            var back = LauncherSettings.Parse(on.Serialize());
            Assert.True(back.Bhaptics);
            Assert.Equal(0.5, back.BhapticsIntensity, 3);
            // An older launcher's file, or a value that is not a clear yes, keeps it off; the intensity stays within 0 to 1.
            Assert.False(LauncherSettings.Parse("schema_version = 2\n").Bhaptics);
            Assert.False(LauncherSettings.Parse("schema_version = 2\nbhaptics = maybe\n").Bhaptics);
            Assert.Equal(1.0, LauncherSettings.Parse("schema_version = 2\nbhaptics_intensity = 7\n").BhapticsIntensity, 3);
            Assert.Empty(LauncherSettings.Parse("schema_version = 2\nbhaptics = 1\nbhaptics_intensity = 0.4\n").UnknownKeys);
            Assert.False(on.WithDefaults().Bhaptics);
            Assert.Equal(SettingRules.NeedsControllers, SettingRules.WhyNot(Setting.Bhaptics, new LauncherSettings { Controllers = false }));
            Assert.Null(SettingRules.WhyNot(Setting.Bhaptics, on));
        }

        [Fact]
        public void AimDotIsOnByDefaultAndCanBeTurnedOff()
        {
            Assert.Equal("1", Env(LaunchPlanBuilder.Build(Inputs()))["ETERNALVR_UI_RETICLE"]);
            Assert.Equal("0", Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { AimDot = false })))["ETERNALVR_UI_RETICLE"]);
            Assert.False(LauncherSettings.Parse(new LauncherSettings { AimDot = false }.Serialize()).AimDot);
            // A file without the key (an older launcher's) keeps the dot.
            Assert.True(LauncherSettings.Parse("schema_version = 2\n").AimDot);
        }

        [Fact]
        public void DlssOnlyChangesTheForcedAntiAliasingInStereo()
        {
            string Aa(LauncherSettings s)
            {
                var args = LaunchPlanBuilder.Build(Inputs(s)).Arguments;
                int i = args.ToList().IndexOf("+r_antialiasing");
                return i < 0 ? null : args[i + 1];
            }
            // TAA, the default.
            Assert.Equal("1", Aa(new LauncherSettings()));
            Assert.Equal("1", Aa(new LauncherSettings { AntiAliasing = AntiAliasingMode.Taa }));
            Assert.Equal(LaunchPlanBuilder.NoAntiAliasing, Aa(new LauncherSettings { AntiAliasing = AntiAliasingMode.Off }));
            Assert.Equal(LaunchPlanBuilder.DlssAntiAliasing, Aa(new LauncherSettings { AntiAliasing = AntiAliasingMode.Dlss }));
            // Mono forces no anti-aliasing: DLSS changes nothing there.
            Assert.Null(Aa(new LauncherSettings { Mode = VrMode.Mono, AntiAliasing = AntiAliasingMode.Dlss }));
            // The layer's run-time hold keeps DLSS only with its switch.
            Assert.Equal("1", Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { AntiAliasing = AntiAliasingMode.Dlss })))["ETERNALVR_STEREO_DLSS"]);
            Assert.False(Env(LaunchPlanBuilder.Build(Inputs())).ContainsKey("ETERNALVR_STEREO_DLSS"));
            Assert.False(Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { Mode = VrMode.Mono, AntiAliasing = AntiAliasingMode.Dlss }))).ContainsKey("ETERNALVR_STEREO_DLSS"));
        }

        [Fact]
        public void OffTurnsAntiAliasingAndTheTemporalHistoryOffInStereo()
        {
            var off = LaunchPlanBuilder.Build(Inputs(new LauncherSettings { AntiAliasing = AntiAliasingMode.Off }));
            var args = off.Arguments.ToList();
            Assert.Equal(LaunchPlanBuilder.NoAntiAliasing, args[args.IndexOf("+r_antialiasing") + 1]);
            Assert.Equal("0", Env(off)["ETERNALVR_STEREO_TAA"]);
            Assert.False(Env(off).ContainsKey("ETERNALVR_STEREO_DLSS"));
            Assert.False(Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { AntiAliasing = AntiAliasingMode.Taa }))).ContainsKey("ETERNALVR_STEREO_TAA"));
            Assert.False(Env(LaunchPlanBuilder.Build(Inputs())).ContainsKey("ETERNALVR_STEREO_TAA"));
            Assert.False(Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { Mode = VrMode.Mono, AntiAliasing = AntiAliasingMode.Off }))).ContainsKey("ETERNALVR_STEREO_TAA"));
            Assert.Equal(AntiAliasingMode.Off, LauncherSettings.Parse(new LauncherSettings { AntiAliasing = AntiAliasingMode.Off }.Serialize()).AntiAliasing);
        }

        [Fact]
        public void ControlsAndAntiAliasingSettingsRoundTrip()
        {
            var s = new LauncherSettings { Turn = TurnMode.Off, SnapDegrees = 30, TurnRate = 300, Hand = Handedness.Left, Locomotion = LocomotionMode.RightHand, AntiAliasing = AntiAliasingMode.Dlss, Dossier = DossierPress.Tap };
            var back = LauncherSettings.Parse(s.Serialize());
            Assert.Equal(DossierPress.Tap, back.Dossier);
            Assert.Equal(TurnMode.Off, back.Turn);
            Assert.Equal(30, back.SnapDegrees);
            Assert.Equal(300, back.TurnRate);
            Assert.Equal(Handedness.Left, back.Hand);
            Assert.Equal(LocomotionMode.RightHand, back.Locomotion);
            Assert.Equal(AntiAliasingMode.Dlss, back.AntiAliasing);
            // A file without the keys (an older launcher's) takes the defaults; junk values do too.
            var old = LauncherSettings.Parse("schema_version = 2\nturn = sideways\nsnap_degrees = lots\n");
            Assert.Equal(TurnMode.Smooth, old.Turn);
            Assert.Equal(LauncherSettings.DefaultSnapDegrees, old.SnapDegrees);
            Assert.Equal(AntiAliasingMode.Taa, old.AntiAliasing);
            Assert.Equal(DossierPress.Hold, old.Dossier);
            Assert.Equal(DossierPress.Hold, LauncherSettings.Parse("schema_version = 2\ndossier = sometimes\n").Dossier);
        }

        [Fact]
        public void DlssAndItsQualityRoundTripAndReachTheLayer()
        {
            // The anti-aliasing row: TAA, DLSS, Off; the quality is the DLSS group's own row.
            Assert.Equal(3, SettingTexts.For(Setting.AntiAliasing).Choices.Count);
            foreach (AntiAliasingMode mode in System.Enum.GetValues(typeof(AntiAliasingMode)))
                foreach (DlssQuality quality in System.Enum.GetValues(typeof(DlssQuality)))
                {
                    var back = LauncherSettings.Parse(new LauncherSettings { AntiAliasing = mode, Dlss = quality }.Serialize());
                    Assert.Equal(mode, back.AntiAliasing);
                    Assert.Equal(quality, back.Dlss);
                }
            var perf = new LauncherSettings { AntiAliasing = AntiAliasingMode.Dlss, Dlss = DlssQuality.Performance };
            var env = Env(LaunchPlanBuilder.Build(Inputs(perf)));
            Assert.Equal("1", env["ETERNALVR_STEREO_DLSS"]);
            Assert.Equal("performance", env["ETERNALVR_STEREO_DLSS_QUALITY"]);
            // TAA and off keep the chosen quality for later but send none.
            perf.AntiAliasing = AntiAliasingMode.Taa;
            Assert.False(Env(LaunchPlanBuilder.Build(Inputs(perf))).ContainsKey("ETERNALVR_STEREO_DLSS_QUALITY"));
            // A file from before the quality existed: DLSS runs at Quality.
            var old = LauncherSettings.Parse("schema_version = 2\nanti_aliasing = dlss\n");
            Assert.Equal(AntiAliasingMode.Dlss, old.AntiAliasing);
            Assert.Equal("quality", Env(LaunchPlanBuilder.Build(Inputs(old)))["ETERNALVR_STEREO_DLSS_QUALITY"]);
            Assert.Equal(DlssQuality.Quality, LauncherSettings.Parse("schema_version = 2\ndlss_quality = native\n").Dlss);
            // DLAA: its own name in the file and for the layer.
            Assert.Equal(DlssQuality.Dlaa, LauncherSettings.Parse("schema_version = 2\ndlss_quality = dlaa\n").Dlss);
            Assert.Contains("dlss_quality = dlaa", new LauncherSettings { Dlss = DlssQuality.Dlaa }.Serialize());
            var dlaa = new LauncherSettings { AntiAliasing = AntiAliasingMode.Dlss, Dlss = DlssQuality.Dlaa };
            Assert.Equal("dlaa", Env(LaunchPlanBuilder.Build(Inputs(dlaa)))["ETERNALVR_STEREO_DLSS_QUALITY"]);
        }
    }
}
