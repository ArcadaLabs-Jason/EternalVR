using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>"Frame pacing" (Picture): one pair per headset frame, stereo only and not with adaptive alternate eyes, off by
    /// default.</summary>
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
        public void OffByDefaultAndPassedExplicitly()
        {
            Assert.Equal(FramePacing.Off, new LauncherSettings().Pacing);
            Assert.Equal("off", Env(LaunchPlanBuilder.Build(Inputs()))["ETERNALVR_PACE"]);
            var env = Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { Pacing = FramePacing.Headset })));
            Assert.Equal("stereo", env["ETERNALVR_MODE"]);
            Assert.Equal("headset", env["ETERNALVR_PACE"]);
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
        public void TheSettingRoundTripsAndAnOlderFileTakesOff()
        {
            foreach (var p in new[] { FramePacing.Off, FramePacing.Headset })
                Assert.Equal(p, LauncherSettings.Parse(new LauncherSettings { Pacing = p }.Serialize()).Pacing);
            Assert.Contains("pace = headset", new LauncherSettings { Pacing = FramePacing.Headset }.Serialize());
            Assert.Contains("pace = off", new LauncherSettings().Serialize());
            // A file from before the key, or a value this version does not know: off.
            Assert.Equal(FramePacing.Off, LauncherSettings.Parse("schema_version = 2\nfoveation = subtle\n").Pacing);
            Assert.Equal(FramePacing.Off, LauncherSettings.Parse("schema_version = 2\npace = 90\n").Pacing);
            Assert.Equal(FramePacing.Headset, LauncherSettings.Parse("schema_version = 2\npace = Headset\n").Pacing);
            // A known key: not kept among the unknown ones, so it is not written twice.
            Assert.Empty(LauncherSettings.Parse("schema_version = 2\npace = headset\n").UnknownKeys);
            // Reset to defaults turns it off.
            Assert.Equal(FramePacing.Off, new LauncherSettings { Pacing = FramePacing.Headset }.WithDefaults().Pacing);
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
            Assert.Equal(new[] { "As fast as the game runs", "Matched to the headset (experimental)" }, text.Choices);
            Assert.DoesNotContain("\u2014", text.Tooltip);
            Assert.DoesNotContain("\u2013", text.Tooltip);
        }
    }
}
