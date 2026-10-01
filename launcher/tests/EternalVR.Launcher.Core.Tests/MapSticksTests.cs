using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>"Dossier map sticks" (Controls): the weapon hand's stick pans the map by default, or the other hand's.</summary>
    public class MapSticksTests
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
        public void TheWeaponHandPansByDefaultAndTheChoiceIsPassedExplicitly()
        {
            Assert.Equal(MapPanStick.Weapon, new LauncherSettings().MapSticks);
            Assert.Equal("weapon", Env(LaunchPlanBuilder.Build(Inputs()))["ETERNALVR_MAP_STICKS"]);
            Assert.Equal("other", Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { MapSticks = MapPanStick.Other })))["ETERNALVR_MAP_STICKS"]);
            var mono = new LauncherSettings { Mode = VrMode.Mono, MapSticks = MapPanStick.Other };
            Assert.Equal("other", Env(LaunchPlanBuilder.Build(Inputs(mono)))["ETERNALVR_MAP_STICKS"]);
        }

        [Fact]
        public void TheSettingRoundTripsAndAnOlderFileTakesTheWeaponHand()
        {
            foreach (var m in new[] { MapPanStick.Weapon, MapPanStick.Other })
                Assert.Equal(m, LauncherSettings.Parse(new LauncherSettings { MapSticks = m }.Serialize()).MapSticks);
            Assert.Contains("map_sticks = other", new LauncherSettings { MapSticks = MapPanStick.Other }.Serialize());
            Assert.Contains("map_sticks = weapon", new LauncherSettings().Serialize());
            // A file from before the key, or a value this version does not know: the weapon hand.
            Assert.Equal(MapPanStick.Weapon, LauncherSettings.Parse("schema_version = 2\ndossier = tap\n").MapSticks);
            Assert.Equal(MapPanStick.Weapon, LauncherSettings.Parse("schema_version = 2\nmap_sticks = left\n").MapSticks);
            Assert.Equal(MapPanStick.Other, LauncherSettings.Parse("schema_version = 2\nmap_sticks = Other\n").MapSticks);
            // A known key: not kept among the unknown ones, so it is not written twice.
            Assert.Empty(LauncherSettings.Parse("schema_version = 2\nmap_sticks = other\n").UnknownKeys);
            Assert.Equal(MapPanStick.Weapon, new LauncherSettings { MapSticks = MapPanStick.Other }.WithDefaults().MapSticks);
        }

        [Fact]
        public void ItNeedsTheControllers()
        {
            Assert.Null(SettingRules.WhyNot(Setting.DossierMapSticks, new LauncherSettings()));
            Assert.Null(SettingRules.WhyNot(Setting.DossierMapSticks, new LauncherSettings { Mode = VrMode.Mono }));
            Assert.Equal(SettingRules.NeedsControllers, SettingRules.WhyNot(Setting.DossierMapSticks, new LauncherSettings { Controllers = false }));
        }

        [Fact]
        public void TheRowShowsItsChoicesInEnumOrder()
        {
            var text = SettingTexts.For(Setting.DossierMapSticks);
            Assert.Equal("Dossier map sticks", text.Label);
            Assert.Equal(new[] { "Weapon hand pans (default)", "Other hand pans" }, text.Choices);
            Assert.DoesNotContain("\u2014", text.Tooltip);
            Assert.DoesNotContain("\u2013", text.Tooltip);
        }
    }
}
