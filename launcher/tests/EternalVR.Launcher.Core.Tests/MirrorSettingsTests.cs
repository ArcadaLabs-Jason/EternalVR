using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>The desktop mirror's display, size and crop, and the cutscene screen's shape.</summary>
    public class MirrorSettingsTests
    {
        private static LaunchInputs Inputs(LauncherSettings s = null) => new LaunchInputs
        {
            GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
            LayerDir = @"E:\EternalVR\layer",
            LogDir = @"E:\data\logs\20260926-010203",
            Settings = s ?? new LauncherSettings(),
            ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
            Displays = new[] { new DisplayArea("tv", 0, 0, 3840, 2160, true) },
        };

        private static Dictionary<string, string> Env(LaunchPlan p) => p.Environment.ToDictionary(e => e.Key, e => e.Value);

        [Fact]
        public void DefaultsKeepTheLaunchersPlacementAndAWholeImage()
        {
            var env = Env(LaunchPlanBuilder.Build(Inputs()));
            Assert.Equal("launcher", env["ETERNALVR_MIRROR_DISPLAY"]);
            Assert.Equal("1280x720", env["ETERNALVR_MIRROR_SIZE"]);
            Assert.Equal("full", env["ETERNALVR_MIRROR_CROP"]);
            Assert.Equal("16:9", env["ETERNALVR_CINEMA_ASPECT"]);
            Assert.Equal("0,0,1280,720", env["ETERNALVR_MIRROR_WINDOW"]);
        }

        [Fact]
        public void ChoicesReachTheLayer()
        {
            var s = new LauncherSettings { MirrorDisplay = "-1920,0", MirrorSize = "1920x1080", MirrorCrop = true, Cinema = CinemaShape.Full };
            var env = Env(LaunchPlanBuilder.Build(Inputs(s)));
            Assert.Equal("-1920,0", env["ETERNALVR_MIRROR_DISPLAY"]);
            Assert.Equal("1920x1080", env["ETERNALVR_MIRROR_SIZE"]);
            Assert.Equal("16:9", env["ETERNALVR_MIRROR_CROP"]);
            Assert.Equal("full", env["ETERNALVR_CINEMA_ASPECT"]);
            // The launcher's own rectangle takes the size too.
            Assert.Equal("0,0,1920,1080", env["ETERNALVR_MIRROR_WINDOW"]);
            Assert.Equal("primary", Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { MirrorDisplay = "primary" })))["ETERNALVR_MIRROR_DISPLAY"]);
            Assert.Equal("16:10", Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { Cinema = CinemaShape.Wide16x10 })))["ETERNALVR_CINEMA_ASPECT"]);
        }

        [Fact]
        public void FillReachesTheLayerAndTheLauncherWindowKeepsTheDefaultSize()
        {
            var env = Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { MirrorSize = "fill", MirrorCrop = true })));
            Assert.Equal("fill", env["ETERNALVR_MIRROR_SIZE"]);
            Assert.Equal("16:9", env["ETERNALVR_MIRROR_CROP"]);
            Assert.Equal("0,0,1280,720", env["ETERNALVR_MIRROR_WINDOW"]);
            Assert.Equal("fill", LauncherSettings.Parse(new LauncherSettings { MirrorSize = "fill" }.Serialize()).MirrorSize);
            Assert.Equal("fill", LauncherSettings.Parse("schema_version = 2\nmirror_size = Fill\n").MirrorSize);
            Assert.Equal("fill", MirrorSettings.NormaliseSize(" FILL "));
            Assert.Contains(MirrorSettings.SizeFill, MirrorSettings.Sizes);
        }

        [Fact]
        public void WithoutTheMirrorNoMirrorVariables()
        {
            // Render size off: the window is the eye image.
            var off = Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { RenderSize = LauncherSettings.RenderSizeOff })));
            Assert.False(off.ContainsKey("ETERNALVR_MIRROR_DISPLAY"));
            Assert.False(off.ContainsKey("ETERNALVR_MIRROR_SIZE"));
            Assert.False(off.ContainsKey("ETERNALVR_MIRROR_CROP"));
            Assert.Equal("16:9", off["ETERNALVR_CINEMA_ASPECT"]);
            // Mono: no stereo window and no cutscene screen.
            var mono = Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { Mode = VrMode.Mono })));
            Assert.False(mono.ContainsKey("ETERNALVR_MIRROR_DISPLAY"));
            Assert.False(mono.ContainsKey("ETERNALVR_CINEMA_ASPECT"));
        }

        [Fact]
        public void SettingsRoundTripAndJunkTakesTheDefaults()
        {
            var s = new LauncherSettings { MirrorDisplay = "2560,-300", MirrorSize = "800x450", MirrorCrop = true, Cinema = CinemaShape.Wide16x10 };
            var back = LauncherSettings.Parse(s.Serialize());
            Assert.Equal("2560,-300", back.MirrorDisplay);
            Assert.Equal("800x450", back.MirrorSize);
            Assert.True(back.MirrorCrop);
            Assert.Equal(CinemaShape.Wide16x10, back.Cinema);
            // An older launcher's file: the defaults.
            var old = LauncherSettings.Parse("schema_version = 2\n");
            Assert.Equal("auto", old.MirrorDisplay);
            Assert.Equal("1280x720", old.MirrorSize);
            Assert.False(old.MirrorCrop);
            Assert.Equal(CinemaShape.Wide16x9, old.Cinema);
            var junk = LauncherSettings.Parse("schema_version = 2\nmirror_display = 3\nmirror_size = 10x10\nmirror_crop = yes\ncinema_aspect = 4:3\n");
            Assert.Equal("auto", junk.MirrorDisplay);
            Assert.Equal("1280x720", junk.MirrorSize);
            Assert.False(junk.MirrorCrop);
            Assert.Equal(CinemaShape.Wide16x9, junk.Cinema);
            Assert.Equal("primary", LauncherSettings.Parse("schema_version = 2\nmirror_display = Primary\n").MirrorDisplay);
            Assert.Equal("1024x576", LauncherSettings.Parse("schema_version = 2\nmirror_size = 1024X576\n").MirrorSize);
        }

        [Fact]
        public void MirrorRowsApplyOnlyToTheMirrorAndTheShapeOnlyToTheFlatScreen()
        {
            foreach (var setting in new[] { Setting.DesktopMonitor, Setting.DesktopSize, Setting.DesktopCrop })
            {
                Assert.Null(SettingRules.WhyNot(setting, new LauncherSettings()));
                Assert.Equal(SettingRules.NeedsRenderSize, SettingRules.WhyNot(setting, new LauncherSettings { RenderSize = LauncherSettings.RenderSizeOff }));
                Assert.Equal(SettingRules.NeedsStereo, SettingRules.WhyNot(setting, new LauncherSettings { Mode = VrMode.Mono }));
            }
            Assert.Null(SettingRules.WhyNot(Setting.CutsceneShape, new LauncherSettings()));
            Assert.Equal(SettingRules.NeedsCinema, SettingRules.WhyNot(Setting.CutsceneShape, new LauncherSettings { Cutscenes = CutsceneView.Immersive }));
            Assert.Equal(SettingRules.NeedsStereo, SettingRules.WhyNot(Setting.CutsceneShape, new LauncherSettings { Mode = VrMode.Mono }));
        }

        [Fact]
        public void ChoicesMatchTheValues()
        {
            Assert.Equal(System.Enum.GetValues(typeof(CinemaShape)).Length, SettingTexts.For(Setting.CutsceneShape).Choices.Count);
            Assert.Equal(MirrorSettings.Sizes.Length, SettingTexts.For(Setting.DesktopSize).Choices.Count);
            Assert.Contains(MirrorSettings.DefaultSize, MirrorSettings.Sizes);
            // Automatic and the primary monitor; the window adds the displays.
            Assert.Equal(2, SettingTexts.For(Setting.DesktopMonitor).Choices.Count);
        }
    }
}
