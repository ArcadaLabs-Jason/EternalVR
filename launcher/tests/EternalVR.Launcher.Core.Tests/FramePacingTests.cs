using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>"Frame pacing" (Picture): one pair per headset frame, stereo only and not with adaptive alternate eyes, on
    /// by default.</summary>
    public class FramePacingTests
    {
        private static LaunchInputs Inputs(LauncherSettings s = null) => new LaunchInputs
        {
            GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
            LayerDir = @"E:\EternalVR\layer",
            LogDir = @"E:\data\logs\20260930-140000",
            Settings = s ?? new LauncherSettings(),
            ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
        };

        private static Dictionary<string, string> Env(LaunchPlan p) => p.Environment.ToDictionary(e => e.Key, e => e.Value);

        [Fact]
        public void MatchedByDefaultAndPassedExplicitly()
        {
            Assert.Equal(FramePacing.Headset, new LauncherSettings().Pacing);
            var env = Env(LaunchPlanBuilder.Build(Inputs()));
            Assert.Equal("stereo", env["ETERNALVR_MODE"]);
            Assert.Equal("headset", env["ETERNALVR_PACE"]);
            Assert.Equal("off", Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { Pacing = FramePacing.Off })))["ETERNALVR_PACE"]);
        }

        [Fact]
        public void MonoAndAdaptiveAlternateEyesAreNeverPaced()
        {
            var s = new LauncherSettings { Mode = VrMode.Mono, Pacing = FramePacing.Headset };
            Assert.Equal("off", Env(LaunchPlanBuilder.Build(Inputs(s)))["ETERNALVR_PACE"]);
            s = new LauncherSettings { AlternateEyes = AlternateEyesMode.Auto, Pacing = FramePacing.Headset };
            Assert.Equal("off", Env(LaunchPlanBuilder.Build(Inputs(s)))["ETERNALVR_PACE"]);
            // Alternate eyes always on decides nothing by the game's rate: paced as asked.
            s = new LauncherSettings { AlternateEyes = AlternateEyesMode.On, Pacing = FramePacing.Headset };
            Assert.Equal("headset", Env(LaunchPlanBuilder.Build(Inputs(s)))["ETERNALVR_PACE"]);
        }

        [Fact]
        public void TheSettingRoundTripsAndOldFilesTakeTheNewDefault()
        {
            foreach (var p in new[] { FramePacing.Off, FramePacing.Headset })
                Assert.Equal(p, LauncherSettings.Parse(new LauncherSettings { Pacing = p }.Serialize()).Pacing);
            Assert.Contains("frame_pacing = headset", new LauncherSettings().Serialize());
            Assert.Contains("frame_pacing = off", new LauncherSettings { Pacing = FramePacing.Off }.Serialize());
            Assert.DoesNotContain("\npace =", "\n" + new LauncherSettings().Serialize());
            // A file from before either key, or a value this version does not know: the default.
            Assert.Equal(FramePacing.Headset, LauncherSettings.Parse("schema_version = 2\nfoveation = subtle\n").Pacing);
            Assert.Equal(FramePacing.Headset, LauncherSettings.Parse("schema_version = 2\nframe_pacing = 90\n").Pacing);
            // Launcher 0.1.11 always wrote pace, off by default, so its 'off' is not a choice; its 'headset' is.
            Assert.Equal(FramePacing.Headset, LauncherSettings.Parse("schema_version = 2\npace = off\n").Pacing);
            Assert.Equal(FramePacing.Headset, LauncherSettings.Parse("schema_version = 2\npace = Headset\n").Pacing);
            // The new key wins over the old one.
            Assert.Equal(FramePacing.Off, LauncherSettings.Parse("schema_version = 2\npace = headset\nframe_pacing = off\n").Pacing);
            // Known keys: not kept among the unknown ones, so neither is written back as is.
            Assert.Empty(LauncherSettings.Parse("schema_version = 2\npace = off\nframe_pacing = off\n").UnknownKeys);
            // Reset to defaults matches the headset again.
            Assert.Equal(FramePacing.Headset, new LauncherSettings { Pacing = FramePacing.Off }.WithDefaults().Pacing);
        }

        [Fact]
        public void ItAppliesInStereoOnly()
        {
            Assert.Null(SettingRules.WhyNot(Setting.FramePacing, new LauncherSettings()));
            Assert.Equal(SettingRules.NeedsStereo, SettingRules.WhyNot(Setting.FramePacing, new LauncherSettings { Mode = VrMode.Mono }));
            Assert.Equal(SettingRules.NotWithAutoEyes,
                SettingRules.WhyNot(Setting.FramePacing, new LauncherSettings { AlternateEyes = AlternateEyesMode.Auto }));
            Assert.Null(SettingRules.WhyNot(Setting.FramePacing, new LauncherSettings { AlternateEyes = AlternateEyesMode.On }));
            var text = SettingTexts.For(Setting.FramePacing);
            Assert.Equal("Frame pacing", text.Label);
            Assert.Equal(new[] { "As fast as the game runs", "Matched to the headset (default)" }, text.Choices);
            Assert.DoesNotContain("\u2014", text.Tooltip);
            Assert.DoesNotContain("\u2013", text.Tooltip);
        }
    }
}
