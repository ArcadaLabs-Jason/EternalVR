using System;
using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>
    /// Resolution's base (<c>resolution_base</c>): Auto (the budgeted ask, the default and every older launcher's size), what
    /// the runtime asks for, or the native panel, each times the number; the launch works the size out before the game starts.
    /// </summary>
    public class ResolutionBaseTests
    {
        private static LaunchInputs Inputs(LauncherSettings s, OpenXrProbeResult probe = null, Extent? panel = null) => new LaunchInputs
        {
            GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
            LayerDir = @"E:\EternalVR\layer",
            Settings = s,
            ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
            RuntimeProbe = probe,
            Panel = panel,
        };

        private static Dictionary<string, string> Env(LaunchPlan p) => p.Environment.ToDictionary(e => e.Key, e => e.Value);

        private static OpenXrProbeResult Probe(uint recW, uint recH, uint maxSwapchain = 16384) => new OpenXrProbeResult
        {
            RuntimeName = "VirtualDesktopXR",
            SystemName = "Meta Quest 3",
            Limits = new ViewLimits
            {
                Recommended = new Extent(recW, recH),
                MaxImageRect = new Extent(maxSwapchain, maxSwapchain),
                MaxSwapchain = new Extent(maxSwapchain, maxSwapchain),
            },
        };

        private static readonly Extent Quest3Panel = new Extent(2064, 2208);

        private static string Size(LauncherSettings s, OpenXrProbeResult probe, Extent? panel = null) =>
            Env(LaunchPlanBuilder.Build(Inputs(s, probe, panel)))["ETERNALVR_RENDER_SIZE"];

        [Fact]
        public void AutoIsTheDefaultAndAnOlderFileIsAutoWithItsNumberAsItWas()
        {
            Assert.Equal(ResolutionBase.Auto, new LauncherSettings().ResolutionBase);
            Assert.Equal(1.0, new LauncherSettings().RenderScale);
            // A launcher 0.1.11 file: no resolution_base, its render_scale kept.
            var old = LauncherSettings.Parse("schema_version = 2\nrender_size = auto\nrender_scale = 1.45\n");
            Assert.Equal(ResolutionBase.Auto, old.ResolutionBase);
            Assert.Equal(1.45, old.RenderScale, 3);
            Assert.Contains("resolution_base = auto", old.Serialize());
            Assert.Contains("render_scale = 1.45", old.Serialize());
            // A value this version does not know: the default.
            Assert.Equal(ResolutionBase.Auto, LauncherSettings.Parse("schema_version = 2\nresolution_base = huge\n").ResolutionBase);
        }

        [Fact]
        public void UpdatingChangesNoOnesSize()
        {
            // The same file renders exactly what launcher 0.1.11 rendered: the layer's auto rule, budget first, then the number.
            foreach (var scale in new[] { 0.5, 0.8, 1.0, 1.25, 1.45, 2.0 })
            {
                var old = LauncherSettings.Parse("schema_version = 2\nrender_scale = " + scale.ToString("0.00", System.Globalization.CultureInfo.InvariantCulture) + "\n");
                var probe = Probe(2496, 2688);
                var before = RenderSize.AutoSize(probe.Limits.WithEyesSideBySide(RenderSizeChoice.StereoEyesSideBySide), (float)scale);
                Assert.Equal(before.ToString(), Size(old, probe, Quest3Panel));
            }
            Assert.Equal("2056x2216", Size(LauncherSettings.Parse("schema_version = 2\n"), Probe(2496, 2688)));
            Assert.Equal("2984x3216", Size(LauncherSettings.Parse("schema_version = 2\nrender_scale = 1.45\n"), Probe(2496, 2688)));
        }

        [Fact]
        public void TheChoiceRoundTrips()
        {
            foreach (ResolutionBase b in Enum.GetValues(typeof(ResolutionBase)))
                Assert.Equal(b, LauncherSettings.Parse(new LauncherSettings { ResolutionBase = b }.Serialize()).ResolutionBase);
            Assert.Contains("resolution_base = ask", new LauncherSettings { ResolutionBase = ResolutionBase.Ask }.Serialize());
            Assert.Contains("resolution_base = panel", new LauncherSettings { ResolutionBase = ResolutionBase.Panel }.Serialize());
            Assert.Empty(LauncherSettings.Parse("schema_version = 2\nresolution_base = panel\n").UnknownKeys);
            Assert.Equal(ResolutionBase.Auto, new LauncherSettings { ResolutionBase = ResolutionBase.Panel }.WithDefaults().ResolutionBase);
            // One choice per base, in enum order.
            Assert.Equal(new[] { "Auto (fits about 4.6 MP per eye)", "Runtime native", "Headset native" },
                SettingTexts.For(Setting.Resolution).Choices);
        }

        [Fact]
        public void WhatTheRuntimeAsksForIsThatSizeTimesTheNumber()
        {
            var ask = new LauncherSettings { ResolutionBase = ResolutionBase.Ask };
            var p = LaunchPlanBuilder.Build(Inputs(ask, Probe(2496, 2688)));
            Assert.Equal("2496x2688", Env(p)["ETERNALVR_RENDER_SIZE"]);
            Assert.Contains("+r_windowWidth 2496 +r_windowHeight 2688", p.CommandLine);
            Assert.Equal(ResolutionBase.Ask, p.RenderSize.Base);
            Assert.Null(p.RenderSize.BaseNote);
            Assert.Contains("render:  render size 2496x2688 from what the runtime asks for, 2496x2688, times 1.00", p.Describe());
            // 2496 x 1.2 = 2995.2 and 2688 x 1.2 = 3225.6, rounded to multiples of 8.
            ask.RenderScale = 1.2;
            Assert.Equal("2992x3224", Size(ask, Probe(2496, 2688)));
            // The layer still gets the number (it uses it only when it decides in-game).
            Assert.Equal("1.20", Env(LaunchPlanBuilder.Build(Inputs(ask, Probe(2496, 2688))))["ETERNALVR_RENDER_SCALE"]);
        }

        [Fact]
        public void TheNativePanelIsThePanelTimesTheNumber()
        {
            var panel = new LauncherSettings { ResolutionBase = ResolutionBase.Panel };
            var p = LaunchPlanBuilder.Build(Inputs(panel, Probe(2496, 2688), Quest3Panel));
            Assert.Equal("2064x2208", Env(p)["ETERNALVR_RENDER_SIZE"]);
            Assert.Equal(ResolutionBase.Panel, p.RenderSize.Base);
            Assert.Equal(Quest3Panel, p.RenderSize.Panel);
            Assert.Contains("render size 2064x2208 from the headset's native panel 2064x2208 times 1.00 (the runtime asks for 2496x2688)", p.Describe());
            panel.RenderScale = 0.8;
            Assert.Equal("1648x1768", Size(panel, Probe(2496, 2688), Quest3Panel));
            panel.RenderScale = 1.25;
            Assert.Equal("2584x2760", Size(panel, Probe(2496, 2688), Quest3Panel));
        }

        [Fact]
        public void TheSizeStaysWithinTheRuntimesLimitsAndTheLayersLargestFixedSize()
        {
            var ask = new LauncherSettings { ResolutionBase = ResolutionBase.Ask, RenderScale = 2.0 };
            // The OpenXR simulator: 4096 wide for both eyes side by side.
            Assert.Equal("2048x2240", Size(ask, Probe(1280, 1400, 4096)));
            // A runtime that allows more than the layer takes as a fixed size: 8192 per side, and still decided before the launch.
            var p = LaunchPlanBuilder.Build(Inputs(ask, Probe(5000, 5000)));
            Assert.Equal("8192x8192", Env(p)["ETERNALVR_RENDER_SIZE"]);
            Assert.NotNull(p.RenderSize.Size);
        }

        [Fact]
        public void AnUnknownPanelFallsBackToAutoAndTheLogSaysSo()
        {
            var panel = new LauncherSettings { ResolutionBase = ResolutionBase.Panel };
            var p = LaunchPlanBuilder.Build(Inputs(panel, Probe(2496, 2688), panel: null));
            Assert.Equal("2056x2216", Env(p)["ETERNALVR_RENDER_SIZE"]);
            Assert.Equal(ResolutionBase.Auto, p.RenderSize.Base);
            Assert.Contains("native panel is not known", p.RenderSize.BaseNote);
            Assert.Contains("render size 2056x2216 from the runtime's recommendation 2496x2688 (scale 1.00); the headset's native panel is not known", p.Describe());
        }

        [Fact]
        public void WithoutTheRuntimesAnswerEveryBaseIsDecidedInGameAsAuto()
        {
            foreach (var b in new[] { ResolutionBase.Ask, ResolutionBase.Panel })
            {
                var s = new LauncherSettings { ResolutionBase = b, RenderScale = 1.3 };
                var p = LaunchPlanBuilder.Build(Inputs(s, OpenXrProbeResult.Failed("xrGetSystem: XR_ERROR_FORM_FACTOR_UNAVAILABLE", true), Quest3Panel));
                // Exactly what a failed probe gave before: the layer's auto with the number.
                Assert.Equal("auto", Env(p)["ETERNALVR_RENDER_SIZE"]);
                Assert.Equal("1.30", Env(p)["ETERNALVR_RENDER_SCALE"]);
                Assert.Contains("+r_windowWidth 1280 +r_windowHeight 720", p.CommandLine);
                Assert.Equal(ResolutionBase.Auto, p.RenderSize.Base);
                Assert.Contains("needs the runtime's answer: the layer uses Auto", p.RenderSize.BaseNote);
                Assert.Contains("render size auto, decided in-game (the runtime did not answer", p.Describe());
                Assert.Contains("needs the runtime's answer", p.Describe());
                Assert.Equal("Headset not detected; the render size is decided in-game.", p.RenderSize.Note);
            }
            // Not asked at all: the same.
            var none = LaunchPlanBuilder.Build(Inputs(new LauncherSettings { ResolutionBase = ResolutionBase.Ask }));
            Assert.Equal("auto", Env(none)["ETERNALVR_RENDER_SIZE"]);
            Assert.NotNull(none.RenderSize.BaseNote);
        }

        [Fact]
        public void AFixedSizeIgnoresTheBase()
        {
            var s = new LauncherSettings { ResolutionBase = ResolutionBase.Ask, RenderSize = "2064x2208" };
            var p = LaunchPlanBuilder.Build(Inputs(s, Probe(2496, 2688), Quest3Panel));
            Assert.Equal("2064x2208", Env(p)["ETERNALVR_RENDER_SIZE"]);
            Assert.Equal(ResolutionBase.Auto, p.RenderSize.Base);
            Assert.Null(p.RenderSize.BaseNote);
            // And the render size off keeps the window's size, whatever the base.
            var off = Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { ResolutionBase = ResolutionBase.Panel, RenderSize = "off" }, Probe(2496, 2688))));
            Assert.False(off.ContainsKey("ETERNALVR_RENDER_SIZE"));
        }
    }
}
