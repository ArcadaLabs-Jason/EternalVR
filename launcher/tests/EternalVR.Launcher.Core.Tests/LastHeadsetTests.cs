using System.IO;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>The headset's remembered sizes and the Play tab's "Each eye" line (<see cref="LastHeadset"/>).</summary>
    public class LastHeadsetTests
    {
        private static ViewLimits Headset(uint recW, uint recH) => new ViewLimits
        {
            Recommended = new Extent(recW, recH),
            MaxImageRect = new Extent(8192, 8192),
            MaxSwapchain = new Extent(16384, 16384),
        };

        private static string Line(ViewLimits last, double scale = 1.0, string renderSize = "auto") =>
            LastHeadset.EachEye(new LauncherSettings { RenderScale = scale, RenderSize = renderSize }, last);

        [Fact]
        public void WithoutARememberedSizeItIsSetAtPlay()
        {
            Assert.Equal("Set from your headset when you press Play", Line(null));
            Assert.Equal("Set from your headset when you press Play", Line(null, 1.5));
        }

        [Fact]
        public void Quest3ThroughVirtualDesktopHighIsBudgetedTo82Percent()
        {
            // 2496x2688 is over the 2064x2208 budget: scaled by sqrt(4557312 / 6709248) = 0.8242 to 2057.1 x 2215.4,
            // rounded to 2057x2215, then to multiples of 8.
            var quest = Headset(2496, 2688);
            Assert.Equal("2056x2216, 82% of the 2496x2688 your headset asked for", Line(quest));
            // The size the launch would start the game at.
            var launch = RenderSizeChoice.Decide("auto", 1.0, new OpenXrProbeResult { Limits = quest });
            Assert.Equal(new Extent(2056, 2216), launch.Size);
        }

        [Fact]
        public void TheLineFollowsTheRenderScale()
        {
            // 0.8242 x 1.2 = 0.9890: 2468.6 x 2658.4, rounded to 2469x2658, then to 2472x2656; 2472 / 2496 = 99%.
            Assert.Equal("2472x2656, 99% of the 2496x2688 your headset asked for", Line(Headset(2496, 2688), 1.2));
            // Lower: 0.8242 x 0.8 = 0.6593: 1645.7 x 1772.3, to 1646x1772, then to 1648x1776 (221.5 eighths rounds up);
            // 1648 / 2496 = 66%.
            Assert.Equal("1648x1776, 66% of the 2496x2688 your headset asked for", Line(Headset(2496, 2688), 0.8));
        }

        [Fact]
        public void ValveIndexIsWithinTheBudgetAt100Percent()
        {
            var index = Headset(2016, 2224);
            Assert.Equal("2016x2224, 100% of the 2016x2224 your headset asked for", Line(index));
            // Above the recommendation the percent passes 100.
            Assert.Equal("4032x4448, 200% of the 2016x2224 your headset asked for", Line(index, 2.0));
        }

        [Fact]
        public void AFixedSizeIsSaidAsSuch()
        {
            Assert.Equal("2064x2208 (fixed size)", Line(null, 1.0, "2064x2208"));
            Assert.Equal("2064x2208 (fixed size)", Line(Headset(2496, 2688), 1.5, "2064x2208"));
            // The layer rounds a fixed size to multiples of 8 too.
            Assert.Equal("2064x2104 (fixed size)", Line(null, 1.0, "2060x2100"));
        }

        [Fact]
        public void OffAndMonoFollowTheWindowOrTheGame()
        {
            Assert.Equal("The game window's size (render_size off)", Line(Headset(2496, 2688), 1.0, "off"));
            var mono = new LauncherSettings { Mode = VrMode.Mono };
            Assert.Equal("The game's own resolution (mono)", LastHeadset.EachEye(mono, Headset(2496, 2688)));
            Assert.Equal(SettingRules.NeedsStereo, SettingRules.WhyNot(Setting.EachEye, mono));
            Assert.Null(SettingRules.WhyNot(Setting.EachEye, new LauncherSettings()));
        }

        [Fact]
        public void TheSizesSurviveASaveAndLoad()
        {
            using (var dir = new TempDir())
            {
                var path = dir.Combine("headset.txt");
                Assert.Null(LastHeadset.Load(path));
                var limits = new ViewLimits
                {
                    Recommended = new Extent(2496, 2688),
                    MaxImageRect = new Extent(4096, 4096),
                    MaxSwapchain = new Extent(0, 0),
                };
                LastHeadset.Save(path, limits);
                var loaded = LastHeadset.Load(path);
                Assert.Equal(limits.Recommended, loaded.Recommended);
                Assert.Equal(limits.MaxImageRect, loaded.MaxImageRect);
                Assert.Equal(limits.MaxSwapchain, loaded.MaxSwapchain);
                Assert.Equal(1u, loaded.EyesSideBySide);
                Assert.Equal("2056x2216, 82% of the 2496x2688 your headset asked for", Line(loaded));

                // A file without a usable recommendation is no remembered size.
                File.WriteAllText(path, "recommended = big\nmax_image = 4096x4096\n");
                Assert.Null(LastHeadset.Load(path));
                File.WriteAllText(path, "recommended = 0x2688\n");
                Assert.Null(LastHeadset.Load(path));
            }
        }
    }
}
