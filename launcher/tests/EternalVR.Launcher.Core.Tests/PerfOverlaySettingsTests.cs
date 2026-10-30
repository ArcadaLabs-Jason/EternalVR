using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>The Advanced tab's "Performance overlay": the game's own Performance Metrics level on the command line.</summary>
    public class PerfOverlaySettingsTests
    {
        private static IReadOnlyList<string> Arguments(LauncherSettings s, VrMode mode = VrMode.Stereo)
        {
            s.Mode = mode;
            return LaunchPlanBuilder.Build(new LaunchInputs
            {
                GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
                LayerDir = @"E:\EternalVR\layer",
                Settings = s,
                ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
            }).Arguments;
        }

        private static Dictionary<string, string> Env(LauncherSettings s) =>
            LaunchPlanBuilder.Build(new LaunchInputs
            {
                GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
                LayerDir = @"E:\EternalVR\layer",
                Settings = s,
                ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
            }).Environment.ToDictionary(e => e.Key, e => e.Value);

        private static string Level(IReadOnlyList<string> args)
        {
            int i = args.ToList().IndexOf("+com_showFPS");
            return i >= 0 && i + 1 < args.Count ? args[i + 1] : null;
        }

        [Fact]
        public void OffByDefaultPassesNothing()
        {
            Assert.Equal(PerformanceOverlay.Off, new LauncherSettings().PerfOverlay);
            Assert.DoesNotContain("+com_showFPS", Arguments(new LauncherSettings()));
            Assert.DoesNotContain("+com_showFPS", Arguments(new LauncherSettings(), VrMode.Mono));
            Assert.Equal(PerformanceOverlay.Off, SettingsPresets.Recommended.Values().PerfOverlay);
            Assert.Equal("0", Env(new LauncherSettings())["ETERNALVR_PERF_OVERLAY"]);
        }

        [Theory]
        [InlineData(PerformanceOverlay.FrameRate, "3")]
        [InlineData(PerformanceOverlay.Detailed, "2")]
        public void EachChoicePassesTheGamesLevel(PerformanceOverlay overlay, string level)
        {
            Assert.Equal(level, Level(Arguments(new LauncherSettings { PerfOverlay = overlay })));
            Assert.Equal(level, Level(Arguments(new LauncherSettings { PerfOverlay = overlay }, VrMode.Mono)));
            Assert.Single(Arguments(new LauncherSettings { PerfOverlay = overlay }), a => a == "+com_showFPS");
            // The layer shows the corner the game draws them in, at the same level.
            Assert.Equal(level, Env(new LauncherSettings { PerfOverlay = overlay })["ETERNALVR_PERF_OVERLAY"]);
            Assert.Equal(level, Env(new LauncherSettings { PerfOverlay = overlay, Mode = VrMode.Mono })["ETERNALVR_PERF_OVERLAY"]);
        }

        [Fact]
        public void ExtraArgumentsComeAfterIt()
        {
            var args = Arguments(new LauncherSettings { PerfOverlay = PerformanceOverlay.Detailed, ExtraArguments = "+com_showFPS 1" }).ToList();
            int overlay = args.IndexOf("+com_showFPS");
            int extra = args.LastIndexOf("+com_showFPS");
            Assert.True(overlay < extra);
            Assert.Equal("2", args[overlay + 1]);
            Assert.Equal("1", args[extra + 1]);
        }

        [Fact]
        public void TheKeyRoundTrips()
        {
            foreach (var overlay in new[] { PerformanceOverlay.Off, PerformanceOverlay.FrameRate, PerformanceOverlay.Detailed })
                Assert.Equal(overlay, LauncherSettings.Parse(new LauncherSettings { PerfOverlay = overlay }.Serialize()).PerfOverlay);
            Assert.Contains("perf_overlay = frame_rate", new LauncherSettings { PerfOverlay = PerformanceOverlay.FrameRate }.Serialize());
            Assert.Contains("perf_overlay = off", new LauncherSettings().Serialize());
            Assert.Equal(PerformanceOverlay.Detailed, LauncherSettings.Parse("schema_version = 2\nperf_overlay = DETAILED\n").PerfOverlay);
            // An older file, or a value this launcher does not know: off.
            Assert.Equal(PerformanceOverlay.Off, LauncherSettings.Parse("schema_version = 2\n").PerfOverlay);
            Assert.Equal(PerformanceOverlay.Off, LauncherSettings.Parse("schema_version = 2\nperf_overlay = ultra\n").PerfOverlay);
            Assert.DoesNotContain(LauncherSettings.Parse("schema_version = 2\nperf_overlay = detailed\n").UnknownKeys, kv => kv.Key == "perf_overlay");
            // Performance in the log is read beside it as before.
            Assert.False(LauncherSettings.Parse("schema_version = 2\nperf_log = 0\nperf_overlay = detailed\n").PerfLog);
        }

        [Fact]
        public void TheRowNamesItsKeyAndChoices()
        {
            Assert.Equal(new[] { "perf_overlay" }, SettingKeys.Of(Setting.PerformanceOverlay));
            var text = SettingTexts.For(Setting.PerformanceOverlay);
            Assert.Equal("Performance overlay", text.Label);
            Assert.Equal(new[] { "Off", "Frame rate", "Detailed" }, text.Choices);
            Assert.Null(SettingRules.WhyNot(Setting.PerformanceOverlay, new LauncherSettings { Mode = VrMode.Mono }));
        }
    }
}
