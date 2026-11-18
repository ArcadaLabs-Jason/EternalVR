using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>"Shared sun shadows (testing)" on the Advanced tab's View group: the right eye on the left eye's sun
    /// cascades of the tick (the layer's <c>ETERNALVR_SHARED_SUN</c>, src/vkcore/shared_sun_hooks.hpp); off by
    /// default.</summary>
    public class SharedSunTests
    {
        private const string Variable = "ETERNALVR_SHARED_SUN";

        private static LaunchInputs Inputs(LauncherSettings s = null, bool parallelEyesGame = true) => new LaunchInputs
        {
            GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
            LayerDir = @"E:\EternalVR\layer",
            LogDir = @"E:\data\logs\20261115-120000",
            Settings = s ?? new LauncherSettings(),
            ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
            ParallelEyesGame = parallelEyesGame,
        };

        private static Dictionary<string, string> Env(LauncherSettings s = null, bool parallelEyesGame = true) =>
            LaunchPlanBuilder.Build(Inputs(s, parallelEyesGame)).Environment.ToDictionary(e => e.Key, e => e.Value);

        [Fact]
        public void OffByDefaultAndAbsentWhenOff()
        {
            Assert.False(new LauncherSettings().SharedSun);
            Assert.False(SettingsPresets.Recommended.Values().SharedSun);
            Assert.False(Env().ContainsKey(Variable));
            Assert.Equal("1", Env(new LauncherSettings { SharedSun = true })[Variable]);
        }

        [Fact]
        public void PassedExactlyWhereTheRowApplies()
        {
            Assert.False(Env(new LauncherSettings { SharedSun = true, Mode = VrMode.Mono }).ContainsKey(Variable));
            Assert.False(Env(new LauncherSettings { SharedSun = true, ParallelEyes = true }).ContainsKey(Variable));
            Assert.False(Env(new LauncherSettings { SharedSun = true, AlternateEyes = AlternateEyesMode.Auto }).ContainsKey(Variable));
            Assert.False(Env(new LauncherSettings { SharedSun = true, AlternateEyes = AlternateEyesMode.On }).ContainsKey(Variable));
            // Set exactly where the window does not grey the box out, on a game build Parallel Eye Rendering runs on and on
            // one it does not (where its box changes nothing).
            foreach (var on in new[] { false, true })
                foreach (var mode in new[] { VrMode.Stereo, VrMode.Mono })
                    foreach (var alt in new[] { AlternateEyesMode.Off, AlternateEyesMode.Auto, AlternateEyesMode.On })
                        foreach (var parallel in new[] { false, true })
                            foreach (var game in new[] { true, false })
                            {
                                var s = new LauncherSettings { SharedSun = on, Mode = mode, AlternateEyes = alt, ParallelEyes = parallel };
                                var applies = SettingRules.WhyNot(Setting.SharedSun, s, game) == null;
                                Assert.Equal(on && applies, Env(s, game).ContainsKey(Variable));
                                Assert.Equal(on && applies, LauncherSettings.SharedSunOn(s, game));
                            }
            Assert.Equal("1", Env(new LauncherSettings { SharedSun = true, ParallelEyes = true }, parallelEyesGame: false)[Variable]);
        }

        [Fact]
        public void GreyedOutWithAReason()
        {
            Assert.Null(SettingRules.WhyNot(Setting.SharedSun, new LauncherSettings()));
            Assert.Equal(SettingRules.NeedsStereo, SettingRules.WhyNot(Setting.SharedSun, new LauncherSettings { Mode = VrMode.Mono }));
            Assert.Equal(SettingRules.NotWithParallelEyes, SettingRules.WhyNot(Setting.SharedSun, new LauncherSettings { ParallelEyes = true }));
            Assert.Equal(SettingRules.NotWithAlternateEyes, SettingRules.WhyNot(Setting.SharedSun, new LauncherSettings { AlternateEyes = AlternateEyesMode.Auto }));
            Assert.Equal(SettingRules.NotWithAlternateEyes, SettingRules.WhyNot(Setting.SharedSun, new LauncherSettings { AlternateEyes = AlternateEyesMode.On }));
            Assert.Null(SettingRules.WhyNot(Setting.SharedSun, new LauncherSettings { ParallelEyes = true }, parallelEyesGame: false));
        }

        [Fact]
        public void KeptInTheIni()
        {
            Assert.Contains("shared_sun = 0", new LauncherSettings().Serialize());
            Assert.Contains("shared_sun = 1", new LauncherSettings { SharedSun = true }.Serialize());
            Assert.True(LauncherSettings.Parse(new LauncherSettings { SharedSun = true }.Serialize()).SharedSun);
            Assert.False(LauncherSettings.Parse("schema_version = 2\nframe_pacing = headset\n").SharedSun);
            Assert.True(LauncherSettings.Parse("schema_version = 2\nshared_sun = on\n").SharedSun);
            Assert.Empty(LauncherSettings.Parse("schema_version = 2\nshared_sun = 1\n").UnknownKeys);
            Assert.Equal(new[] { "shared_sun" }, SettingKeys.Of(Setting.SharedSun));
        }

        [Fact]
        public void AnAdvancedViewRowMarkedForTesting()
        {
            // In the View group, right after Alternate eyes (SettingRules.Setting is in window order).
            Assert.Equal(Setting.AlternateEyes + 1, Setting.SharedSun);
            var text = SettingTexts.For(Setting.SharedSun);
            Assert.Contains("(testing)", text.Label);
            Assert.Contains("Off by default", text.Tooltip);
            Assert.Contains("not yet tried in a headset", text.Tooltip);
            Assert.DoesNotContain("\u2014", text.Tooltip);
        }

        [Fact]
        public void TheLayerReadsTheVariable()
        {
            Assert.Contains("L\"" + Variable + "\"", File.ReadAllText(RepoFile("src", "vkcore", "shared_sun_hooks.cpp")));
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
