using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>"Weapon hand angle" and "Off hand angle" (Controls): each controller tilted about its grip, 0 by default.</summary>
    public class ControllerAngleTests
    {
        private static LaunchInputs Inputs(LauncherSettings s = null) => new LaunchInputs
        {
            GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
            LayerDir = @"E:\EternalVR\layer",
            LogDir = @"E:\data\logs\20261026-200000",
            Settings = s ?? new LauncherSettings(),
            ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
        };

        private static Dictionary<string, string> Env(LauncherSettings s = null) =>
            LaunchPlanBuilder.Build(Inputs(s)).Environment.ToDictionary(e => e.Key, e => e.Value);

        [Fact]
        public void BothAreZeroByDefaultAndPassedWithinTheLayersRange()
        {
            var env = Env();
            Assert.Equal("0.00", env["ETERNALVR_WEAPON_HAND_TILT"]);
            Assert.Equal("0.00", env["ETERNALVR_OFF_HAND_TILT"]);
            env = Env(new LauncherSettings { WeaponHandAngle = -20, OffHandAngle = -40 });
            Assert.Equal("-20.00", env["ETERNALVR_WEAPON_HAND_TILT"]);
            Assert.Equal("-40.00", env["ETERNALVR_OFF_HAND_TILT"]);
            env = Env(new LauncherSettings { WeaponHandAngle = 90, OffHandAngle = -90 });
            Assert.Equal("60.00", env["ETERNALVR_WEAPON_HAND_TILT"]);
            Assert.Equal("-60.00", env["ETERNALVR_OFF_HAND_TILT"]);
        }

        [Fact]
        public void TheyRoundTripAndAnOlderFileTakesZero()
        {
            var back = LauncherSettings.Parse(new LauncherSettings { WeaponHandAngle = -20, OffHandAngle = 15 }.Serialize());
            Assert.Equal(-20, back.WeaponHandAngle, 3);
            Assert.Equal(15, back.OffHandAngle, 3);
            Assert.Contains("weapon_hand_angle = -20.00", new LauncherSettings { WeaponHandAngle = -20 }.Serialize());
            var old = LauncherSettings.Parse("schema_version = 2\nhandedness = right\n");
            Assert.Equal(0, old.WeaponHandAngle);
            Assert.Equal(0, old.OffHandAngle);
            Assert.Equal(0, LauncherSettings.Parse("schema_version = 2\nweapon_hand_angle = down\n").WeaponHandAngle);
            Assert.Empty(LauncherSettings.Parse("schema_version = 2\nweapon_hand_angle = -20\noff_hand_angle = -40\n").UnknownKeys);
            Assert.Equal(0, new LauncherSettings { WeaponHandAngle = -20, OffHandAngle = -40 }.WithDefaults().OffHandAngle);
        }

        [Fact]
        public void TheyNeedTheControllersAndTheirTextsHaveNoDashes()
        {
            foreach (var setting in new[] { Setting.WeaponHandAngle, Setting.OffHandAngle })
            {
                Assert.Null(SettingRules.WhyNot(setting, new LauncherSettings()));
                Assert.Equal(SettingRules.NeedsControllers, SettingRules.WhyNot(setting, new LauncherSettings { Controllers = false }));
                var text = SettingTexts.For(setting);
                Assert.DoesNotContain("\u2014", text.Tooltip);
                Assert.DoesNotContain("\u2013", text.Tooltip);
            }
            Assert.Equal("Weapon hand angle", SettingTexts.For(Setting.WeaponHandAngle).Label);
            Assert.Equal("Off hand angle", SettingTexts.For(Setting.OffHandAngle).Label);
        }

        [Fact]
        public void TheirTextsSayTheyAreOnTopOfTheControllersFitAndIndexStartsAtZero()
        {
            // The layer gives Index controllers their own fit (-20 / -40 and a move), so the old advice to set
            // -20 / -40 here would now turn an Index player's hands twice.
            foreach (var setting in new[] { Setting.WeaponHandAngle, Setting.OffHandAngle })
            {
                var tooltip = SettingTexts.For(setting).Tooltip;
                Assert.Contains("on top of the mod's own fit for your controller", tooltip);
                Assert.Contains("start at 0", tooltip);
                Assert.DoesNotContain("-20", tooltip);
                Assert.DoesNotContain("-40", tooltip);
            }
            Assert.Contains("set back to 0 once", SettingTexts.For(Setting.WeaponHandAngle).Tooltip);
        }
    }
}
