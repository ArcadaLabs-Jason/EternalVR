using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>"Wait for a late frame (testing)" on the Play tab under Frame pacing: held, a headset frame waits for the
    /// image the game is still drawing (the layer's <c>ETERNALVR_PACE_LATE_WAIT</c>, src/vkcore/frame_pacing.cpp); off by
    /// default.</summary>
    public class LateWaitTests
    {
        private const string Variable = "ETERNALVR_PACE_LATE_WAIT";

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
            Assert.False(new LauncherSettings().LateWait);
            Assert.False(SettingsPresets.Recommended.Values().LateWait);
            Assert.False(Env().ContainsKey(Variable));
            Assert.Equal("1", Env(new LauncherSettings { LateWait = true })[Variable]);
            Assert.Equal("headset", Env(new LauncherSettings { LateWait = true })["ETERNALVR_PACE"]);
        }

        [Fact]
        public void PassedExactlyWhereTheRowApplies()
        {
            Assert.False(Env(new LauncherSettings { LateWait = true, Mode = VrMode.Mono }).ContainsKey(Variable));
            Assert.False(Env(new LauncherSettings { LateWait = true, Pacing = FramePacing.Off }).ContainsKey(Variable));
            Assert.False(Env(new LauncherSettings { LateWait = true, Pacing = FramePacing.NewFrames }).ContainsKey(Variable));
            Assert.False(Env(new LauncherSettings { LateWait = true, ParallelEyes = true }).ContainsKey(Variable));
            Assert.False(Env(new LauncherSettings { LateWait = true, AlternateEyes = AlternateEyesMode.Auto }).ContainsKey(Variable));
            Assert.False(Env(new LauncherSettings { LateWait = true, AlternateEyes = AlternateEyesMode.On }).ContainsKey(Variable));
            // Set exactly where the window does not grey the box out, on a game build Parallel Eye Rendering runs on and on
            // one it does not (where its box changes nothing).
            foreach (var on in new[] { false, true })
                foreach (var mode in new[] { VrMode.Stereo, VrMode.Mono })
                    foreach (var pacing in new[] { FramePacing.Off, FramePacing.Headset, FramePacing.NewFrames })
                        foreach (var alt in new[] { AlternateEyesMode.Off, AlternateEyesMode.Auto, AlternateEyesMode.On })
                            foreach (var parallel in new[] { false, true })
                                foreach (var game in new[] { true, false })
                                {
                                    var s = new LauncherSettings { LateWait = on, Mode = mode, Pacing = pacing, AlternateEyes = alt, ParallelEyes = parallel };
                                    var applies = SettingRules.WhyNot(Setting.LateWait, s, game) == null;
                                    Assert.Equal(on && applies, Env(s, game).ContainsKey(Variable));
                                    Assert.Equal(on && applies, LauncherSettings.LateWaitOn(s, game));
                                    // Only ever beside the matched pacing the layer waits under.
                                    if (on && applies) Assert.Equal("headset", Env(s, game)["ETERNALVR_PACE"]);
                                }
        }

        [Fact]
        public void GreyedOutWithAReason()
        {
            Assert.Null(SettingRules.WhyNot(Setting.LateWait, new LauncherSettings()));
            Assert.Equal(SettingRules.NeedsStereo, SettingRules.WhyNot(Setting.LateWait, new LauncherSettings { Mode = VrMode.Mono }));
            Assert.Equal(SettingRules.NeedsMatchedPacing, SettingRules.WhyNot(Setting.LateWait, new LauncherSettings { Pacing = FramePacing.Off }));
            Assert.Equal(SettingRules.NeedsMatchedPacing, SettingRules.WhyNot(Setting.LateWait, new LauncherSettings { Pacing = FramePacing.NewFrames }));
            Assert.Equal(SettingRules.NotWithParallelEyes, SettingRules.WhyNot(Setting.LateWait, new LauncherSettings { ParallelEyes = true }));
            Assert.Equal(SettingRules.NotWithAlternateEyes, SettingRules.WhyNot(Setting.LateWait, new LauncherSettings { AlternateEyes = AlternateEyesMode.Auto }));
            Assert.Equal(SettingRules.NotWithAlternateEyes, SettingRules.WhyNot(Setting.LateWait, new LauncherSettings { AlternateEyes = AlternateEyesMode.On }));
            Assert.Null(SettingRules.WhyNot(Setting.LateWait, new LauncherSettings { ParallelEyes = true }, parallelEyesGame: false));
        }

        [Fact]
        public void KeptInTheIni()
        {
            Assert.Contains("late_wait = 0", new LauncherSettings().Serialize());
            Assert.Contains("late_wait = 1", new LauncherSettings { LateWait = true }.Serialize());
            Assert.True(LauncherSettings.Parse(new LauncherSettings { LateWait = true }.Serialize()).LateWait);
            Assert.False(LauncherSettings.Parse("schema_version = 2\nframe_pacing = headset\n").LateWait);
            Assert.True(LauncherSettings.Parse("schema_version = 2\nlate_wait = on\n").LateWait);
            Assert.Empty(LauncherSettings.Parse("schema_version = 2\nlate_wait = 1\n").UnknownKeys);
            Assert.Equal(new[] { "late_wait" }, SettingKeys.Of(Setting.LateWait));
        }

        [Fact]
        public void APictureRowUnderFramePacingMarkedForTesting()
        {
            // In the Picture group, right after Frame pacing (SettingRules.Setting is in window order).
            Assert.Equal(Setting.FramePacing + 1, Setting.LateWait);
            var text = SettingTexts.For(Setting.LateWait);
            Assert.Contains("(testing)", text.Label);
            Assert.Contains("Off by default", text.Tooltip);
            Assert.Contains("not yet tried in a headset", text.Tooltip);
            Assert.Contains("graphics card is the limit", text.Tooltip);
            Assert.DoesNotContain("\u2014", text.Tooltip);
        }

        [Fact]
        public void TheLayerReadsTheVariable()
        {
            Assert.Contains("L\"" + Variable + "\"", File.ReadAllText(RepoFile("src", "vkcore", "frame_pacing.cpp")));
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
