using System;
using System.IO;
using System.Linq;
using System.Text;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>
    /// Stereo launches, pinned byte for byte: the plan as the launcher logs it (<see cref="LaunchPlan.Describe"/>: the
    /// command line, every variable, the window and the render size) for a few stereo settings, against
    /// <c>stereo-plans.txt</c>. The file was written by these cases on main before Mono got the render size (the
    /// mono-quality branch), so a change there that reaches a stereo launch fails here. A deliberate change to stereo
    /// launches rewrites the file: set EVR_WRITE_STEREO_GOLDEN to a path, run this test, and copy the file over.
    /// </summary>
    public class StereoPlanGoldenTests
    {
        private static LaunchInputs Inputs(LauncherSettings s) => new LaunchInputs
        {
            GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
            LayerDir = @"E:\EternalVR\layer",
            LogDir = @"E:\data\logs\20261120-150000",
            Settings = s,
            ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
        };

        private static OpenXrProbeResult Probe(uint w, uint h, uint maxImage, uint maxSwapchain) => new OpenXrProbeResult
        {
            RuntimeName = "test",
            SystemName = "test",
            Limits = new ViewLimits { Recommended = new Extent(w, h), MaxImageRect = new Extent(maxImage, maxImage), MaxSwapchain = new Extent(maxSwapchain, maxSwapchain) },
        };

        private static readonly DisplayArea[] Displays =
        {
            new DisplayArea("tv", 0, 0, 2560, 1440, true),
            new DisplayArea("vdd", 2560, 0, 3840, 2160, false, true),
        };

        /// <summary>Every case's plan, one block each.</summary>
        public static string Plans()
        {
            var cases = new (string Name, Func<LaunchInputs> Make)[]
            {
                ("defaults, no runtime answer", () => Inputs(new LauncherSettings())),
                ("defaults, VDXR's Quest 3 answer, a virtual display", () =>
                {
                    var i = Inputs(new LauncherSettings());
                    i.RuntimeProbe = Probe(2496, 2688, 4096, 16384);
                    i.Displays = Displays;
                    return i;
                }),
                ("render size off, the eye-sized window", () =>
                {
                    var i = Inputs(new LauncherSettings { RenderSize = "off" });
                    i.Displays = Displays;
                    return i;
                }),
                ("DLSS Quality on an RTX card, edge margin Medium, Godlike", () =>
                {
                    var i = Inputs(new LauncherSettings { AntiAliasing = AntiAliasingMode.Dlss, EdgeMargin = EdgeMarginMode.Medium });
                    i.RuntimeProbe = Probe(3072, 3264, 4096, 16384);
                    i.NvidiaRtx = true;
                    return i;
                }),
                ("anti-aliasing off, the simulator at x1.30 of what the runtime asks for, the mirror cropped", () =>
                {
                    var i = Inputs(new LauncherSettings { AntiAliasing = AntiAliasingMode.Off, RenderScale = 1.3, ResolutionBase = ResolutionBase.Ask, MirrorCrop = true });
                    i.RuntimeProbe = Probe(1280, 1400, 4096, 4096);
                    return i;
                }),
                ("a fixed render size, controllers off", () =>
                {
                    var i = Inputs(new LauncherSettings { RenderSize = "2064x2208", Controllers = false });
                    i.RuntimeProbe = Probe(1280, 1400, 4096, 4096);
                    return i;
                }),
            };
            var sb = new StringBuilder();
            foreach (var (name, make) in cases)
            {
                var plan = LaunchPlanBuilder.Build(make());
                sb.Append("## ").Append(name).Append('\n');
                sb.Append(plan.Describe().Replace("\r\n", "\n"));
                sb.Append("kept:    ").Append(plan.KeptKeys.Count == 0 ? "-" : string.Join(",", plan.KeptKeys)).Append('\n');
            }
            return sb.ToString();
        }

        [Fact]
        public void StereoLaunchesAreAsBeforeMonoGotTheRenderSize()
        {
            var actual = Plans();
            var write = Environment.GetEnvironmentVariable("EVR_WRITE_STEREO_GOLDEN");
            if (!string.IsNullOrEmpty(write))
            {
                File.WriteAllText(write, actual);
                return;
            }
            var expected = TestData.Read("stereo-plans.txt").Replace("\r\n", "\n");
            var a = actual.Split('\n');
            var e = expected.Split('\n');
            for (int k = 0; k < Math.Min(a.Length, e.Length); k++)
                Assert.True(a[k] == e[k], "line " + (k + 1) + " differs:\n  expected: " + e[k] + "\n  actual:   " + a[k]);
            Assert.Equal(e.Length, a.Length);
            Assert.All(a.Where(l => l.StartsWith("env:     ETERNALVR_MODE=", StringComparison.Ordinal)), l => Assert.Equal("env:     ETERNALVR_MODE=stereo", l));
        }
    }
}
