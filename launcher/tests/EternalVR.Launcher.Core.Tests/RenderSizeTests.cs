using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>The render size in stereo (docs/rig-findings/render-size.md): the mirror window and the layer's variables.</summary>
    public class RenderSizeTests
    {
        private static LaunchInputs Inputs(LauncherSettings s = null) => new LaunchInputs
        {
            GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
            LayerDir = @"E:\EternalVR\layer",
            Settings = s ?? new LauncherSettings(),
            ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
        };

        private static Dictionary<string, string> Env(LaunchPlan p) => p.Environment.ToDictionary(e => e.Key, e => e.Value);

        [Fact]
        public void MirrorGoesOnAVirtualDisplayWhenThereIsOne()
        {
            var i = Inputs();
            i.Displays = new[]
            {
                new DisplayArea("tv", 0, 0, 3840, 2160, true),
                new DisplayArea("vdd", 3840, 0, 1920, 1080, false, true),
            };
            var p = LaunchPlanBuilder.Build(i);
            Assert.Equal("3840,0,1280,720", Env(p)["ETERNALVR_MIRROR_WINDOW"]);
            Assert.Equal("3840,0,1280,720", Env(p)["ETERNALVR_WINDOW"]);
            Assert.Contains("window:  1280x720 desktop mirror (each eye at the render size) on vdd", p.Describe());
            // Without a virtual display: the primary one.
            i.Displays = new[] { new DisplayArea("a", -1920, 0, 1920, 1080, false), new DisplayArea("tv", 0, 0, 3840, 2160, true) };
            Assert.Equal("0,0,1280,720", Env(LaunchPlanBuilder.Build(i))["ETERNALVR_MIRROR_WINDOW"]);
            // A display smaller than the mirror: scaled down, 16:9 kept.
            var small = StereoWindow.Mirror(new[] { new DisplayArea("s", 0, 0, 1024, 768, true) });
            Assert.Equal("0,0,1024,576", small.EnvironmentValue);
            Assert.True(small.Reduced);
        }

        [Fact]
        public void RenderScaleAndFixedSizeReachTheLayer()
        {
            var env = Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { RenderScale = 0.8, RenderSize = "2064x2208" })));
            Assert.Equal("2064x2208", env["ETERNALVR_RENDER_SIZE"]);
            Assert.Equal("0.80", env["ETERNALVR_RENDER_SCALE"]);
            Assert.Equal(0.8f, RenderSizeChoice.Decide("auto", 0.8, null).Scale);
            Assert.Equal("2.00", Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { RenderScale = 5 })))["ETERNALVR_RENDER_SCALE"]);
            // Not a size: the default.
            Assert.Equal("auto", Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { RenderSize = "huge" })))["ETERNALVR_RENDER_SIZE"]);
        }

        private static OpenXrProbeResult Probe(uint recW, uint recH, uint maxSwapchain = 16384) => new OpenXrProbeResult
        {
            RuntimeName = "test",
            SystemName = "test",
            Limits = new ViewLimits
            {
                Recommended = new Extent(recW, recH),
                MaxImageRect = new Extent(maxSwapchain, maxSwapchain),
                MaxSwapchain = new Extent(maxSwapchain, maxSwapchain),
            },
        };

        [Fact]
        public void AutoStartsTheGameAtTheRuntimesSize()
        {
            var i = Inputs();
            i.RuntimeProbe = Probe(2496, 2688);
            var p = LaunchPlanBuilder.Build(i);
            var env = Env(p);
            Assert.Equal("2056x2216", env["ETERNALVR_RENDER_SIZE"]);
            Assert.Contains("+r_windowWidth 2056 +r_windowHeight 2216", p.CommandLine);
            // The real window stays the small mirror.
            Assert.Equal("0,0,1280,720", env["ETERNALVR_MIRROR_WINDOW"]);
            Assert.Equal("0,0,1280,720", env["ETERNALVR_WINDOW"]);
            Assert.Contains("render:  render size 2056x2216 from the runtime's recommendation 2496x2688 (scale 1.00)", p.Describe());
            Assert.Null(p.RenderSize.Note);

            // The scale, as the layer reads it; the simulator's limits halve the width for Route S.
            i.Settings = new LauncherSettings { RenderScale = 0.8 };
            Assert.Equal("1648x1776", Env(LaunchPlanBuilder.Build(i))["ETERNALVR_RENDER_SIZE"]);
            i.Settings = new LauncherSettings { RenderScale = 1.2 };
            i.RuntimeProbe = Probe(1280, 1400, 4096);
            p = LaunchPlanBuilder.Build(i);
            Assert.Equal("1536x1680", Env(p)["ETERNALVR_RENDER_SIZE"]);
            Assert.Contains("(scale 1.20)", p.Describe());
        }

        [Fact]
        public void AutoWithoutTheRuntimeIsDecidedInGame()
        {
            var i = Inputs();
            i.RuntimeProbe = OpenXrProbeResult.Failed("xrGetSystem: XR_ERROR_FORM_FACTOR_UNAVAILABLE", true);
            var p = LaunchPlanBuilder.Build(i);
            Assert.Equal("auto", Env(p)["ETERNALVR_RENDER_SIZE"]);
            Assert.Contains("+r_windowWidth 1280 +r_windowHeight 720", p.CommandLine);
            Assert.Equal("Headset not detected; the render size is decided in-game.", p.RenderSize.Note);
            Assert.Contains("render:  render size auto, decided in-game (the runtime did not answer: xrGetSystem: XR_ERROR_FORM_FACTOR_UNAVAILABLE)", p.Describe());
            Assert.Equal("The headset runtime reports no headset: it is off, asleep or not connected.", p.HeadsetProblem);
            // A runtime that does not answer at all: asked about too.
            i.RuntimeProbe = OpenXrProbeResult.Failed("xrCreateInstance: XR_ERROR_RUNTIME_UNAVAILABLE");
            p = LaunchPlanBuilder.Build(i);
            Assert.Equal("The headset runtime did not answer (xrCreateInstance: XR_ERROR_RUNTIME_UNAVAILABLE). Is it installed and running?", p.HeadsetProblem);
            // Not asked at all (no probe): the same size, and nothing to ask about.
            i.RuntimeProbe = null;
            p = LaunchPlanBuilder.Build(i);
            Assert.Equal("auto", Env(p)["ETERNALVR_RENDER_SIZE"]);
            Assert.NotNull(p.RenderSize.Note);
            Assert.Null(p.HeadsetProblem);
        }

        [Fact]
        public void FixedSizeStartsTheGameAtThatSize()
        {
            var i = Inputs(new LauncherSettings { RenderSize = "2064x2208" });
            var p = LaunchPlanBuilder.Build(i);
            Assert.Equal("2064x2208", Env(p)["ETERNALVR_RENDER_SIZE"]);
            Assert.Contains("+r_windowWidth 2064 +r_windowHeight 2208", p.CommandLine);
            Assert.Null(p.RenderSize.Note);
            Assert.Null(p.HeadsetProblem);
            // Fitted to the runtime's limits as the layer would (the simulator: 4096 wide for both eyes).
            i.RuntimeProbe = Probe(1280, 1400, 4096);
            p = LaunchPlanBuilder.Build(i);
            Assert.Equal("2048x2192", Env(p)["ETERNALVR_RENDER_SIZE"]);
            Assert.Contains("+r_windowWidth 2048 +r_windowHeight 2192", p.CommandLine);
            Assert.Contains("render size 2048x2192 from the setting 2064x2208 fitted to the runtime's limits", p.Describe());
        }

        [Fact]
        public void TheProbeGetsTheGamesOpenXrEnvironment()
        {
            Assert.True(LaunchPlanBuilder.WantsRenderSize(new LauncherSettings()));
            Assert.False(LaunchPlanBuilder.WantsRenderSize(new LauncherSettings { RenderSize = "off" }));
            Assert.False(LaunchPlanBuilder.WantsRenderSize(new LauncherSettings { Mode = VrMode.Mono }));
            Assert.Empty(LaunchPlanBuilder.OpenXrEnvironment(new LauncherSettings(), null));
            var env = LaunchPlanBuilder.OpenXrEnvironment(new LauncherSettings { Runtime = @"E:\rt\sim.json" }, null);
            Assert.Equal("XR_RUNTIME_JSON", Assert.Single(env).Key);
            Assert.Equal(@"E:\rt\sim.json", env[0].Value);
        }

        [Fact]
        public void RenderSizeOffKeepsTheEyeSizedWindow()
        {
            var p = LaunchPlanBuilder.Build(Inputs(new LauncherSettings { RenderSize = "off" }));
            var env = Env(p);
            Assert.Contains("+r_windowWidth 2064", p.CommandLine);
            Assert.Contains("+r_windowHeight 2100", p.CommandLine);
            Assert.Equal("0,0,2064,2100", env["ETERNALVR_WINDOW"]);
            Assert.False(env.ContainsKey("ETERNALVR_RENDER_SIZE"));
            Assert.False(env.ContainsKey("ETERNALVR_MIRROR_WINDOW"));
            Assert.False(p.Window.Mirror);
        }

        [Fact]
        public void VirtualDisplaysAreRecognised()
        {
            Assert.True(VirtualDisplays.IsVirtual("Virtual Display Driver", null));
            Assert.True(VirtualDisplays.IsVirtual("Virtual Desktop Monitor", ""));
            Assert.True(VirtualDisplays.IsVirtual("Meta Virtual Monitor", ""));
            Assert.True(VirtualDisplays.IsVirtual("", @"MONITOR\MTT1337\{4d36e96e-e325-11ce-bfc1-08002be10318}\0001"));
            Assert.False(VirtualDisplays.IsVirtual("NVIDIA GeForce RTX 4080", @"MONITOR\GSMC0C8\{4d36e96e-e325-11ce-bfc1-08002be10318}\0003"));
            Assert.False(VirtualDisplays.IsVirtual(null, null));
        }

        [Fact]
        public void RenderSizeKeysRoundTripAndDefault()
        {
            var d = LauncherSettings.Parse("schema_version = 2\nworld_scale = 1.10\n");
            Assert.Equal(LauncherSettings.RenderSizeAuto, d.RenderSize);
            Assert.Equal(1.0, d.RenderScale);
            var r = LauncherSettings.Parse(new LauncherSettings { RenderSize = "2064X2208", RenderScale = 1.25 }.Serialize());
            Assert.Equal("2064x2208", r.RenderSize);
            Assert.Equal(1.25, r.RenderScale, 3);
            Assert.Equal("off", LauncherSettings.Parse("render_size = OFF").RenderSize);
            Assert.Equal("auto", LauncherSettings.Parse("render_size = 100x100").RenderSize); // too small: default
            Assert.Equal(0.5, LauncherSettings.Parse("render_scale = 0.1").RenderScale);
            Assert.Equal(1.0, LauncherSettings.Parse("render_scale = auto").RenderScale);
            Assert.Equal(1.0, LauncherSettings.Parse("render_scale = big").RenderScale);
        }
    }
}
