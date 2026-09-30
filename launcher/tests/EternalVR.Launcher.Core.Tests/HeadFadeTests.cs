using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>"Fade in walls": the view fades to black with the head in a wall or too far from the body (on by default).</summary>
    public class HeadFadeTests
    {
        private static LaunchInputs Inputs(LauncherSettings s = null) => new LaunchInputs
        {
            GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
            LayerDir = @"E:\EternalVR\layer",
            LogDir = @"E:\data\logs\20260930-120000",
            Settings = s ?? new LauncherSettings(),
            ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
        };

        private static Dictionary<string, string> Env(LaunchPlan p) => p.Environment.ToDictionary(e => e.Key, e => e.Value);

        [Fact]
        public void OnByDefaultAndPassedExplicitly()
        {
            Assert.True(new LauncherSettings().HeadFade);
            Assert.Equal("1", Env(LaunchPlanBuilder.Build(Inputs()))["ETERNALVR_HEAD_FADE"]);
            Assert.Equal("0", Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { HeadFade = false })))["ETERNALVR_HEAD_FADE"]);
        }

        [Fact]
        public void TheSettingRoundTripsAndAnOlderFileKeepsTheFade()
        {
            Assert.False(LauncherSettings.Parse(new LauncherSettings { HeadFade = false }.Serialize()).HeadFade);
            Assert.Contains("head_fade = 0", new LauncherSettings { HeadFade = false }.Serialize());
            Assert.Contains("head_fade = 1", new LauncherSettings().Serialize());
            Assert.True(LauncherSettings.Parse("schema_version = 2\nbody_follow = 0\n").HeadFade);
            Assert.False(LauncherSettings.Parse("schema_version = 2\nbody_follow = 0\n").BodyFollow);
            Assert.Empty(LauncherSettings.Parse("schema_version = 2\nhead_fade = 0\n").UnknownKeys);
            Assert.True(new LauncherSettings { HeadFade = false }.WithDefaults().HeadFade);
        }

        [Fact]
        public void ItAlwaysAppliesSeatedOrWithoutControllers()
        {
            Assert.Null(SettingRules.WhyNot(Setting.HeadFade, new LauncherSettings()));
            Assert.Null(SettingRules.WhyNot(Setting.HeadFade, new LauncherSettings { Controllers = false }));
            Assert.Null(SettingRules.WhyNot(Setting.HeadFade, new LauncherSettings { Posture = PostureMode.Seated }));
            var text = SettingTexts.For(Setting.HeadFade);
            Assert.Equal("Fade in walls", text.Label);
            Assert.DoesNotContain("\u2014", text.Tooltip);
        }
    }
}
