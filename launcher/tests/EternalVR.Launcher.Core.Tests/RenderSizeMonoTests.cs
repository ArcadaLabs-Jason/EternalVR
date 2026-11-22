using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>
    /// Mono's render size (docs/VR_HEAD_TRACKED.md "Render size"): the one image both eyes see renders at one eye's size by the
    /// same rule, with its own swapchain width. The same cases as the layer's
    /// <c>tests/features/render_size/render_size_tests.cpp</c> ("auto in mono").
    /// </summary>
    public class RenderSizeMonoTests
    {
        private static ViewLimits Limits(uint recW, uint recH, uint maxImage, uint maxSwapchain, uint eyes) => new ViewLimits
        {
            Recommended = new Extent(recW, recH),
            MaxImageRect = new Extent(maxImage, maxImage),
            MaxSwapchain = new Extent(maxSwapchain, maxSwapchain),
            EyesSideBySide = eyes,
        };

        private static OpenXrProbeResult Probe(uint recW, uint recH, uint maxImage, uint maxSwapchain) =>
            new OpenXrProbeResult { RuntimeName = "test", SystemName = "test", Limits = Limits(recW, recH, maxImage, maxSwapchain, 1) };

        private static LaunchInputs Inputs(LauncherSettings s, OpenXrProbeResult probe) => new LaunchInputs
        {
            GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
            LayerDir = @"E:\EternalVR\layer",
            Settings = s,
            ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
            RuntimeProbe = probe,
        };

        private static Dictionary<string, string> Env(LaunchPlan p) => p.Environment.ToDictionary(e => e.Key, e => e.Value);

        [Fact]
        public void MonoKeepsTheStereoCvarsOutAndRendersAtTheRenderSizeInTheMirror()
        {
            var p = LaunchPlanBuilder.Build(Inputs(new LauncherSettings { Mode = VrMode.Mono, Aim = AimMode.Head, HudWrist = true }, null));
            var env = Env(p);
            Assert.False(env.ContainsKey("ETERNALVR_MODE"));
            // The UI layer stays on: menus over the game need it for their panel and pointer.
            Assert.Equal("1", env["ETERNALVR_UI_LAYER"]);
            Assert.Equal("wrist", env["ETERNALVR_HUD"]);
            Assert.Equal("head", env["ETERNALVR_AIM"]);
            // The stereo cvars stay out: the game's own anti-aliasing, dynamic resolution and vsync.
            foreach (var name in new[] { "r_TAASafeMode", "r_antialiasing", "r_TAAAntiGhosting", "rs_enable", "r_swapInterval" })
                Assert.DoesNotContain("+" + name, p.CommandLine);
            Assert.Contains("+r_hdrDisplay 0", p.CommandLine);
            // The one image at the render size (decided in-game without the runtime's answer), windowed in the desktop mirror.
            Assert.Equal("auto", env["ETERNALVR_RENDER_SIZE"]);
            Assert.Equal("1.00", env["ETERNALVR_RENDER_SCALE"]);
            Assert.Equal("0,0,1280,720", env["ETERNALVR_WINDOW"]);
            Assert.Equal("0,0,1280,720", env["ETERNALVR_MIRROR_WINDOW"]);
            foreach (var arg in new[] { "+r_fullscreen 0", "+r_windowWidth 1280", "+r_windowHeight 720" })
                Assert.Contains(arg, p.CommandLine);
            Assert.True(p.Window.Mirror);
            Assert.NotNull(p.RenderSize);
            Assert.False(env.ContainsKey("ETERNALVR_EDGE_MARGIN"));
        }

        [Fact]
        public void MonoWithTheRenderSizeOffKeepsTheFlatGamesWindow()
        {
            var p = LaunchPlanBuilder.Build(Inputs(new LauncherSettings { Mode = VrMode.Mono, RenderSize = "off" }, null));
            var env = Env(p);
            Assert.False(env.ContainsKey("ETERNALVR_WINDOW"));
            Assert.False(env.ContainsKey("ETERNALVR_RENDER_SIZE"));
            Assert.False(env.ContainsKey("ETERNALVR_MIRROR_WINDOW"));
            Assert.Null(p.Window);
            Assert.Null(p.RenderSize);
            foreach (var name in new[] { "r_fullscreen", "r_windowWidth", "r_windowHeight", "r_swapInterval" })
                Assert.DoesNotContain("+" + name, p.CommandLine);
            Assert.False(LaunchPlanBuilder.WantsRenderSize(new LauncherSettings { Mode = VrMode.Mono, RenderSize = "off" }));
        }

        [Fact]
        public void MonoStartsAtOneEyesSizeFromTheRuntimesAnswer()
        {
            // Virtual Desktop's Godlike on a Quest 3: one eye's Auto size, no edge margin, the swapchain's width not shared.
            var i = Inputs(new LauncherSettings { Mode = VrMode.Mono, EdgeMargin = EdgeMarginMode.Large }, null);
            i.RuntimeProbe = new OpenXrProbeResult
            {
                Limits = new ViewLimits { Recommended = new Extent(3072, 3264), MaxImageRect = new Extent(4096, 4096), MaxSwapchain = new Extent(16384, 16384) },
            };
            var p = LaunchPlanBuilder.Build(i);
            var env = Env(p);
            Assert.Equal("2072x2200", env["ETERNALVR_RENDER_SIZE"]);
            foreach (var arg in new[] { "+r_fullscreen 0", "+r_windowWidth 2072", "+r_windowHeight 2200" })
                Assert.Contains(arg, p.CommandLine);
            Assert.Equal(1.0, p.RenderSize.Growth);
            Assert.False(env.ContainsKey("ETERNALVR_EDGE_MARGIN"));
            Assert.True(LaunchPlanBuilder.WantsRenderSize(new LauncherSettings { Mode = VrMode.Mono }));
            // Stereo at the same settings: each eye the same size, grown by the edge margin.
            i.Settings = new LauncherSettings { EdgeMargin = EdgeMarginMode.Large };
            Assert.NotEqual("2072x2200", Env(LaunchPlanBuilder.Build(i))["ETERNALVR_RENDER_SIZE"]);
            i.Settings = new LauncherSettings();
            Assert.Equal("2072x2200", Env(LaunchPlanBuilder.Build(i))["ETERNALVR_RENDER_SIZE"]);
        }

        [Theory]
        // VDXR's 100% on a Quest 3, Virtual Desktop's Godlike: within the limits mono's size is one stereo eye's.
        [InlineData(2496u, 2688u, 1.0f, 2056u, 2216u)]
        [InlineData(3072u, 3264u, 1.0f, 2072u, 2200u)]
        [InlineData(3072u, 3264u, 1.5f, 3104u, 3304u)]
        public void MonoAtAutoIsOneStereoEyesSize(uint recW, uint recH, float scale, uint w, uint h)
        {
            var mono = RenderSize.AutoSize(Limits(recW, recH, 4096, 16384, RenderSizeChoice.MonoEyesSideBySide), scale);
            var stereo = RenderSize.AutoSize(Limits(recW, recH, 4096, 16384, RenderSizeChoice.StereoEyesSideBySide), scale);
            Assert.Equal(new Extent(w, h), mono);
            Assert.Equal(stereo, mono);
            foreach (var s in new[] { 0.5f, 1.0f, 1.5f, 2.0f })
                Assert.Equal(RenderSize.AutoSize(Limits(recW, recH, 4096, 16384, 2), s), RenderSize.AutoSize(Limits(recW, recH, 4096, 16384, 1), s));
        }

        [Fact]
        public void TheSimulatorsSwapchainWidthIsSharedByTwoEyesOnly()
        {
            // The OpenXR Simulator: 4096 x 4096 swapchains, 1280 x 1400 recommended.
            Assert.Equal(new Extent(1280, 1400), RenderSize.AutoSize(Limits(1280, 1400, 4096, 4096, 2), 1.0f));
            Assert.Equal(new Extent(2048, 2240), RenderSize.AutoSize(Limits(1280, 1400, 4096, 4096, 2), 2.0f));
            Assert.Equal(new Extent(1280, 1400), RenderSize.AutoSize(Limits(1280, 1400, 4096, 4096, 1), 1.0f));
            Assert.Equal(new Extent(2560, 2800), RenderSize.AutoSize(Limits(1280, 1400, 4096, 4096, 1), 2.0f));
            Assert.Equal(new Extent(2496, 2688), RenderSize.FitToLimits(new Extent(2496, 2688), Limits(1280, 1400, 4096, 4096, 1)));
            // The launch plans the same: mono at x2.00 starts at 2560 x 2800, stereo's eyes at 2048 x 2240.
            var probe = Probe(1280, 1400, 4096, 4096);
            Assert.Equal("2560x2800", Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { Mode = VrMode.Mono, RenderScale = 2.0 }, probe)))["ETERNALVR_RENDER_SIZE"]);
            Assert.Equal("2048x2240", Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { RenderScale = 2.0 }, probe)))["ETERNALVR_RENDER_SIZE"]);
        }

        [Fact]
        public void MonoTakesResolutionsBaseButNoEdgeMargin()
        {
            var probe = Probe(3072, 3264, 4096, 16384);
            // What the runtime asks for, exactly: Godlike's 3072 x 3264.
            var ask = LaunchPlanBuilder.Build(Inputs(new LauncherSettings { Mode = VrMode.Mono, ResolutionBase = ResolutionBase.Ask }, probe));
            Assert.Equal("3072x3264", Env(ask)["ETERNALVR_RENDER_SIZE"]);
            Assert.Contains("+r_windowWidth 3072 +r_windowHeight 3264", ask.CommandLine);
            // An edge margin set for stereo does not grow mono's image.
            foreach (var margin in new[] { EdgeMarginMode.Small, EdgeMarginMode.Medium, EdgeMarginMode.Large })
            {
                var p = LaunchPlanBuilder.Build(Inputs(new LauncherSettings { Mode = VrMode.Mono, EdgeMargin = margin }, probe));
                Assert.Equal("2072x2200", Env(p)["ETERNALVR_RENDER_SIZE"]);
                Assert.Equal(1.0, p.RenderSize.Growth);
            }
        }

        [Fact]
        public void TheWindowSetIsForcedInMonoOnlyWithTheRenderSize()
        {
            var shipped = ForcedCvars.Parse(TestData.Read("forced-cvars.txt"));
            var window = shipped.All.Where(c => c.WindowSet).Select(c => c.Name).ToList();
            Assert.Equal(new[] { "r_fullscreen", "r_windowWidth", "r_windowHeight" }, window);
            Assert.All(shipped.All.Where(c => c.WindowSet), c => Assert.False(c.StereoOnly || c.MonoOnly));
            // Stereo always; mono with the render size; never the rest of the stereo set (vsync, TAA, dynamic resolution).
            foreach (var name in window)
            {
                Assert.Contains(shipped.For(true), c => c.Name == name);
                Assert.Contains(shipped.For(true, false), c => c.Name == name);
                Assert.Contains(shipped.For(false, true), c => c.Name == name);
                Assert.DoesNotContain(shipped.For(false), c => c.Name == name);
            }
            Assert.DoesNotContain(shipped.For(false, true), c => c.StereoOnly);
            Assert.DoesNotContain(shipped.For(false, true), c => c.Name == "r_swapInterval" || c.Name == "rs_enable" || c.Name == "r_antialiasing");
            // The tag parses, takes the size placeholders, and is one scope among stereo and mono.
            var list = ForcedCvars.Parse("a | 1 | window\nb | $eye_width | window\nc | 2 | stereo\nd | 3 | mono");
            Assert.Equal(new[] { "a", "b", "c" }, list.For(true).Select(c => c.Name));
            Assert.Equal(new[] { "d" }, list.For(false).Select(c => c.Name));
            Assert.Equal(new[] { "a", "b", "d" }, list.For(false, true).Select(c => c.Name));
            Assert.Throws<System.FormatException>(() => ForcedCvars.Parse("a | $other | window"));
            Assert.Throws<System.FormatException>(() => ForcedCvars.Parse("a | 1 | windows"));
            // Restored after every session either way (the names are the restore's keys).
            Assert.Contains("r_fullscreen", shipped.Names);
        }
    }
}
