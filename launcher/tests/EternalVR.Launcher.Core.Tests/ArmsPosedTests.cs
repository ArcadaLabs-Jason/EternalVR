using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>
    /// The arms the layer poses (T-160): the free off hand and the bind pose's solve (the layer's ETERNALVR_OFFHAND and
    /// ETERNALVR_ARM_SOLVE) whenever the weapon hand aims, with no setting of their own; the game's off hand and the animated
    /// solve otherwise. The old New arms setting's key is not read.
    /// </summary>
    public class ArmsPosedTests
    {
        private static LaunchInputs Inputs(LauncherSettings s = null) => new LaunchInputs
        {
            GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
            LayerDir = @"E:\EternalVR\layer",
            LogDir = @"E:\data\logs\20261024-120000",
            Settings = s ?? new LauncherSettings(),
            ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
        };

        private static Dictionary<string, string> Env(LaunchPlan p) => p.Environment.ToDictionary(e => e.Key, e => e.Value);

        [Fact]
        public void TheNewArmsByDefaultPassedExplicitly()
        {
            var env = Env(LaunchPlanBuilder.Build(Inputs()));
            Assert.Equal("bind", env["ETERNALVR_ARM_SOLVE"]);
            Assert.Equal("free", env["ETERNALVR_OFFHAND"]);
            Assert.Equal("shown", env["ETERNALVR_ARMS"]);
            foreach (var arms in new[] { ArmsView.Hidden, ArmsView.HandsOnly })
            {
                var other = Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { Arms = arms })));
                Assert.Equal("bind", other["ETERNALVR_ARM_SOLVE"]);
                Assert.Equal("free", other["ETERNALVR_OFFHAND"]);
                Assert.Equal(LauncherSettings.ArmsName(arms), other["ETERNALVR_ARMS"]);
            }
        }

        [Theory]
        [InlineData(false, AimMode.Hand)]
        [InlineData(true, AimMode.Head)]
        [InlineData(true, AimMode.View)]
        [InlineData(false, AimMode.Head)]
        public void HeadAimAndGamepadSessionsKeepTheGamesArms(bool controllers, AimMode aim)
        {
            // Where the layer does not pose the arms: the game's off hand, the animated solve and the game's arms, as before;
            // hidden arms stay hidden.
            foreach (var arms in new[] { ArmsView.Shown, ArmsView.HandsOnly })
            {
                var env = Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { Arms = arms, Controllers = controllers, Aim = aim })));
                Assert.Equal("game", env["ETERNALVR_OFFHAND"]);
                Assert.Equal("animated", env["ETERNALVR_ARM_SOLVE"]);
                Assert.Equal("shown", env["ETERNALVR_ARMS"]);
            }
            var hidden = Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { Arms = ArmsView.Hidden, Controllers = controllers, Aim = aim })));
            Assert.Equal("hidden", hidden["ETERNALVR_ARMS"]);
            Assert.Equal("game", hidden["ETERNALVR_OFFHAND"]);
            Assert.Equal("animated", hidden["ETERNALVR_ARM_SOLVE"]);
        }

        [Theory]
        [InlineData("new_arms = 0")]
        [InlineData("new_arms = 1")]
        public void AnOldNewArmsLineIsNotReadAndIsDropped(string line)
        {
            // A launcher.ini from the testing build: new_arms = 0 must not keep a player on the old arms.
            var s = LauncherSettings.Parse("schema_version = 2\n" + line + "\n");
            Assert.Empty(s.UnknownKeys);
            Assert.DoesNotContain("new_arms", s.Serialize());
            var env = Env(LaunchPlanBuilder.Build(Inputs(s)));
            Assert.Equal("free", env["ETERNALVR_OFFHAND"]);
            Assert.Equal("bind", env["ETERNALVR_ARM_SOLVE"]);
        }

        [Fact]
        public void NoWindowRowIsLeft()
        {
            Assert.DoesNotContain("NewArms", System.Enum.GetNames(typeof(Setting)));
            Assert.DoesNotContain(System.Enum.GetValues(typeof(Setting)).Cast<Setting>(), s => SettingKeys.Of(s).Contains("new_arms"));
        }
    }
}
