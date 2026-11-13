using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>"Depth for headset smoothing" on the Play tab's Picture group: each eye's depth handed to the headset's runtime
    /// (the layer's <c>ETERNALVR_DEPTH_LAYER</c>, src/vkcore/presenter_depth.hpp); off by default, experimental.</summary>
    public class DepthLayerTests
    {
        private const string Variable = "ETERNALVR_DEPTH_LAYER";

        private static LaunchInputs Inputs(LauncherSettings s = null, bool parallelEyesGame = true) => new LaunchInputs
        {
            GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
            LayerDir = @"E:\EternalVR\layer",
            LogDir = @"E:\data\logs\20261111-120000",
            Settings = s ?? new LauncherSettings(),
            ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
            ParallelEyesGame = parallelEyesGame,
        };

        private static Dictionary<string, string> Env(LauncherSettings s = null, bool parallelEyesGame = true) =>
            LaunchPlanBuilder.Build(Inputs(s, parallelEyesGame)).Environment.ToDictionary(e => e.Key, e => e.Value);

        [Fact]
        public void OffByDefaultAndPassedExplicitly()
        {
            Assert.False(new LauncherSettings().DepthLayer);
            Assert.False(SettingsPresets.Recommended.Values().DepthLayer);
            Assert.Equal("0", Env()[Variable]);
            Assert.Equal("1", Env(new LauncherSettings { DepthLayer = true })[Variable]);
        }

        [Fact]
        public void OnlyStereoPairsGetIt()
        {
            // Mono and Parallel Eye Rendering have no eye pairs the depth goes with: 0 whatever the box says.
            Assert.Equal("0", Env(new LauncherSettings { DepthLayer = true, Mode = VrMode.Mono })[Variable]);
            Assert.Equal("0", Env(new LauncherSettings { DepthLayer = true, ParallelEyes = true })[Variable]);
            // Alternate eyes' images (on or auto) pair eyes of different presents: 0, as the window greys it out.
            Assert.Equal("0", Env(new LauncherSettings { DepthLayer = true, AlternateEyes = AlternateEyesMode.Auto })[Variable]);
            Assert.Equal("0", Env(new LauncherSettings { DepthLayer = true, AlternateEyes = AlternateEyesMode.On })[Variable]);
            // The variable is 1 exactly where the window does not grey the box out, on a game build Parallel Eye
            // Rendering runs on and on one it does not (where its box changes nothing).
            foreach (var mode in new[] { VrMode.Stereo, VrMode.Mono })
                foreach (var alt in new[] { AlternateEyesMode.Off, AlternateEyesMode.Auto, AlternateEyesMode.On })
                    foreach (var parallel in new[] { false, true })
                        foreach (var game in new[] { true, false })
                        {
                            var s = new LauncherSettings { DepthLayer = true, Mode = mode, AlternateEyes = alt, ParallelEyes = parallel };
                            Assert.Equal(SettingRules.WhyNot(Setting.DepthLayer, s, game) == null ? "1" : "0", Env(s, game)[Variable]);
                        }
            // Parallel Eye Rendering ticked on a build it does not run on: the standard renderer, so the depth goes.
            Assert.Equal("1", Env(new LauncherSettings { DepthLayer = true, ParallelEyes = true }, parallelEyesGame: false)[Variable]);
        }

        [Fact]
        public void KeptInTheIniAndUnknownToNoOne()
        {
            Assert.Contains("depth_layer = 0", new LauncherSettings().Serialize());
            Assert.Contains("depth_layer = 1", new LauncherSettings { DepthLayer = true }.Serialize());
            Assert.True(LauncherSettings.Parse(new LauncherSettings { DepthLayer = true }.Serialize()).DepthLayer);
            Assert.False(LauncherSettings.Parse(new LauncherSettings().Serialize()).DepthLayer);
            // A file without the key (0.1.x launchers) takes the default: off.
            Assert.False(LauncherSettings.Parse("schema_version = 2\nframe_pacing = headset\n").DepthLayer);
            Assert.True(LauncherSettings.Parse("schema_version = 2\ndepth_layer = on\n").DepthLayer);
            Assert.Empty(LauncherSettings.Parse("schema_version = 2\ndepth_layer = 1\n").UnknownKeys);
            Assert.Equal(new[] { "depth_layer" }, SettingKeys.Of(Setting.DepthLayer));
        }

        [Fact]
        public void AppliesInStereoWithEyePairs()
        {
            Assert.Null(SettingRules.WhyNot(Setting.DepthLayer, new LauncherSettings()));
            Assert.Equal(SettingRules.NotWithAlternateEyes, SettingRules.WhyNot(Setting.DepthLayer, new LauncherSettings { AlternateEyes = AlternateEyesMode.Auto }));
            Assert.Equal(SettingRules.NeedsStereo, SettingRules.WhyNot(Setting.DepthLayer, new LauncherSettings { Mode = VrMode.Mono }));
            Assert.Equal(SettingRules.NotWithParallelEyes, SettingRules.WhyNot(Setting.DepthLayer, new LauncherSettings { ParallelEyes = true }));
            Assert.Equal(SettingRules.NotWithAlternateEyes, SettingRules.WhyNot(Setting.DepthLayer, new LauncherSettings { AlternateEyes = AlternateEyesMode.On }));
        }

        [Fact]
        public void ItIsAPictureRowWithAnHonestTooltip()
        {
            // In the Picture group, right after Frame pacing (SettingRules.Setting is in window order).
            Assert.Equal(Setting.FramePacing + 1, Setting.DepthLayer);
            var text = SettingTexts.For(Setting.DepthLayer);
            Assert.Contains("experimental", text.Label);
            Assert.Contains("Off by default", text.Tooltip);
            Assert.Contains("frame rate drops", text.Tooltip);
            Assert.Contains("edges", text.Tooltip);
            Assert.Contains("can warp while frames drop", text.Tooltip);
            Assert.Contains("not yet tried in a headset", text.Tooltip);
            Assert.DoesNotContain("\u2014", text.Tooltip);
        }

        [Fact]
        public void TheLayerReadsTheVariable()
        {
            Assert.Contains("L\"" + Variable + "\"", File.ReadAllText(RepoFile("src", "vkcore", "ui_vulkan.cpp")));
        }

        private static string RepoFile(params string[] parts)
        {
            for (var dir = new DirectoryInfo(AppContext.BaseDirectory); dir != null; dir = dir.Parent)
            {
                var path = Path.Combine(new[] { dir.FullName }.Concat(parts).ToArray());
                if (File.Exists(path)) return path;
            }
            throw new FileNotFoundException(string.Join("/", parts));
        }
    }
}
