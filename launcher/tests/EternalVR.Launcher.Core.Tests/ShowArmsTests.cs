using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>"Show arms": the Slayer's first-person arms drawn (default) or hidden, the weapon alone.</summary>
    public class ShowArmsTests
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
        public void ShownByDefaultAndPassedExplicitly()
        {
            Assert.True(new LauncherSettings().ShowArms);
            Assert.Equal("shown", Env(LaunchPlanBuilder.Build(Inputs()))["ETERNALVR_ARMS"]);
            Assert.Equal("hidden", Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { ShowArms = false })))["ETERNALVR_ARMS"]);
        }

        [Fact]
        public void TheSettingRoundTripsAndAnOlderFileShowsTheArms()
        {
            Assert.False(LauncherSettings.Parse(new LauncherSettings { ShowArms = false }.Serialize()).ShowArms);
            Assert.Contains("show_arms = 0", new LauncherSettings { ShowArms = false }.Serialize());
            Assert.Contains("show_arms = 1", new LauncherSettings().Serialize());
            Assert.True(LauncherSettings.Parse("schema_version = 2\nhandedness = left\n").ShowArms);
            Assert.Empty(LauncherSettings.Parse("schema_version = 2\nshow_arms = 0\n").UnknownKeys);
            Assert.True(new LauncherSettings { ShowArms = false }.WithDefaults().ShowArms);
        }

        [Fact]
        public void ItAppliesWithMotionControllersAndSitsUnderTheWeaponHand()
        {
            Assert.Null(SettingRules.WhyNot(Setting.Arms, new LauncherSettings()));
            Assert.Equal(SettingRules.WhyNot(Setting.WeaponHand, new LauncherSettings { Controllers = false }),
                SettingRules.WhyNot(Setting.Arms, new LauncherSettings { Controllers = false }));
            Assert.Equal((int)Setting.WeaponHand + 1, (int)Setting.Arms);
            var text = SettingTexts.For(Setting.Arms);
            Assert.Equal("Show arms", text.Label);
            Assert.DoesNotContain("\u2014", text.Tooltip);
        }
    }
}
