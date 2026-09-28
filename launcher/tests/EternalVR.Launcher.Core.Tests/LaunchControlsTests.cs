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
            Assert.Equal("head", env["ETERNALVR_LOCOMOTION"]);
            Assert.Equal("hold", env["ETERNALVR_DOSSIER"]);
            var s = new LauncherSettings { Turn = TurnMode.Snap, SnapDegrees = 120, TurnRate = 90, Hand = Handedness.LeftMirrored, Locomotion = LocomotionMode.Hand, Dossier = DossierPress.Tap };
            env = Env(LaunchPlanBuilder.Build(Inputs(s)));
            Assert.Equal("snap", env["ETERNALVR_TURN"]);
            Assert.Equal("90", env["ETERNALVR_SNAP_DEGREES"]); // clamped into the layer's range
            Assert.Equal("150", env["ETERNALVR_TURN_RATE"]);
            Assert.Equal("left_mirror", env["ETERNALVR_HANDEDNESS"]);
            Assert.Equal("hand", env["ETERNALVR_LOCOMOTION"]);
            Assert.Equal("tap", env["ETERNALVR_DOSSIER"]);
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
            Assert.Equal("1", Aa(new LauncherSettings { AntiAliasing = AntiAliasingMode.Taa }));
            // Off, the default.
            Assert.Equal(LaunchPlanBuilder.NoAntiAliasing, Aa(new LauncherSettings()));
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
            Assert.Equal("0", Env(LaunchPlanBuilder.Build(Inputs()))["ETERNALVR_STEREO_TAA"]);
            Assert.False(Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { Mode = VrMode.Mono, AntiAliasing = AntiAliasingMode.Off }))).ContainsKey("ETERNALVR_STEREO_TAA"));
            Assert.Equal(AntiAliasingMode.Off, LauncherSettings.Parse(new LauncherSettings { AntiAliasing = AntiAliasingMode.Off }.Serialize()).AntiAliasing);
        }

        [Fact]
        public void ControlsAndAntiAliasingSettingsRoundTrip()
        {
            var s = new LauncherSettings { Turn = TurnMode.Off, SnapDegrees = 30, TurnRate = 300, Hand = Handedness.Left, Locomotion = LocomotionMode.Hand, AntiAliasing = AntiAliasingMode.Dlss, Dossier = DossierPress.Tap };
            var back = LauncherSettings.Parse(s.Serialize());
            Assert.Equal(DossierPress.Tap, back.Dossier);
            Assert.Equal(TurnMode.Off, back.Turn);
            Assert.Equal(30, back.SnapDegrees);
            Assert.Equal(300, back.TurnRate);
            Assert.Equal(Handedness.Left, back.Hand);
            Assert.Equal(LocomotionMode.Hand, back.Locomotion);
            Assert.Equal(AntiAliasingMode.Dlss, back.AntiAliasing);
            // A file without the keys (an older launcher's) takes the defaults; junk values do too.
            var old = LauncherSettings.Parse("schema_version = 2\nturn = sideways\nsnap_degrees = lots\n");
            Assert.Equal(TurnMode.Smooth, old.Turn);
            Assert.Equal(LauncherSettings.DefaultSnapDegrees, old.SnapDegrees);
            Assert.Equal(AntiAliasingMode.Off, old.AntiAliasing);
            Assert.Equal(DossierPress.Hold, old.Dossier);
            Assert.Equal(DossierPress.Hold, LauncherSettings.Parse("schema_version = 2\ndossier = sometimes\n").Dossier);
        }
    }
}
