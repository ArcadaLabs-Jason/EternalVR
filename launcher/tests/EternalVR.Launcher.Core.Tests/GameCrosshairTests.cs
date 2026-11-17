using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>
    /// The Game's crosshair (hand aim) setting (DECISIONS T-233): Hidden by default; On the gun passes the layer's
    /// ETERNALVR_UI_CROSSHAIR=gun, only with the aim dot on.
    /// </summary>
    public class GameCrosshairTests
    {
        private static LaunchInputs Inputs(LauncherSettings s) => new LaunchInputs
        {
            GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
            LayerDir = @"E:\EternalVR\layer",
            Settings = s,
            ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
        };

        private static Dictionary<string, string> Env(LauncherSettings s) =>
            LaunchPlanBuilder.Build(Inputs(s)).Environment.ToDictionary(e => e.Key, e => e.Value);

        [Fact]
        public void HiddenByDefaultAndKeptInTheFile()
        {
            Assert.Equal(GameCrosshairMode.Hidden, LauncherSettings.DefaultGameCrosshair);
            Assert.Equal(GameCrosshairMode.Hidden, new LauncherSettings().GameCrosshair);
            Assert.Contains("game_crosshair = hidden", new LauncherSettings().Serialize());
            Assert.Contains("game_crosshair = gun", new LauncherSettings { GameCrosshair = GameCrosshairMode.Gun }.Serialize());
            Assert.Equal(GameCrosshairMode.Gun,
                LauncherSettings.Parse(new LauncherSettings { GameCrosshair = GameCrosshairMode.Gun }.Serialize()).GameCrosshair);
            Assert.Equal(GameCrosshairMode.Gun, LauncherSettings.Parse("schema_version = 2\ngame_crosshair = Gun\n").GameCrosshair);
            // An older launcher's file, or a value this one does not know, is Hidden; the key is known.
            Assert.Equal(GameCrosshairMode.Hidden, LauncherSettings.Parse("schema_version = 2\n").GameCrosshair);
            Assert.Equal(GameCrosshairMode.Hidden, LauncherSettings.Parse("schema_version = 2\ngame_crosshair = huge\n").GameCrosshair);
            Assert.Empty(LauncherSettings.Parse("schema_version = 2\ngame_crosshair = gun\n").UnknownKeys);
            Assert.Equal(GameCrosshairMode.Hidden, new LauncherSettings { GameCrosshair = GameCrosshairMode.Gun }.WithDefaults().GameCrosshair);
        }

        [Fact]
        public void OnTheGunPassesGunOnlyWithTheAimDotOn()
        {
            // Hidden: nothing passed, the layer's own default (hold).
            Assert.False(Env(new LauncherSettings()).ContainsKey("ETERNALVR_UI_CROSSHAIR"));
            Assert.Equal("gun", Env(new LauncherSettings { GameCrosshair = GameCrosshairMode.Gun })["ETERNALVR_UI_CROSSHAIR"]);
            // The dot off: nothing in the middle, so the crosshair is not put on the gun either.
            var noDot = new LauncherSettings { GameCrosshair = GameCrosshairMode.Gun, AimDot = false };
            Assert.False(noDot.GameCrosshairOnGun);
            Assert.False(Env(noDot).ContainsKey("ETERNALVR_UI_CROSSHAIR"));
            Assert.Equal("0", Env(noDot)["ETERNALVR_UI_RETICLE"]);
            // The choice is kept with the dot off and back with it on.
            Assert.Equal(GameCrosshairMode.Gun, LauncherSettings.Parse(noDot.Serialize()).GameCrosshair);
        }

        [Fact]
        public void TheRowNeedsControllersHandAimAndTheDot()
        {
            var text = SettingTexts.For(Setting.GameCrosshair);
            Assert.Equal("Game's crosshair (hand aim)", text.Label);
            Assert.Equal(new[] { "Hidden", "On the gun" }, text.Choices.ToArray());
            Assert.Contains("aim dot", text.Tooltip);
            Assert.Contains("Lock-On Burst", text.Tooltip);
            Assert.Null(SettingRules.WhyNot(Setting.GameCrosshair, new LauncherSettings()));
            Assert.Null(SettingRules.WhyNot(Setting.GameCrosshair, new LauncherSettings { Mode = VrMode.Mono }));
            Assert.Equal(SettingRules.NeedsControllers, SettingRules.WhyNot(Setting.GameCrosshair, new LauncherSettings { Controllers = false }));
            Assert.Equal(SettingRules.NeedsHandAim, SettingRules.WhyNot(Setting.GameCrosshair, new LauncherSettings { Aim = AimMode.Head }));
            Assert.Equal(SettingRules.NeedsAimDot, SettingRules.WhyNot(Setting.GameCrosshair, new LauncherSettings { AimDot = false }));
            // Under the Aim dot and its size on the Play tab, before the picture rows.
            Assert.Equal((int)Setting.AimDotSize + 1, (int)Setting.GameCrosshair);
            Assert.True(Setting.GameCrosshair < Setting.Resolution);
        }
    }
}
