using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>"Melee aim with" and "Equipment aim with": what aims melee, or the equipment launcher and the Flame Belch,
    /// under hand aim.</summary>
    public class ActionAimTests
    {
        private static LaunchInputs Inputs(LauncherSettings s = null) => new LaunchInputs
        {
            GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
            LayerDir = @"E:\EternalVR\layer",
            LogDir = @"E:\data\logs\20261002-010203",
            Settings = s ?? new LauncherSettings(),
            ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
        };

        private static Dictionary<string, string> Env(LaunchPlan p) => p.Environment.ToDictionary(e => e.Key, e => e.Value);

        [Fact]
        public void SameAsAimWithIsTheDefaultAndPassesNothing()
        {
            Assert.Equal(ActionAimMode.Same, new LauncherSettings().MeleeAim);
            Assert.Equal(ActionAimMode.Same, new LauncherSettings().EquipmentAim);
            var env = Env(LaunchPlanBuilder.Build(Inputs()));
            Assert.False(env.ContainsKey("ETERNALVR_MELEE_AIM"));
            Assert.False(env.ContainsKey("ETERNALVR_EQUIPMENT_AIM"));
        }

        [Fact]
        public void AChoiceIsPassedToTheLayer()
        {
            var s = new LauncherSettings { MeleeAim = ActionAimMode.Head, EquipmentAim = ActionAimMode.OffHand };
            var env = Env(LaunchPlanBuilder.Build(Inputs(s)));
            Assert.Equal("hand", env["ETERNALVR_AIM"]);
            Assert.Equal("head", env["ETERNALVR_MELEE_AIM"]);
            Assert.Equal("offhand", env["ETERNALVR_EQUIPMENT_AIM"]);
            s = new LauncherSettings { MeleeAim = ActionAimMode.OffHand };
            env = Env(LaunchPlanBuilder.Build(Inputs(s)));
            Assert.Equal("offhand", env["ETERNALVR_MELEE_AIM"]);
            Assert.False(env.ContainsKey("ETERNALVR_EQUIPMENT_AIM"));
        }

        [Fact]
        public void TheSettingsRoundTripAndAnOlderFileTakesTheDefault()
        {
            foreach (var mode in new[] { ActionAimMode.Same, ActionAimMode.Head, ActionAimMode.OffHand })
            {
                var back = LauncherSettings.Parse(new LauncherSettings { MeleeAim = mode, EquipmentAim = mode }.Serialize());
                Assert.Equal(mode, back.MeleeAim);
                Assert.Equal(mode, back.EquipmentAim);
            }
            var text = new LauncherSettings { MeleeAim = ActionAimMode.Head, EquipmentAim = ActionAimMode.OffHand }.Serialize();
            Assert.Contains("melee_aim = head", text);
            Assert.Contains("equipment_aim = offhand", text);
            Assert.Contains("melee_aim = same", new LauncherSettings().Serialize());
            Assert.Contains("schema_version = 2", text);
            // A file from before the keys, or a value this version does not know: the default.
            var old = LauncherSettings.Parse("schema_version = 2\naim = hand\nrevenant_aim = head\n");
            Assert.Equal(ActionAimMode.Same, old.MeleeAim);
            Assert.Equal(ActionAimMode.Same, old.EquipmentAim);
            Assert.Equal(ActionAimMode.Same, LauncherSettings.Parse("schema_version = 2\nmelee_aim = feet\n").MeleeAim);
            Assert.Equal(ActionAimMode.OffHand, LauncherSettings.Parse("schema_version = 2\nequipment_aim = OffHand\n").EquipmentAim);
            // Known keys: not kept among the unknown ones, so they are not written twice.
            Assert.Empty(LauncherSettings.Parse("schema_version = 2\nmelee_aim = head\nequipment_aim = head\n").UnknownKeys);
            // Reset to defaults goes back to the same as Aim with.
            var reset = new LauncherSettings { MeleeAim = ActionAimMode.Head, EquipmentAim = ActionAimMode.Head }.WithDefaults();
            Assert.Equal(ActionAimMode.Same, reset.MeleeAim);
            Assert.Equal(ActionAimMode.Same, reset.EquipmentAim);
        }

        [Fact]
        public void TheyApplyOnlyWhenTheWeaponHandAims()
        {
            foreach (var setting in new[] { Setting.MeleeAimWith, Setting.EquipmentAimWith })
            {
                Assert.Null(SettingRules.WhyNot(setting, new LauncherSettings()));
                Assert.Null(SettingRules.WhyNot(setting, new LauncherSettings { Mode = VrMode.Mono }));
                Assert.Equal(SettingRules.NeedsHandAim, SettingRules.WhyNot(setting, new LauncherSettings { Aim = AimMode.Head }));
                Assert.Equal(SettingRules.NeedsHandAim, SettingRules.WhyNot(setting, new LauncherSettings { Aim = AimMode.View }));
                Assert.Equal(SettingRules.NeedsControllers, SettingRules.WhyNot(setting, new LauncherSettings { Controllers = false }));
            }
        }

        [Fact]
        public void TheRowsShowTheirChoicesInEnumOrder()
        {
            var melee = SettingTexts.For(Setting.MeleeAimWith);
            Assert.Equal("Melee aim with", melee.Label);
            Assert.Equal(new[] { "Same as Aim with", "Head", "Off hand" }, melee.Choices);
            Assert.Contains("glory kills", melee.Tooltip);
            var equipment = SettingTexts.For(Setting.EquipmentAimWith);
            Assert.Equal("Equipment aim with", equipment.Label);
            Assert.Equal(new[] { "Same as Aim with", "Head", "Off hand" }, equipment.Choices);
            Assert.Contains("Flame Belch", equipment.Tooltip);
        }
    }
}
