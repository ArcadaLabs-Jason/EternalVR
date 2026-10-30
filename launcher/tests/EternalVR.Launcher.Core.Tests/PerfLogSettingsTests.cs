using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>The Advanced tab's "Performance in the log" option: its key, its variables and its words.</summary>
    public class PerfLogSettingsTests
    {
        private static Dictionary<string, string> Env(LauncherSettings s) =>
            LaunchPlanBuilder.Build(new LaunchInputs
            {
                GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
                LayerDir = @"E:\EternalVR\layer",
                Settings = s,
                ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
            }).Environment.ToDictionary(e => e.Key, e => e.Value);

        [Fact]
        public void OnByDefaultLeavesTheLayersOwnDefault()
        {
            Assert.True(new LauncherSettings().PerfLog);
            var env = Env(new LauncherSettings());
            Assert.False(env.ContainsKey("ETERNALVR_GPU_TIMING"));
            Assert.False(env.ContainsKey("ETERNALVR_CPU_TIMING"));
        }

        [Fact]
        public void OffTurnsBothTimingsOff()
        {
            var env = Env(new LauncherSettings { PerfLog = false });
            Assert.Equal("0", env["ETERNALVR_GPU_TIMING"]);
            Assert.Equal("0", env["ETERNALVR_CPU_TIMING"]);
        }

        [Fact]
        public void TheKeyRoundTrips()
        {
            var off = new LauncherSettings { PerfLog = false };
            Assert.Contains("perf_log = 0", off.Serialize());
            Assert.False(LauncherSettings.Parse(off.Serialize()).PerfLog);
            Assert.True(LauncherSettings.Parse("schema_version = 2\n").PerfLog);
            Assert.Equal(new[] { "perf_log" }, SettingKeys.Of(Setting.PerformanceLog));
            Assert.Equal("Performance in the log", SettingTexts.For(Setting.PerformanceLog).Label);
        }
    }
}
