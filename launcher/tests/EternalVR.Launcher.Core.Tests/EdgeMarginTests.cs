using System;
using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Headsets;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>
    /// The edge margin (docs/VR_STEREO.md "Edge margin"): each eye wider than its view in stereo, the layer's
    /// ETERNALVR_EDGE_MARGIN, and an automatic render size grown by the same factor as the layer grows it
    /// (src/features/render_size/edge_margin.hpp, render_size_tests.cpp's vectors).
    /// </summary>
    public class EdgeMarginTests
    {
        private static LaunchInputs Inputs(LauncherSettings s) => new LaunchInputs
        {
            GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
            LayerDir = @"E:\EternalVR\layer",
            Settings = s,
            ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
        };

        private static Dictionary<string, string> Env(LaunchPlan p) => p.Environment.ToDictionary(e => e.Key, e => e.Value);

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
        public void TheSettingIsReadAndWritten()
        {
            Assert.Equal(LauncherSettings.DefaultEdgeMargin, new LauncherSettings().EdgeMargin);
            Assert.Contains("edge_margin = " + LauncherSettings.EdgeMarginName(LauncherSettings.DefaultEdgeMargin), new LauncherSettings().Serialize());
            foreach (EdgeMarginMode m in Enum.GetValues(typeof(EdgeMarginMode)))
                Assert.Equal(m, LauncherSettings.Parse(new LauncherSettings { EdgeMargin = m }.Serialize()).EdgeMargin);
            Assert.Equal(EdgeMarginMode.Large, LauncherSettings.Parse("schema_version = 2\nedge_margin = Large\n").EdgeMargin);
            // Unknown or missing: the default.
            Assert.Equal(LauncherSettings.DefaultEdgeMargin, LauncherSettings.Parse("schema_version = 2\nedge_margin = huge\n").EdgeMargin);
            Assert.Equal(LauncherSettings.DefaultEdgeMargin, LauncherSettings.Parse("schema_version = 2\n").EdgeMargin);
        }

        [Fact]
        public void TheWindowHasOneChoicePerSizeAndOnlyInStereo()
        {
            var text = SettingTexts.For(Setting.EdgeMargin);
            Assert.Equal(Enum.GetValues(typeof(EdgeMarginMode)).Length, text.Choices.Count);
            Assert.Contains("black", text.Tooltip);
            Assert.Null(SettingRules.WhyNot(Setting.EdgeMargin, new LauncherSettings()));
            Assert.Null(SettingRules.WhyNot(Setting.EdgeMargin, new LauncherSettings { ParallelEyes = true }));
            Assert.Equal(SettingRules.NeedsStereo, SettingRules.WhyNot(Setting.EdgeMargin, new LauncherSettings { Mode = VrMode.Mono }));
        }

        [Fact]
        public void TheGrowthIsTheLayersFactor()
        {
            // The layer's float constants as doubles: what render_size::autoSize multiplies by.
            Assert.Equal(1.0, LauncherSettings.EdgeMarginGrowth(EdgeMarginMode.Off));
            Assert.Equal((double)1.10f, LauncherSettings.EdgeMarginGrowth(EdgeMarginMode.Small));
            Assert.Equal((double)1.20f, LauncherSettings.EdgeMarginGrowth(EdgeMarginMode.Medium));
            Assert.Equal((double)1.30f, LauncherSettings.EdgeMarginGrowth(EdgeMarginMode.Large));
            Assert.Equal("off", LauncherSettings.EdgeMarginName(EdgeMarginMode.Off));
            Assert.Equal("small", LauncherSettings.EdgeMarginName(EdgeMarginMode.Small));
            Assert.Equal("medium", LauncherSettings.EdgeMarginName(EdgeMarginMode.Medium));
            Assert.Equal("large", LauncherSettings.EdgeMarginName(EdgeMarginMode.Large));
        }

        [Fact]
        public void StereoPassesTheMarginAndMonoDoesNot()
        {
            foreach (EdgeMarginMode m in Enum.GetValues(typeof(EdgeMarginMode)))
                Assert.Equal(LauncherSettings.EdgeMarginName(m), Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { EdgeMargin = m })))["ETERNALVR_EDGE_MARGIN"]);
            Assert.False(Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { Mode = VrMode.Mono, EdgeMargin = EdgeMarginMode.Large })))
                .ContainsKey("ETERNALVR_EDGE_MARGIN"));
        }

        [Fact]
        public void AnAutomaticSizeGrowsAsTheLayersDoes()
        {
            // Quest 3 through VDXR: 2056 x 2216 without, then as render_size_tests.cpp.
            var i = Inputs(new LauncherSettings { EdgeMargin = EdgeMarginMode.Off });
            i.RuntimeProbe = Probe(2496, 2688);
            Assert.Equal("2056x2216", Env(LaunchPlanBuilder.Build(i))["ETERNALVR_RENDER_SIZE"]);
            var expected = new Dictionary<EdgeMarginMode, string>
            {
                [EdgeMarginMode.Small] = "2264x2440",
                [EdgeMarginMode.Medium] = "2472x2656",
                [EdgeMarginMode.Large] = "2672x2880",
            };
            foreach (var kv in expected)
            {
                i.Settings = new LauncherSettings { EdgeMargin = kv.Key };
                var p = LaunchPlanBuilder.Build(i);
                Assert.Equal(kv.Value, Env(p)["ETERNALVR_RENDER_SIZE"]);
                Assert.Contains("+r_windowWidth " + kv.Value.Replace("x", " +r_windowHeight "), p.CommandLine);
                Assert.Contains("and the edge margin's " + LauncherSettings.EdgeMarginGrowth(kv.Key).ToString("0.00", System.Globalization.CultureInfo.InvariantCulture), p.Describe());
            }
            // The test PC's simulator: 1280 x 1400, 2048 per eye side by side.
            i.Settings = new LauncherSettings { EdgeMargin = EdgeMarginMode.Medium };
            i.RuntimeProbe = Probe(1280, 1400, 4096);
            Assert.Equal("1536x1680", Env(LaunchPlanBuilder.Build(i))["ETERNALVR_RENDER_SIZE"]);
            // The layer's own rule, directly.
            Assert.Equal(new Extent(2472, 2656),
                RenderSize.AutoSize(Probe(2496, 2688).Limits.WithEyesSideBySide(2), 1.0f, RenderSize.DefaultPixelBudget, (double)1.20f));
            Assert.Null(RenderSize.AutoSize(Probe(2496, 2688).Limits, 1.0f, RenderSize.DefaultPixelBudget, 0.0));
            Assert.Equal("auto x1.00, edge margin x1.20",
                RenderSize.Describe(new RenderSizeRequest { Mode = RenderSizeMode.Auto, Growth = 1.2 }));
        }

        [Fact]
        public void EveryAutomaticBaseGrowsAndAFixedSizeDoesNot()
        {
            var probe = Probe(2496, 2688);
            // What the runtime asks for: 2496 x 2688 times 1.2 (2995 x 3226), sides to multiples of 8.
            var ask = RenderSizeChoice.Decide("auto", 1.0, probe, ResolutionBase.Ask, null, (double)1.20f);
            Assert.Equal(new Extent(2992, 3224), ask.Size);
            Assert.Equal(new Extent(2496, 2688), RenderSizeChoice.Decide("auto", 1.0, probe, ResolutionBase.Ask, null).Size);
            // The native panel (a Quest 3's 2064 x 2208).
            var panel = RenderSizeChoice.Decide("auto", 1.0, probe, ResolutionBase.Panel, new Extent(2064, 2208), (double)1.20f);
            Assert.Equal(new Extent(2480, 2648), panel.Size);
            // A fixed size is the player's own: used as it is.
            var fixedSize = RenderSizeChoice.Decide("2064x2208", 1.0, probe, ResolutionBase.Auto, null, (double)1.20f);
            Assert.Equal(new Extent(2064, 2208), fixedSize.Size);
            Assert.Equal(1.0, fixedSize.Growth);
        }

        [Fact]
        public void TheEachEyeLineSaysTheMarginIsIn()
        {
            var facts = new HeadsetFacts { Limits = Probe(2496, 2688).Limits };
            var off = HeadsetView.EachEye(new LauncherSettings { EdgeMargin = EdgeMarginMode.Off }, facts, null);
            Assert.DoesNotContain("edge margin", off);
            var medium = HeadsetView.EachEye(new LauncherSettings { EdgeMargin = EdgeMarginMode.Medium }, facts, null);
            Assert.Contains("2472 x 2656", medium);
            Assert.Contains("With the edge margin (Medium", medium);
        }

        [Fact]
        public void NoneIsOff()
        {
            Assert.Equal(EdgeMarginMode.Off, LauncherSettings.Parse("schema_version = 2\nedge_margin = None\n").EdgeMargin);
        }

        [Fact]
        public void TheMarginAFittedSizeKeepsIsTheLayers()
        {
            // render_size_tests.cpp's "the margin a fitted size keeps", vector for vector.
            double medium = (double)1.20f, small = (double)1.10f;
            var quest3 = Probe(2496, 2688).Limits.WithEyesSideBySide(2);
            var auto = new RenderSizeRequest { Mode = RenderSizeMode.Auto };
            Assert.Equal(medium, RenderSize.RequestGrowth(auto, quest3, medium));
            var narrow = Limits(2496, 2688, new Extent(2300, 4096), new Extent(16384, 16384));
            Assert.Equal(1.1178615, RenderSize.RequestGrowth(auto, narrow, medium), 6);
            Assert.Equal(small, RenderSize.RequestGrowth(auto, narrow, small));
            var sim = Limits(2496, 2688, default, new Extent(4096, 4096));
            Assert.Equal(1.0, RenderSize.RequestGrowth(auto, sim, medium), 9);
            var grown = new RenderSizeRequest { Mode = RenderSizeMode.Fixed, Fixed = new Extent(2472, 2656) };
            Assert.Equal(medium, RenderSize.RequestGrowth(grown, quest3, medium));
            Assert.Equal(1.1165049, RenderSize.RequestGrowth(grown, narrow, medium), 6);
            Assert.Equal(1.0, RenderSize.RequestGrowth(grown, Limits(2496, 2688, new Extent(2000, 4096), new Extent(16384, 16384)), medium), 9);
            Assert.Equal(medium, RenderSize.RequestGrowth(new RenderSizeRequest(), quest3, medium));
            Assert.Equal(1.0, RenderSize.RequestGrowth(auto, quest3, 1.0));
            Assert.Equal(1.0, RenderSize.RequestGrowth(auto, quest3, double.NaN));
            Assert.Equal(medium, RenderSize.FittedGrowth(default, medium, quest3));
        }

        [Fact]
        public void ALaunchWhoseSizeTheLimitsCutPassesTheMarginItKeeps()
        {
            // Medium on Auto with an image limit of 2300: the size is cut to 2300 wide, which holds x1.118 at the centre's
            // density; the layer is told 11 (rounded down), so it never widens the FOV past what the size holds.
            var i = Inputs(new LauncherSettings { EdgeMargin = EdgeMarginMode.Medium });
            i.RuntimeProbe = new OpenXrProbeResult { RuntimeName = "test", SystemName = "test", Limits = Limits(2496, 2688, new Extent(2300, 4096), new Extent(16384, 16384)) };
            var p = LaunchPlanBuilder.Build(i);
            Assert.Equal("11", Env(p)["ETERNALVR_EDGE_MARGIN"]);
            Assert.True(RenderSize.ParseRenderSize(Env(p)["ETERNALVR_RENDER_SIZE"]).Fixed.Width <= 2300);
            Assert.Contains("the runtime's limits keep x1.118 of the margin", p.Describe());
            // The same Resolution where the grown size fits: the setting's name.
            i.RuntimeProbe = Probe(2496, 2688);
            Assert.Equal("medium", Env(LaunchPlanBuilder.Build(i))["ETERNALVR_EDGE_MARGIN"]);
            // What the runtime asks for, cut so far that nothing of the margin is left at the centre's density: off.
            i.Settings = new LauncherSettings { EdgeMargin = EdgeMarginMode.Medium, ResolutionBase = ResolutionBase.Ask };
            i.RuntimeProbe = Probe(2496, 2688, 4096);
            Assert.Equal("off", Env(LaunchPlanBuilder.Build(i))["ETERNALVR_EDGE_MARGIN"]);
            // A render_size set by hand is not grown: it renders the whole margin at its size, so the setting's name goes.
            i.Settings = new LauncherSettings { EdgeMargin = EdgeMarginMode.Medium, RenderSize = "2064x2208" };
            i.RuntimeProbe = Probe(2496, 2688);
            var handSet = Env(LaunchPlanBuilder.Build(i));
            Assert.Equal("2064x2208", handSet["ETERNALVR_RENDER_SIZE"]);
            Assert.Equal("medium", handSet["ETERNALVR_EDGE_MARGIN"]);
            i.RuntimeProbe = null;
            Assert.Equal("medium", Env(LaunchPlanBuilder.Build(i))["ETERNALVR_EDGE_MARGIN"]);
            // The value itself.
            Assert.Equal("medium", LauncherSettings.EdgeMarginValue(EdgeMarginMode.Medium, (double)1.20f));
            // A kept factor an ulp under the growth is the whole margin, not 19 or 9.
            Assert.Equal("medium", LauncherSettings.EdgeMarginValue(EdgeMarginMode.Medium, Math.BitDecrement((double)1.20f)));
            Assert.Equal("small", LauncherSettings.EdgeMarginValue(EdgeMarginMode.Small, Math.BitDecrement((double)1.10f)));
            Assert.Equal("medium", LauncherSettings.EdgeMarginValue(EdgeMarginMode.Medium, null));
            Assert.Equal("11", LauncherSettings.EdgeMarginValue(EdgeMarginMode.Medium, 1.1178));
            Assert.Equal("off", LauncherSettings.EdgeMarginValue(EdgeMarginMode.Medium, 1.004));
            Assert.Equal("off", LauncherSettings.EdgeMarginValue(EdgeMarginMode.Off, 1.0));
        }

        [Fact]
        public void TheEachEyeLineSaysWhatTheMarginCostsAndWhenItIsCut()
        {
            // Small's 21% is under the "more pixels than Auto" note's quarter: the margin line says it.
            var facts = new HeadsetFacts { Limits = Probe(2496, 2688).Limits };
            var small = HeadsetView.EachEye(new LauncherSettings { EdgeMargin = EdgeMarginMode.Small }, facts, null);
            Assert.Contains("With the edge margin (Small): 21% more pixels", small);
            var medium = HeadsetView.EachEye(new LauncherSettings { EdgeMargin = EdgeMarginMode.Medium }, facts, null);
            Assert.Contains("With the edge margin (Medium): 44% more pixels", medium);
            Assert.DoesNotContain("cut to", medium);
            // An image limit of 2300: the size is cut, and with it the margin.
            var narrow = new HeadsetFacts { Limits = Limits(2496, 2688, new Extent(2300, 4096), new Extent(16384, 16384)) };
            var cut = HeadsetView.EachEye(new LauncherSettings { EdgeMargin = EdgeMarginMode.Medium }, narrow, null);
            Assert.Contains("cut to x1.11 by the runtime's limits", cut);
        }

        // Limits with the image and swapchain maxima given apart (the probe's own, before Route S's two eyes side by side).
        private static ViewLimits Limits(uint recW, uint recH, Extent maxImage, Extent maxSwapchain) => new ViewLimits
        {
            Recommended = new Extent(recW, recH),
            MaxImageRect = maxImage,
            MaxSwapchain = maxSwapchain,
            EyesSideBySide = 2,
        };
    }
}
