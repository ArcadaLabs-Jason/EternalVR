using System;
using System.Runtime.InteropServices;
using EternalVR.Launcher.Core.Launch;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>
    /// The layer's render size rules (<see cref="RenderSize"/>) against the vectors of the layer's own tests,
    /// tests/features/render_size/render_size_tests.cpp, so the launcher and the layer agree on every size.
    /// </summary>
    public class RenderSizeMathTests
    {
        private static Extent E(uint w, uint h) => new Extent(w, h);

        private static ViewLimits Quest3Vdxr() => new ViewLimits
        {
            Recommended = E(2496, 2688),
            MaxImageRect = E(4096, 4096),
            MaxSwapchain = E(16384, 16384),
            EyesSideBySide = 2,
        };

        [Fact]
        public void RenderSizeSetting()
        {
            Assert.Equal(RenderSizeMode.Off, RenderSize.ParseRenderSize("").Mode);
            Assert.Equal(RenderSizeMode.Off, RenderSize.ParseRenderSize(" off ").Mode);
            Assert.Equal(RenderSizeMode.Off, RenderSize.ParseRenderSize("0").Mode);
            Assert.Equal(RenderSizeMode.Auto, RenderSize.ParseRenderSize("Auto").Mode);
            var f = RenderSize.ParseRenderSize("2064x2208");
            Assert.Equal(RenderSizeMode.Fixed, f.Mode);
            Assert.Equal(E(2064, 2208), f.Fixed);
            Assert.Equal(E(1024, 1024), RenderSize.ParseRenderSize(" 1024X1024 ").Fixed);
            foreach (var bad in new[] { "255x1024", "8193x1024", "2064", "2064x", "x2208", "2064x2208x3", "-2064x2208", "big" })
                Assert.Null(RenderSize.ParseRenderSize(bad));
        }

        [Fact]
        public void RenderScaleSetting()
        {
            Assert.Equal(1.0f, RenderSize.ParseRenderScale(""));
            Assert.Equal(1.0f, RenderSize.ParseRenderScale("auto"));
            Assert.Equal(0.8f, RenderSize.ParseRenderScale(" 0.8 "));
            Assert.Equal(1.25f, RenderSize.ParseRenderScale("1.25"));
            Assert.Equal(2.0f, RenderSize.ParseRenderScale("2"));
            Assert.Equal(0.5f, RenderSize.ParseRenderScale("0.5"));
            foreach (var bad in new[] { "0.49", "2.01", "1e0", "-1", "1.2.3", ".", "nan" })
                Assert.Null(RenderSize.ParseRenderScale(bad));
        }

        [Fact]
        public void AutoQuest3ThroughVdxrFitsTheDefaultBudget()
        {
            var size = RenderSize.AutoSize(Quest3Vdxr(), 1.0f).Value;
            Assert.Equal(E(2056, 2216), size);
            Assert.True((double)size.Width * size.Height <= RenderSize.DefaultPixelBudget * 1.01);
            Assert.Equal(2496.0 / 2688.0, (double)size.Width / size.Height, 2);
        }

        [Fact]
        public void AutoWithinTheBudgetIsUsedAsItIs()
        {
            Assert.Equal(E(1440, 1584), RenderSize.AutoSize(new ViewLimits { Recommended = E(1440, 1584) }, 1.0f));
            Assert.Equal(E(1880, 2024), RenderSize.AutoSize(new ViewLimits { Recommended = E(1880, 2024) }, 1.0f));
        }

        [Fact]
        public void AutoRenderScaleMultipliesTheBudgetedSize()
        {
            Assert.Equal(E(1032, 1112), RenderSize.AutoSize(Quest3Vdxr(), 0.5f));
            var more = RenderSize.AutoSize(Quest3Vdxr(), 1.2f).Value;
            Assert.Equal(E(2472, 2656), more);
            Assert.True((ulong)more.Width * more.Height > RenderSize.DefaultPixelBudget);
        }

        [Fact]
        public void AutoSidesAreMultiplesOf8()
        {
            Assert.Equal(E(1832, 1920), RenderSize.AutoSize(new ViewLimits { Recommended = E(1833, 1917) }, 1.0f));
        }

        [Fact]
        public void AutoNeedsARecommendationAndAPositiveScale()
        {
            var l = new ViewLimits();
            Assert.Null(RenderSize.AutoSize(l, 1.0f));
            l.Recommended = E(2000, 2000);
            Assert.Null(RenderSize.AutoSize(l, 0.0f));
            Assert.Null(RenderSize.AutoSize(l, -1.0f));
            Assert.Null(RenderSize.AutoSize(l, float.NaN));
        }

        [Fact]
        public void FitScalesDownUniformly()
        {
            Assert.Equal(E(2048, 2048), RenderSize.FitToLimits(E(4096, 4096), new ViewLimits { MaxImageRect = E(2048, 4096) }));
            // Two eyes side by side share the swapchain's width.
            var ring = new ViewLimits { MaxSwapchain = E(4096, 4096), EyesSideBySide = 2 };
            Assert.Equal(E(2048, 2208), RenderSize.FitToLimits(E(2496, 2688), ring));
            Assert.Equal(E(16384, 8192), RenderSize.FitToLimits(E(20000, 10000), new ViewLimits()));
            Assert.Equal(default(Extent), RenderSize.FitToLimits(E(0, 100), new ViewLimits()));
            Assert.Equal(E(256, 256), RenderSize.FitToLimits(E(100, 120), new ViewLimits()));
        }

        [Fact]
        public void SelectOffFixedAndAuto()
        {
            Assert.Null(RenderSize.SelectSize(new RenderSizeRequest(), Quest3Vdxr()));
            var fixedSize = new RenderSizeRequest { Mode = RenderSizeMode.Fixed, Fixed = E(2064, 2208) };
            Assert.Equal(E(2064, 2208), RenderSize.SelectSize(fixedSize, null));
            Assert.Equal(E(1032, 1104), RenderSize.SelectSize(fixedSize, new ViewLimits { MaxImageRect = E(1032, 4096) }));
            var automatic = new RenderSizeRequest { Mode = RenderSizeMode.Auto };
            Assert.Null(RenderSize.SelectSize(automatic, null));
            Assert.Equal(E(2056, 2216), RenderSize.SelectSize(automatic, Quest3Vdxr()));
            automatic.Scale = 0.8f;
            Assert.Equal(E(1648, 1776), RenderSize.SelectSize(automatic, Quest3Vdxr()));
        }

        [Fact]
        public void Describe()
        {
            Assert.Equal("off", RenderSize.Describe(new RenderSizeRequest()));
            Assert.Equal("auto x0.80", RenderSize.Describe(new RenderSizeRequest { Mode = RenderSizeMode.Auto, Scale = 0.8f }));
            Assert.Equal("2064x2208", RenderSize.Describe(new RenderSizeRequest { Mode = RenderSizeMode.Fixed, Fixed = E(2064, 2208) }));
        }

        /// <summary>The probe's structs against openxr.h of SDK 1.1.63 (x64): sizes and the offsets that matter.</summary>
        [Fact]
        public void ProbeStructsMatchOpenXrHeader()
        {
            Assert.Equal(8, IntPtr.Size);
            Assert.Equal(272, Marshal.SizeOf<OpenXrProbe.XrApplicationInfo>());
            Assert.Equal(128, Off<OpenXrProbe.XrApplicationInfo>("applicationVersion"));
            Assert.Equal(132, Off<OpenXrProbe.XrApplicationInfo>("engineName"));
            Assert.Equal(260, Off<OpenXrProbe.XrApplicationInfo>("engineVersion"));
            Assert.Equal(264, Off<OpenXrProbe.XrApplicationInfo>("apiVersion"));
            Assert.Equal(328, Marshal.SizeOf<OpenXrProbe.XrInstanceCreateInfo>());
            Assert.Equal(16, Off<OpenXrProbe.XrInstanceCreateInfo>("createFlags"));
            Assert.Equal(24, Off<OpenXrProbe.XrInstanceCreateInfo>("applicationInfo"));
            Assert.Equal(296, Off<OpenXrProbe.XrInstanceCreateInfo>("enabledApiLayerCount"));
            Assert.Equal(320, Off<OpenXrProbe.XrInstanceCreateInfo>("enabledExtensionNames"));
            Assert.Equal(152, Marshal.SizeOf<OpenXrProbe.XrInstanceProperties>());
            Assert.Equal(24, Marshal.SizeOf<OpenXrProbe.XrSystemGetInfo>());
            Assert.Equal(16, Off<OpenXrProbe.XrSystemGetInfo>("formFactor"));
            Assert.Equal(304, Marshal.SizeOf<OpenXrProbe.XrSystemProperties>());
            Assert.Equal(16, Off<OpenXrProbe.XrSystemProperties>("systemId"));
            Assert.Equal(28, Off<OpenXrProbe.XrSystemProperties>("systemName"));
            Assert.Equal(284, Off<OpenXrProbe.XrSystemProperties>("graphicsProperties"));
            Assert.Equal(296, Off<OpenXrProbe.XrSystemProperties>("trackingProperties"));
            Assert.Equal(40, Marshal.SizeOf<OpenXrProbe.XrViewConfigurationView>());
            Assert.Equal(16, Off<OpenXrProbe.XrViewConfigurationView>("recommendedImageRectWidth"));
            Assert.Equal(24, Off<OpenXrProbe.XrViewConfigurationView>("recommendedImageRectHeight"));
            Assert.Equal((1UL << 48) | 63UL, OpenXrProbe.ApiVersion1_0);
        }

        [Fact]
        public void ProbeWithoutTheLoaderFailsCleanly()
        {
            using (var dir = new TempDir())
            {
                var r = OpenXrProbe.Run(dir.Combine("openxr_loader.dll"), null);
                Assert.False(r.Ok);
                Assert.False(string.IsNullOrEmpty(r.Error));
            }
        }

        private static int Off<T>(string field) => Marshal.OffsetOf<T>(field).ToInt32();
    }
}
