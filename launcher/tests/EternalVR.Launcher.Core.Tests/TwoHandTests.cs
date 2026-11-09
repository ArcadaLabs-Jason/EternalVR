using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>
    /// "Two-handed aim (testing)" on the Advanced tab (the layer's ETERNALVR_TWO_HAND): off by default, passed explicitly, and
    /// only while the layer poses the arms (the controllers on, the weapon hand aiming), which puts the off hand on its controller.
    /// </summary>
    public class TwoHandTests
    {
        private static LaunchInputs Inputs(LauncherSettings s = null) => new LaunchInputs
        {
            GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
            LayerDir = @"E:\EternalVR\layer",
            LogDir = @"E:\data\logs\20261024-120000",
            Settings = s ?? new LauncherSettings(),
            ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
        };

        private static Dictionary<string, string> Env(LauncherSettings s) =>
            LaunchPlanBuilder.Build(Inputs(s)).Environment.ToDictionary(e => e.Key, e => e.Value);

        [Fact]
        public void OffByDefaultAndPassedExplicitly()
        {
            Assert.False(new LauncherSettings().TwoHandAim);
            Assert.Equal("0", Env(new LauncherSettings())["ETERNALVR_TWO_HAND"]);
            Assert.Equal("1", Env(new LauncherSettings { TwoHandAim = true })["ETERNALVR_TWO_HAND"]);
            Assert.Equal("1", Env(new LauncherSettings { TwoHandAim = true, Arms = ArmsView.HandsOnly })["ETERNALVR_TWO_HAND"]);
        }

        [Theory]
        [InlineData(true, AimMode.Hand, ArmsView.Shown)]
        [InlineData(true, AimMode.Hand, ArmsView.Hidden)] // hidden arms: the layer still poses them
        [InlineData(true, AimMode.Head, ArmsView.Shown)]  // head aim: both arms are the game's
        [InlineData(true, AimMode.View, ArmsView.Shown)]
        [InlineData(false, AimMode.Hand, ArmsView.Shown)]
        public void ItReachesTheLayerOnlyWhileTheArmsArePosed(bool controllers, AimMode aim, ArmsView arms)
        {
            var s = new LauncherSettings { TwoHandAim = true, Controllers = controllers, Aim = aim, Arms = arms };
            var env = Env(s);
            Assert.Equal(s.ArmsPosed ? "1" : "0", env["ETERNALVR_TWO_HAND"]);
            Assert.Equal(s.ArmsPosed, env["ETERNALVR_OFFHAND"] == "free" && env["ETERNALVR_ARM_SOLVE"] == "bind");
            // The setting itself is kept for a session with the arms posed.
            Assert.True(LauncherSettings.Parse(s.Serialize()).TwoHandAim);
        }

        [Fact]
        public void TheSettingRoundTripsAndAnOlderFileKeepsItOff()
        {
            Assert.True(LauncherSettings.Parse(new LauncherSettings { TwoHandAim = true }.Serialize()).TwoHandAim);
            Assert.Contains("two_hand_aim = 1", new LauncherSettings { TwoHandAim = true }.Serialize());
            Assert.Contains("two_hand_aim = 0", new LauncherSettings().Serialize());
            Assert.False(LauncherSettings.Parse("schema_version = 2\nnew_arms = 1\n").TwoHandAim);
            Assert.True(LauncherSettings.Parse("schema_version = 2\ntwo_hand_aim = on\n").TwoHandAim);
            Assert.False(LauncherSettings.Parse("schema_version = 2\ntwo_hand_aim = maybe\n").TwoHandAim);
            Assert.Empty(LauncherSettings.Parse("schema_version = 2\ntwo_hand_aim = 1\n").UnknownKeys);
            Assert.False(new LauncherSettings { TwoHandAim = true }.WithDefaults().TwoHandAim);
            Assert.False(SettingsPresets.Recommended.Values().TwoHandAim);
        }

        [Fact]
        public void TheRowSaysWhatItNeeds()
        {
            Assert.Null(SettingRules.WhyNot(Setting.TwoHandAim, new LauncherSettings()));
            Assert.Null(SettingRules.WhyNot(Setting.TwoHandAim, new LauncherSettings { Arms = ArmsView.HandsOnly }));
            Assert.Equal(SettingRules.NeedsControllers,
                SettingRules.WhyNot(Setting.TwoHandAim, new LauncherSettings { Controllers = false }));
            Assert.Equal(SettingRules.NeedsHandAim,
                SettingRules.WhyNot(Setting.TwoHandAim, new LauncherSettings { Aim = AimMode.Head }));
            Assert.Equal(new[] { "two_hand_aim" }, SettingKeys.Of(Setting.TwoHandAim));
            var text = SettingTexts.For(Setting.TwoHandAim);
            Assert.DoesNotContain("New arms", text.Tooltip);
            Assert.Contains("not tried in a headset", text.Tooltip);
        }
    }
}
