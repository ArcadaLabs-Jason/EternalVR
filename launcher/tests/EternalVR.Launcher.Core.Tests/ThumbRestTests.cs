using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>"Thumb-rest wheel" and its rows (Controls): off and the game's wheel by default, face-button touch
    /// off, the slowdown on.</summary>
    public class ThumbRestTests
    {
        private static LaunchInputs Inputs(LauncherSettings s = null) => new LaunchInputs
        {
            GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
            LayerDir = @"E:\EternalVR\layer",
            LogDir = @"E:\data\logs\20261008-140000",
            Settings = s ?? new LauncherSettings(),
            ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
        };

        private static Dictionary<string, string> Env(LaunchPlan p) => p.Environment.ToDictionary(e => e.Key, e => e.Value);

        [Fact]
        public void TheDefaultsReachTheLayerExplicitly()
        {
            var s = new LauncherSettings();
            Assert.Equal(ThumbRestMode.Off, s.ThumbRest);
            Assert.Equal(ThumbRestPick.Wheel, s.ThumbRestPicks);
            Assert.False(s.ThumbRestFaceTouch);
            Assert.True(s.ThumbRestSlowdown);
            var env = Env(LaunchPlanBuilder.Build(Inputs()));
            Assert.Equal("off", env["ETERNALVR_THUMBREST_WHEEL"]);
            Assert.Equal("wheel", env["ETERNALVR_THUMBREST_PICK"]);
            Assert.Equal("0", env["ETERNALVR_THUMBREST_FACE_TOUCH"]);
            Assert.Equal("1", env["ETERNALVR_THUMBREST_SLOWDOWN"]);
            Assert.False(env.ContainsKey("ETERNALVR_WEAPON_DIRECTIONS"));
        }

        [Fact]
        public void EveryChoiceReachesTheLayer()
        {
            var s = new LauncherSettings
            {
                ThumbRest = ThumbRestMode.Full, ThumbRestPicks = ThumbRestPick.Directions, ThumbRestFaceTouch = true, ThumbRestSlowdown = false,
                WeaponDirections = " up=4,down=none ",
            };
            var env = Env(LaunchPlanBuilder.Build(Inputs(s)));
            Assert.Equal("full", env["ETERNALVR_THUMBREST_WHEEL"]);
            // The turn-stick mode (room scale), offered again since the owner's spec of 2026-10-10.
            Assert.Equal(ThumbRestMode.Extreme, LauncherSettings.Parse("schema_version = 2\nthumb_rest_wheel = extreme\n").ThumbRest);
            Assert.Equal("extreme", Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { ThumbRest = ThumbRestMode.Extreme })))["ETERNALVR_THUMBREST_WHEEL"]);
            Assert.Equal("slots", env["ETERNALVR_THUMBREST_PICK"]);
            Assert.Equal("1", env["ETERNALVR_THUMBREST_FACE_TOUCH"]);
            Assert.Equal("0", env["ETERNALVR_THUMBREST_SLOWDOWN"]);
            Assert.Equal("up=4,down=none", env["ETERNALVR_WEAPON_DIRECTIONS"]);
            Assert.Equal("full", Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { ThumbRest = ThumbRestMode.Full })))["ETERNALVR_THUMBREST_WHEEL"]);
            Assert.Equal("edge", Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { ThumbRest = ThumbRestMode.Edge })))["ETERNALVR_THUMBREST_WHEEL"]);
        }

        [Fact]
        public void TheSettingsRoundTripAndAnOlderFileTakesTheDefaults()
        {
            foreach (ThumbRestMode m in System.Enum.GetValues(typeof(ThumbRestMode)))
                Assert.Equal(m, LauncherSettings.Parse(new LauncherSettings { ThumbRest = m }.Serialize()).ThumbRest);
            var set = new LauncherSettings
            {
                ThumbRestPicks = ThumbRestPick.Directions, ThumbRestFaceTouch = true, ThumbRestSlowdown = false, WeaponDirections = "up=2",
            };
            var back = LauncherSettings.Parse(set.Serialize());
            Assert.Equal(ThumbRestPick.Directions, back.ThumbRestPicks);
            Assert.True(back.ThumbRestFaceTouch);
            Assert.False(back.ThumbRestSlowdown);
            Assert.Equal("up=2", back.WeaponDirections);
            Assert.Contains("thumb_rest_wheel = off", new LauncherSettings().Serialize());
            Assert.Contains("thumb_rest_pick = directions", set.Serialize());
            // The direction table is written only when set by hand.
            Assert.DoesNotContain("weapon_directions", new LauncherSettings().Serialize());
            // A file from before the keys: the wheel off, as for a new player.
            var old = LauncherSettings.Parse("schema_version = 2\nwheel_select = hand\n");
            Assert.Equal(ThumbRestMode.Off, old.ThumbRest);
            Assert.Equal(ThumbRestPick.Wheel, old.ThumbRestPicks);
            Assert.False(old.ThumbRestFaceTouch);
            Assert.True(old.ThumbRestSlowdown);
            // Values this version does not know take the defaults; known keys are not kept among the unknown ones.
            var odd = LauncherSettings.Parse("schema_version = 2\nthumb_rest_wheel = Always\nthumb_rest_pick = x\nthumb_rest_slowdown = maybe\n");
            Assert.Equal(ThumbRestMode.Off, odd.ThumbRest);
            Assert.Equal(ThumbRestPick.Wheel, odd.ThumbRestPicks);
            Assert.True(odd.ThumbRestSlowdown);
            Assert.Equal(ThumbRestMode.Full, LauncherSettings.Parse("schema_version = 2\nthumb_rest_wheel = FULL\n").ThumbRest);
            Assert.Empty(LauncherSettings.Parse("schema_version = 2\nthumb_rest_wheel = off\nweapon_directions = up=1\n").UnknownKeys);
            Assert.Equal(ThumbRestMode.Off, new LauncherSettings { ThumbRest = ThumbRestMode.Full }.WithDefaults().ThumbRest);
            // Reset to defaults keeps a table written by hand: the window cannot set it again.
            Assert.Equal("up=2", new LauncherSettings { WeaponDirections = "up=2" }.WithDefaults().WeaponDirections);
        }

        [Fact]
        public void TheRowsNeedTheControllersAndTheWheelOn()
        {
            foreach (var setting in new[] { Setting.ThumbRestWheel, Setting.ThumbRestPicks, Setting.ThumbRestFaceTouch, Setting.ThumbRestSlowdown })
            {
                Assert.Null(SettingRules.WhyNot(setting, new LauncherSettings { ThumbRest = ThumbRestMode.Full }));
                Assert.Equal(SettingRules.NeedsControllers, SettingRules.WhyNot(setting, new LauncherSettings { Controllers = false }));
            }
            var off = new LauncherSettings { ThumbRest = ThumbRestMode.Off };
            Assert.Null(SettingRules.WhyNot(Setting.ThumbRestWheel, off));
            foreach (var setting in new[] { Setting.ThumbRestPicks, Setting.ThumbRestFaceTouch, Setting.ThumbRestSlowdown })
                Assert.Equal(SettingRules.NeedsThumbRest, SettingRules.WhyNot(setting, off));
            var directions = new LauncherSettings { ThumbRest = ThumbRestMode.Full, ThumbRestPicks = ThumbRestPick.Directions };
            Assert.Equal(SettingRules.NeedsWheelPick, SettingRules.WhyNot(Setting.ThumbRestSlowdown, directions));
            Assert.Null(SettingRules.WhyNot(Setting.ThumbRestFaceTouch, directions));
        }

        [Fact]
        public void TheRowsShowTheirChoicesInEnumOrder()
        {
            var wheel = SettingTexts.For(Setting.ThumbRestWheel);
            Assert.Equal("Thumb-rest wheel", wheel.Label);
            Assert.Equal(new[] { "Touch, then push", "While touched", "Turn stick picks (standing)", "Off (default)" }, wheel.Choices);
            Assert.Contains("Quest and Rift", wheel.Tooltip);
            Assert.Equal(new[] { "Weapon wheel (default)", "Weapon by direction" }, SettingTexts.For(Setting.ThumbRestPicks).Choices);
            foreach (var setting in new[] { Setting.ThumbRestWheel, Setting.ThumbRestPicks, Setting.ThumbRestFaceTouch, Setting.ThumbRestSlowdown })
            {
                var text = SettingTexts.For(setting);
                Assert.DoesNotContain("\u2014", text.Tooltip);
                Assert.DoesNotContain("\u2013", text.Tooltip);
            }
        }
    }
}
