using EternalVR.Launcher.Core.Data;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using System.Collections.Generic;
using System.Linq;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    public class HudPlacesTests
    {
        private static LaunchInputs Inputs(LauncherSettings s) => new LaunchInputs
        {
            GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
            LayerDir = @"E:\EternalVR\layer",
            LogDir = @"E:\data\logs\20260926-010203",
            Settings = s,
            ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
        };

        private static Dictionary<string, string> Env(LaunchPlan p) => p.Environment.ToDictionary(e => e.Key, e => e.Value);

        [Fact]
        public void TheHudPlacesAreSwitchesOfTheirOwn()
        {
            // The default is today's: the whole HUD in front, no hand HUD.
            var d = new LauncherSettings();
            Assert.True(d.HudFront);
            Assert.False(d.HudWrist);
            Assert.False(d.HudWeapon);
            var env = Env(LaunchPlanBuilder.Build(Inputs(d)));
            Assert.Equal("panel", env["ETERNALVR_HUD"]);
            Assert.Equal("1", env["ETERNALVR_HUD_FRONT"]);
            // Each switch on its own reaches the layer.
            env = Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { HudFront = false, HudWrist = true })));
            Assert.Equal("wrist", env["ETERNALVR_HUD"]);
            Assert.Equal("0", env["ETERNALVR_HUD_FRONT"]);
            env = Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { HudWrist = true })));
            Assert.Equal("wrist", env["ETERNALVR_HUD"]);
            Assert.Equal("1", env["ETERNALVR_HUD_FRONT"]); // both on: both show
            env = Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { HudWeapon = true })));
            Assert.Equal("weapon", env["ETERNALVR_HUD"]);
            Assert.Equal("1", env["ETERNALVR_HUD_FRONT"]);
            // Front off and nothing else is the readouts hidden: the player's choice.
            Assert.Equal("0", Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { HudFront = false })))["ETERNALVR_HUD_FRONT"]);
            // No controllers, no wrist: the panel keeps the readouts whatever the switch says.
            Assert.Equal("1", Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { HudFront = false, HudWrist = true, Controllers = false })))["ETERNALVR_HUD_FRONT"]);
            // They survive a save and a read.
            var all = new LauncherSettings { HudFront = false, HudWrist = true };
            var back = LauncherSettings.Parse(all.Serialize());
            Assert.False(back.HudFront);
            Assert.True(back.HudWrist);
            Assert.False(back.HudWeapon);
            Assert.True(LauncherSettings.Parse(new LauncherSettings { HudWeapon = true }.Serialize()).HudWeapon);
            // A file from before the keys: the default.
            var plain = LauncherSettings.Parse("schema_version = 2\nworld_scale = 1.10\n");
            Assert.True(plain.HudFront);
            Assert.False(plain.HudWrist);
            Assert.False(plain.HudWeapon);
        }

        [Fact]
        public void TheRowsSayWhenTheyDoNotApply()
        {
            Assert.Null(SettingRules.WhyNot(Setting.HudFront, new LauncherSettings()));
            Assert.Null(SettingRules.WhyNot(Setting.HudWrist, new LauncherSettings()));
            Assert.Null(SettingRules.WhyNot(Setting.HudWeapon, new LauncherSettings()));
            // One hand HUD at a time: the weapon row is greyed out while the wrist is on.
            Assert.Equal(SettingRules.NotWithWristHud, SettingRules.WhyNot(Setting.HudWeapon, new LauncherSettings { HudWrist = true }));
            // A weapon tick left under the greyed row is saved as it is read back: off.
            Assert.Contains("hud_weapon = 0", new LauncherSettings { HudWrist = true, HudWeapon = true }.Serialize());
            var noHands = new LauncherSettings { Controllers = false };
            Assert.Equal(SettingRules.NeedsControllers, SettingRules.WhyNot(Setting.HudWrist, noHands));
            Assert.Equal(SettingRules.NeedsControllers, SettingRules.WhyNot(Setting.HudWeapon, noHands));
            Assert.Null(SettingRules.WhyNot(Setting.HudFront, noHands));
            // The wrist row says how to look at it.
            Assert.Contains("palm up", SettingTexts.For(Setting.HudWrist).Tooltip);
        }

        [Fact]
        public void AnOlderLauncherStillFindsTheHandHud()
        {
            // Saved, the file holds the 0.1.88 choice nearest the switches beside them, so a downgrade keeps the hand HUD.
            Assert.Equal("wrist", LauncherSettings.ReadMap(new LauncherSettings { HudFront = false, HudWrist = true }.Serialize())["hud"]);
            Assert.Equal("wrist", LauncherSettings.ReadMap(new LauncherSettings { HudWrist = true }.Serialize())["hud"]);
            Assert.Equal("weapon", LauncherSettings.ReadMap(new LauncherSettings { HudFront = false, HudWeapon = true }.Serialize())["hud"]);
            Assert.Equal("panel", LauncherSettings.ReadMap(new LauncherSettings().Serialize())["hud"]);
            Assert.Equal("panel", LauncherSettings.ReadMap(new LauncherSettings { HudFront = false }.Serialize())["hud"]);
            // This launcher reads its switches, not that line: both on stays both on.
            var both = LauncherSettings.Parse(new LauncherSettings { HudWrist = true }.Serialize());
            Assert.True(both.HudFront);
            Assert.True(both.HudWrist);
            // Not a setting of its own: a profile neither counts it as a change nor writes it.
            var recommended = new LauncherSettings();
            var wrist = new LauncherSettings { HudWrist = true };
            Assert.DoesNotContain("hud", ProfileEdits.Changed(wrist, recommended));
            Assert.Contains("hud_wrist", ProfileEdits.Changed(wrist, recommended));
            Assert.DoesNotContain("hud = ", ProfileEdits.ProfileText(wrist));
            Assert.True(ProfileEdits.ResetAll(wrist, recommended).HudFront);
            Assert.False(ProfileEdits.ResetAll(wrist, recommended).HudWrist);
        }

        [Fact]
        public void AnOlderHudChoiceKeepsWhatThePlayerHad()
        {
            // Up to 0.1.88 one choice: the whole HUD in front (panel), the blocks on the wrist instead of in front, or the ammo on the weapon.
            var panel = LauncherSettings.Parse("hud = panel\n");
            Assert.True(panel.HudFront);
            Assert.False(panel.HudWrist);
            Assert.False(panel.HudWeapon);
            var wrist = LauncherSettings.Parse("schema_version = 2\nhud = wrist\n");
            Assert.False(wrist.HudFront);
            Assert.True(wrist.HudWrist);
            Assert.False(wrist.HudWeapon);
            var weapon = LauncherSettings.Parse("hud = Weapon");
            Assert.False(weapon.HudFront);
            Assert.False(weapon.HudWrist);
            Assert.True(weapon.HudWeapon);
            // A value this version does not know is the default.
            var elbow = LauncherSettings.Parse("hud = elbow");
            Assert.True(elbow.HudFront);
            Assert.False(elbow.HudWrist);
            // Saved again, the new keys say the same.
            var saved = wrist.Serialize();
            Assert.Contains("hud_front = 0", saved);
            Assert.Contains("hud_wrist = 1", saved);
            var again = LauncherSettings.Parse(saved);
            Assert.False(again.HudFront);
            Assert.True(again.HudWrist);
            // The new keys win over a leftover old one.
            var both = LauncherSettings.Parse("hud = wrist\nhud_front = 1\nhud_wrist = 0\n");
            Assert.True(both.HudFront);
            Assert.False(both.HudWrist);
            // The plan of an old file's wrist choice: the wrist, and nothing in front.
            var env = Env(LaunchPlanBuilder.Build(Inputs(wrist)));
            Assert.Equal("wrist", env["ETERNALVR_HUD"]);
            Assert.Equal("0", env["ETERNALVR_HUD_FRONT"]);
        }
    }
}
