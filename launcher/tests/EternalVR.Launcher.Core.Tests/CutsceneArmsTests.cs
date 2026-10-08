using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>"Arms in cutscenes": the first-person arms in cutscenes around you, hidden (default) or drawn as the game has them.</summary>
    public class CutsceneArmsTests
    {
        private static LaunchInputs Inputs(LauncherSettings s = null) => new LaunchInputs
        {
            GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
            LayerDir = @"E:\EternalVR\layer",
            LogDir = @"E:\data\logs\20261007-120000",
            Settings = s ?? new LauncherSettings(),
            ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
        };

        private static Dictionary<string, string> Env(LaunchPlan p) => p.Environment.ToDictionary(e => e.Key, e => e.Value);

        [Fact]
        public void HiddenByDefaultAndPassedExplicitly()
        {
            Assert.False(new LauncherSettings().CutsceneArms);
            Assert.Equal("hidden", Env(LaunchPlanBuilder.Build(Inputs()))["ETERNALVR_CUTSCENE_ARMS"]);
            Assert.Equal("shown", Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { CutsceneArms = true })))["ETERNALVR_CUTSCENE_ARMS"]);
            // Show arms off: the setting is still passed as it stands, and the layer ignores it under ETERNALVR_ARMS=hidden
            // (the arms stay hidden in cutscenes, the weapon FOV as in play).
            foreach (var cutsceneArms in new[] { false, true })
            {
                var env = Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { ShowArms = false, CutsceneArms = cutsceneArms })));
                Assert.Equal("hidden", env["ETERNALVR_ARMS"]);
                Assert.Equal(cutsceneArms ? "shown" : "hidden", env["ETERNALVR_CUTSCENE_ARMS"]);
            }
        }

        [Fact]
        public void TheSettingRoundTripsAndAnOlderFileHidesTheArms()
        {
            Assert.True(LauncherSettings.Parse(new LauncherSettings { CutsceneArms = true }.Serialize()).CutsceneArms);
            Assert.Contains("cutscene_arms = 1", new LauncherSettings { CutsceneArms = true }.Serialize());
            Assert.Contains("cutscene_arms = 0", new LauncherSettings().Serialize());
            Assert.False(LauncherSettings.Parse("schema_version = 2\ncutscene_view = immersive\n").CutsceneArms);
            Assert.Empty(LauncherSettings.Parse("schema_version = 2\ncutscene_arms = 1\n").UnknownKeys);
            Assert.False(new LauncherSettings { CutsceneArms = true }.WithDefaults().CutsceneArms);
        }

        [Fact]
        public void ItAppliesOnlyToCutscenesAroundYouWithTheArmsShown()
        {
            var immersive = new LauncherSettings { Cutscenes = CutsceneView.Immersive };
            Assert.Null(SettingRules.WhyNot(Setting.CutsceneArms, immersive));
            Assert.Equal(SettingRules.NeedsImmersive, SettingRules.WhyNot(Setting.CutsceneArms, new LauncherSettings()));
            Assert.Equal(SettingRules.NeedsShownArms,
                SettingRules.WhyNot(Setting.CutsceneArms, new LauncherSettings { Cutscenes = CutsceneView.Immersive, ShowArms = false }));
            Assert.Equal(SettingRules.NeedsControllers,
                SettingRules.WhyNot(Setting.CutsceneArms, new LauncherSettings { Cutscenes = CutsceneView.Immersive, Controllers = false }));
            var text = SettingTexts.For(Setting.CutsceneArms);
            Assert.Equal("Arms in cutscenes", text.Label);
            Assert.DoesNotContain("\u2014", text.Tooltip);
        }
    }
}
