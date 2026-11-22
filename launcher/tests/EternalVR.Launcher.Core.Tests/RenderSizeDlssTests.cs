using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Preflight;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>
    /// Resolution's Auto with DLSS: the pixel budget holds what DLSS renders (<see cref="RenderSize.AutoSize"/>), with the cases of
    /// the layer's own tests, tests/features/render_size/render_size_dlss_tests.cpp, so the launcher and the layer agree.
    /// </summary>
    public class RenderSizeDlssTests
    {
        private static Extent E(uint w, uint h) => new Extent(w, h);

        private static ViewLimits Headset(uint w, uint h) => new ViewLimits
        {
            Recommended = E(w, h),
            MaxImageRect = E(4096, 4096),
            MaxSwapchain = E(16384, 16384),
            EyesSideBySide = 2,
        };

        // Quest 3 through VDXR at its default (High), Virtual Desktop's Godlike, and a recommendation large enough that DLSS
        // Quality's input passes the budget.
        private static ViewLimits Vdxr() => Headset(2496, 2688);
        private static ViewLimits Godlike() => Headset(3072, 3264);
        private static ViewLimits Big() => Headset(3840, 3840);
        // The test PC's simulator: 1280 x 1400, 2048 per eye side by side.
        private static ViewLimits Simulator() => new ViewLimits { Recommended = E(1280, 1400), MaxSwapchain = E(4096, 4096), EyesSideBySide = 2 };

        // The edge margin's factors, as the layer multiplies by them.
        private static readonly double Small = LauncherSettings.EdgeMarginGrowth(EdgeMarginMode.Small);
        private static readonly double Medium = LauncherSettings.EdgeMarginGrowth(EdgeMarginMode.Medium);
        private static readonly double Large = LauncherSettings.EdgeMarginGrowth(EdgeMarginMode.Large);

        private static readonly DlssQuality[] Scaled = { DlssQuality.UltraPerformance, DlssQuality.Performance, DlssQuality.Balanced, DlssQuality.Quality };

        private static Extent Dlss(ViewLimits l, DlssQuality q, float scale = 1.0f, double growth = 1.0) =>
            RenderSize.AutoSize(l, scale, RenderSize.DefaultPixelBudget, growth, RenderSize.DlssInputShare(q)).Value;

        [Fact]
        public void DlssInputShareIsNvidiasFactorPerQuality()
        {
            Assert.Equal(1.0 / 3.0, RenderSize.DlssInputShare(DlssQuality.UltraPerformance), 6);
            Assert.Equal(0.5, RenderSize.DlssInputShare(DlssQuality.Performance), 6);
            Assert.Equal(0.58, RenderSize.DlssInputShare(DlssQuality.Balanced), 6);
            Assert.Equal(2.0 / 3.0, RenderSize.DlssInputShare(DlssQuality.Quality), 6);
            Assert.Equal(1.0, RenderSize.DlssInputShare(DlssQuality.Dlaa));
        }

        [Fact]
        public void VdxrsDefaultRendersTheRecommendation()
        {
            Assert.Equal(E(2056, 2216), RenderSize.AutoSize(Vdxr(), 1.0f).Value);
            Assert.Equal(E(2056, 2216), Dlss(Vdxr(), DlssQuality.Dlaa));
            foreach (var q in Scaled) Assert.Equal(E(2496, 2688), Dlss(Vdxr(), q));
        }

        [Fact]
        public void GodlikeAndALargerRecommendationStayWithinTheOutputCap()
        {
            // Godlike's 3072 x 3264 (10.0 Mpix) passes 1.5 times the budget (6.84 Mpix): scaled down to it, at every quality.
            Assert.Equal(1.5, RenderSize.DlssOutputCap);
            Assert.Equal(E(2072, 2200), RenderSize.AutoSize(Godlike(), 1.0f).Value);
            foreach (var q in Scaled)
            {
                Assert.Equal(E(2536, 2696), Dlss(Godlike(), q));
                Assert.Equal(E(2616, 2616), Dlss(Big(), q));
                foreach (var e in new[] { Dlss(Godlike(), q), Dlss(Big(), q) })
                {
                    Assert.True((double)e.Width * e.Height <= RenderSize.DefaultPixelBudget * 1.5 * 1.01);
                    double input = e.Width * RenderSize.DlssInputShare(q);
                    Assert.True(input * input <= RenderSize.DefaultPixelBudget * 1.01);
                }
            }
            Assert.Equal(E(2136, 2136), RenderSize.AutoSize(Big(), 1.0f).Value);
            // DLAA keeps the budget on the output, as TAA does.
            Assert.Equal(E(2072, 2200), Dlss(Godlike(), DlssQuality.Dlaa));
        }

        [Fact]
        public void TheSimulatorsRecommendationIsWithinTheBudgetEitherWay()
        {
            foreach (var q in Scaled)
            {
                Assert.Equal(E(1280, 1400), Dlss(Simulator(), q));
                Assert.Equal(E(1536, 1680), Dlss(Simulator(), q, 1.0f, Medium));
            }
        }

        [Fact]
        public void TheScaleAndTheEdgeMarginMultiplyAfterTheBudget()
        {
            Assert.Equal(E(2000, 2152), Dlss(Vdxr(), DlssQuality.Quality, 0.8f));
            Assert.Equal(E(2992, 3224), Dlss(Vdxr(), DlssQuality.Quality, 1.2f));
            // The cap holds before the scale: a scale above 1 goes past it on purpose, as past the budget with TAA.
            Assert.Equal(E(2032, 2160), Dlss(Godlike(), DlssQuality.Quality, 0.8f));
            Assert.Equal(E(3048, 3232), Dlss(Godlike(), DlssQuality.Quality, 1.2f));
            Assert.Equal(E(2744, 2960), Dlss(Vdxr(), DlssQuality.Quality, 1.0f, Small));
            Assert.Equal(E(2992, 3224), Dlss(Vdxr(), DlssQuality.Quality, 1.0f, Medium));
            Assert.Equal(E(3248, 3496), Dlss(Vdxr(), DlssQuality.Quality, 1.0f, Large));
            // The edge margin grows the capped size too: Godlike with Large fits the runtime's 4096.
            Assert.Equal(E(3296, 3504), Dlss(Godlike(), DlssQuality.Quality, 1.0f, Large));
            var r = new RenderSizeRequest { Mode = RenderSizeMode.Auto, DlssInput = RenderSize.DlssInputShare(DlssQuality.Quality) };
            double large = Large;
            Assert.Equal(large, RenderSize.RequestGrowth(r, Godlike(), large));
            Assert.Equal(large, RenderSize.RequestGrowth(r, Vdxr(), large));
            // With 1.2 as well it passes 4096: fitted down, and only part of the margin is kept.
            Assert.Equal(E(3856, 4096), Dlss(Godlike(), DlssQuality.Quality, 1.2f, Large));
            r.Scale = 1.2f;
            Assert.Equal(1.2666032, RenderSize.RequestGrowth(r, Godlike(), large), 6);
            r.DlssInput = 1.0;
            Assert.Equal(large, RenderSize.RequestGrowth(r, Godlike(), large));
        }

        [Fact]
        public void ThroughTheRequestAndABadShareCountsAsNone()
        {
            var r = new RenderSizeRequest { Mode = RenderSizeMode.Auto, DlssInput = RenderSize.DlssInputShare(DlssQuality.Quality) };
            Assert.Equal(E(2496, 2688), RenderSize.SelectSize(r, Vdxr()).Value);
            r.Growth = Medium;
            Assert.Equal(E(2992, 3224), RenderSize.SelectSize(r, Vdxr()).Value);
            var fixedSize = new RenderSizeRequest { Mode = RenderSizeMode.Fixed, Fixed = E(2056, 2216), DlssInput = 2.0 / 3.0 };
            Assert.Equal(E(2056, 2216), RenderSize.SelectSize(fixedSize, Vdxr()).Value);
            foreach (var bad in new[] { 0.0, -0.5, 1.5, double.NaN })
                Assert.Equal(E(2056, 2216), RenderSize.AutoSize(Vdxr(), 1.0f, RenderSize.DefaultPixelBudget, 1.0, bad).Value);
        }

        [Fact]
        public void Describe()
        {
            var a = new RenderSizeRequest { Mode = RenderSizeMode.Auto, DlssInput = RenderSize.DlssInputShare(DlssQuality.Quality) };
            Assert.Equal("auto x1.00, the budget on DLSS's input x0.667", RenderSize.Describe(a));
            a.Growth = 1.2;
            Assert.Equal("auto x1.00, edge margin x1.20, the budget on DLSS's input x0.667", RenderSize.Describe(a));
            a.DlssInput = 1.0;
            Assert.Equal("auto x1.00, edge margin x1.20", RenderSize.Describe(a));
        }

        private static OpenXrProbeResult Probe(ViewLimits l) => new OpenXrProbeResult { RuntimeName = "test", SystemName = "test", Limits = l };

        private static Dictionary<string, string> Env(LaunchPlan p) => p.Environment.ToDictionary(e => e.Key, e => e.Value);

        private static LaunchPlan Launch(LauncherSettings s, ViewLimits l, bool nvidiaRtx = true) => LaunchPlanBuilder.Build(new LaunchInputs
        {
            GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
            LayerDir = @"E:\EternalVR\layer",
            Settings = s,
            ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
            RuntimeProbe = Probe(l),
            NvidiaRtx = nvidiaRtx,
        });

        [Fact]
        public void TheLaunchStartsTheGameAtTheSizeWhoseDlssInputFits()
        {
            // TAA (the default) and Off: as before.
            Assert.Equal("2056x2216", Env(Launch(new LauncherSettings(), Vdxr()))["ETERNALVR_RENDER_SIZE"]);
            Assert.Equal("2056x2216", Env(Launch(new LauncherSettings { AntiAliasing = AntiAliasingMode.Off }, Vdxr()))["ETERNALVR_RENDER_SIZE"]);
            // DLSS Quality: the recommendation, said in the launcher's log.
            var p = Launch(new LauncherSettings { AntiAliasing = AntiAliasingMode.Dlss, Dlss = DlssQuality.Quality }, Vdxr());
            Assert.Equal("2496x2688", Env(p)["ETERNALVR_RENDER_SIZE"]);
            Assert.Contains("+r_windowWidth 2496 +r_windowHeight 2688", p.CommandLine);
            Assert.Contains("render size 2496x2688 from the runtime's recommendation 2496x2688 (scale 1.00), the pixel budget on what DLSS renders (x0.667 per side)", p.Describe());
            Assert.Equal("2536x2696", Env(Launch(new LauncherSettings { AntiAliasing = AntiAliasingMode.Dlss }, Godlike()))["ETERNALVR_RENDER_SIZE"]);
            Assert.Equal("2616x2616", Env(Launch(new LauncherSettings { AntiAliasing = AntiAliasingMode.Dlss }, Big()))["ETERNALVR_RENDER_SIZE"]);
            // DLAA renders at the full size: as before.
            Assert.Equal("2056x2216", Env(Launch(new LauncherSettings { AntiAliasing = AntiAliasingMode.Dlss, Dlss = DlssQuality.Dlaa }, Vdxr()))["ETERNALVR_RENDER_SIZE"]);
            // The other bases have no budget: the same with DLSS.
            Assert.Equal("2496x2688", Env(Launch(new LauncherSettings { AntiAliasing = AntiAliasingMode.Dlss, ResolutionBase = ResolutionBase.Ask }, Vdxr()))["ETERNALVR_RENDER_SIZE"]);
            Assert.Equal("3072x3264", Env(Launch(new LauncherSettings { AntiAliasing = AntiAliasingMode.Dlss, ResolutionBase = ResolutionBase.Ask }, Godlike()))["ETERNALVR_RENDER_SIZE"]);
            // A fixed size is used as it is.
            Assert.Equal("2064x2208", Env(Launch(new LauncherSettings { AntiAliasing = AntiAliasingMode.Dlss, RenderSize = "2064x2208" }, Vdxr()))["ETERNALVR_RENDER_SIZE"]);
            // Mono has no DLSS of the launcher's.
            Assert.Equal(1.0, RenderSizeChoice.DlssInputFor(new LauncherSettings { Mode = VrMode.Mono, AntiAliasing = AntiAliasingMode.Dlss }, true));
        }

        [Fact]
        public void OnlyAnNvidiaRtxCardGetsTheBudgetOnWhatDlssDraws()
        {
            // Without an RTX card (or with none known) DLSS cannot run per eye and the layer falls back to TAA: today's size,
            // not the larger one TAA would then draw.
            var dlss = new LauncherSettings { AntiAliasing = AntiAliasingMode.Dlss };
            Assert.Equal(2.0 / 3.0, RenderSizeChoice.DlssInputFor(dlss, true), 6);
            Assert.Equal(1.0, RenderSizeChoice.DlssInputFor(dlss, false));
            Assert.Equal("2496x2688", Env(Launch(dlss, Vdxr(), nvidiaRtx: true))["ETERNALVR_RENDER_SIZE"]);
            Assert.Equal("2056x2216", Env(Launch(dlss, Vdxr(), nvidiaRtx: false))["ETERNALVR_RENDER_SIZE"]);
            Assert.Equal("2072x2200", Env(Launch(dlss, Godlike(), nvidiaRtx: false))["ETERNALVR_RENDER_SIZE"]);
            // Unknown is the default of the launch's inputs.
            Assert.False(new LaunchInputs().NvidiaRtx);
            // The card, by the preflight's own reading of the adapters (UntestedHardware.MainGpu).
            GpuAdapter Gpu(string name, long gb) => new GpuAdapter(name, gb << 30, true);
            Assert.True(UntestedHardware.MainGpuIsNvidiaRtx(new[] { Gpu("NVIDIA GeForce RTX 3080 Ti", 12) }));
            Assert.True(UntestedHardware.MainGpuIsNvidiaRtx(new[] { new GpuAdapter("AMD Radeon(TM) Graphics", 512L << 20, true),
                Gpu("NVIDIA GeForce RTX 4070 Laptop GPU", 8), new GpuAdapter("Virtual Display Driver", null, false) }));
            Assert.False(UntestedHardware.MainGpuIsNvidiaRtx(new[] { Gpu("NVIDIA GeForce GTX 1080 Ti", 11) }));
            Assert.False(UntestedHardware.MainGpuIsNvidiaRtx(new[] { Gpu("AMD Radeon RX 7900 XTX", 24) }));
            Assert.False(UntestedHardware.MainGpuIsNvidiaRtx(new GpuAdapter[0]));
            Assert.False(UntestedHardware.MainGpuIsNvidiaRtx(null));
            Assert.False(UntestedHardware.MainGpuIsNvidiaRtx(new[] { new GpuAdapter("Virtual Display Driver", null, false) }));
        }
    }
}
