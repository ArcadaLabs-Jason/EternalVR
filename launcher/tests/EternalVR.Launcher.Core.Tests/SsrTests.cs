using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Game;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>
    /// Screen-space reflections in stereo: the player's own r_SSR from their config reaches the layer, which holds it
    /// while per-eye TAA runs.
    /// </summary>
    public class SsrTests
    {
        private static Dictionary<string, string> Env(LauncherSettings s, string playerSsr, string playerSsdo = null) =>
            LaunchPlanBuilder.Build(new LaunchInputs
            {
                GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
                LayerDir = @"E:\EternalVR\layer",
                Settings = s,
                ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
                PlayerSsr = playerSsr,
                PlayerSsdo = playerSsdo,
            }).Environment.ToDictionary(e => e.Key, e => e.Value);

        [Fact]
        public void StereoHoldsThePlayersSsrOnUnlessTheirConfigTurnsItOff()
        {
            var s = new LauncherSettings();
            Assert.Equal("1", Env(s, null)["ETERNALVR_STEREO_SSR"]);
            Assert.Equal("1", Env(s, "1")["ETERNALVR_STEREO_SSR"]);
            Assert.Equal("1", Env(s, "2")["ETERNALVR_STEREO_SSR"]);
            Assert.Equal("0", Env(s, "0")["ETERNALVR_STEREO_SSR"]);
            s.AntiAliasing = AntiAliasingMode.Off;
            Assert.Equal("1", Env(s, null)["ETERNALVR_STEREO_SSR"]);
            s.Mode = VrMode.Mono;
            Assert.False(Env(s, "0").ContainsKey("ETERNALVR_STEREO_SSR"));
        }

        [Fact]
        public void SsrAndSsdoFollowTheirOwnCvar()
        {
            var s = new LauncherSettings();
            var env = Env(s, "0", "1");
            Assert.Equal("0", env["ETERNALVR_STEREO_SSR"]);
            Assert.Equal("1", env["ETERNALVR_STEREO_SSDO"]);
            env = Env(s, "1", "0");
            Assert.Equal("1", env["ETERNALVR_STEREO_SSR"]);
            Assert.Equal("0", env["ETERNALVR_STEREO_SSDO"]);
        }

        [Fact]
        public void PlayerCvarReadsTheirSsr()
        {
            using (var t = new TempDir())
            {
                var locations = new[] { new SettingsLocation("saved-games", SettingsLocationKind.SavedGames, t.Path) };
                Assert.Null(GameLayout.PlayerCvar(locations, "r_SSR"));
                t.Write("base/DOOMEternalConfig.local", "seta r_SSR \"0\"\nseta r_SSRQuality \"2\"\n");
                Assert.Equal("0", GameLayout.PlayerCvar(locations, "r_SSR"));
                Assert.Null(GameLayout.PlayerCvar(locations, "r_SSDO"));
            }
        }
    }
}
