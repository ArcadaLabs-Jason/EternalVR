using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Game;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>SSDO in stereo: the player's own r_SSDO from their config reaches the layer, which holds it.</summary>
    public class SsdoTests
    {
        private static Dictionary<string, string> Env(LauncherSettings s, string playerSsdo) =>
            LaunchPlanBuilder.Build(new LaunchInputs
            {
                GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
                LayerDir = @"E:\EternalVR\layer",
                Settings = s,
                ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
                PlayerSsdo = playerSsdo,
            }).Environment.ToDictionary(e => e.Key, e => e.Value);

        [Fact]
        public void StereoHoldsThePlayersSsdoOnUnlessTheirConfigTurnsItOff()
        {
            var s = new LauncherSettings();
            Assert.Equal("1", Env(s, null)["ETERNALVR_STEREO_SSDO"]);
            Assert.Equal("1", Env(s, "1")["ETERNALVR_STEREO_SSDO"]);
            Assert.Equal("1", Env(s, "2")["ETERNALVR_STEREO_SSDO"]);
            Assert.Equal("0", Env(s, "0")["ETERNALVR_STEREO_SSDO"]);
            s.Mode = VrMode.Mono;
            Assert.False(Env(s, "0").ContainsKey("ETERNALVR_STEREO_SSDO"));
        }

        [Fact]
        public void PlayerCvarReadsTheLastConfigThatSetsIt()
        {
            using (var t = new TempDir())
            {
                var locations = new[] { new SettingsLocation("saved-games", SettingsLocationKind.SavedGames, t.Path) };
                Assert.Null(GameLayout.PlayerCvar(locations, "r_SSDO"));
                t.Write("base/DOOMEternalConfig.cfg", "bind \"MOUSE1\" \"_attack\"\nr_SSDO \"0\"\n");
                Assert.Equal("0", GameLayout.PlayerCvar(locations, "r_SSDO"));
                t.Write("base/DOOMEternalConfig.local", "seta r_ssdo \"1\"\n");
                Assert.Equal("1", GameLayout.PlayerCvar(locations, "r_SSDO"));
                Assert.Null(GameLayout.PlayerCvar(locations, "r_SSR"));
            }
        }
    }
}
