using System;
using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Game;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Safety;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>
    /// Screen-space reflections in stereo: the player's own r_SSR from their config reaches the layer, which holds it
    /// while per-eye TAA runs and follows the game's Reflections setting, or holds it off with the launcher's Off.
    /// </summary>
    public class SsrTests
    {
        private static Dictionary<string, string> Env(LauncherSettings s, string playerSsr, string playerSsdo = null) =>
            Plan(s, playerSsr, playerSsdo).Environment.ToDictionary(e => e.Key, e => e.Value);

        private static LaunchPlan Plan(LauncherSettings s, string playerSsr = null, string playerSsdo = null) =>
            LaunchPlanBuilder.Build(new LaunchInputs
            {
                GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
                LayerDir = @"E:\EternalVR\layer",
                Settings = s,
                ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
                PlayerSsr = playerSsr,
                PlayerSsdo = playerSsdo,
            });

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
        public void OffHoldsThemOffWhateverThePlayersConfig()
        {
            var s = new LauncherSettings { Reflections = ReflectionsMode.Off };
            Assert.Equal("off", Env(s, null)["ETERNALVR_STEREO_SSR"]);
            Assert.Equal("off", Env(s, "1")["ETERNALVR_STEREO_SSR"]);
            s.Mode = VrMode.Mono;
            Assert.False(Env(s, null).ContainsKey("ETERNALVR_STEREO_SSR"));
        }

        [Fact]
        public void TheSettingIsTheGamesByDefaultAndKeptInLauncherIni()
        {
            var s = new LauncherSettings();
            Assert.Equal(ReflectionsMode.Game, s.Reflections);
            Assert.Contains("screen_reflections = game", s.Serialize());
            s.Reflections = ReflectionsMode.Off;
            Assert.Contains("screen_reflections = off", s.Serialize());
            Assert.Equal(ReflectionsMode.Off, LauncherSettings.Parse(s.Serialize()).Reflections);
            Assert.Equal(ReflectionsMode.Game, LauncherSettings.Parse("schema_version = 2\nscreen_reflections = high\n").Reflections);
            Assert.Equal(ReflectionsMode.Game, LauncherSettings.Parse("schema_version = 2\n").Reflections);
            Assert.Equal(Enum.GetValues(typeof(ReflectionsMode)).Length, SettingTexts.For(Setting.ScreenReflections).Choices.Count);
            // Stereo only, and nothing to choose with anti-aliasing Off: the game turns them off itself then.
            Assert.Null(SettingRules.WhyNot(Setting.ScreenReflections, new LauncherSettings()));
            Assert.Equal(SettingRules.NeedsStereo, SettingRules.WhyNot(Setting.ScreenReflections, new LauncherSettings { Mode = VrMode.Mono }));
            Assert.Equal(SettingRules.NotWithAntiAliasingOff,
                SettingRules.WhyNot(Setting.ScreenReflections, new LauncherSettings { AntiAliasing = AntiAliasingMode.Off }));
            Assert.Null(SettingRules.WhyNot(Setting.ScreenReflections, new LauncherSettings { AntiAliasing = AntiAliasingMode.Dlss }));
        }

        [Fact]
        public void TheRestoreKeepsTheirSsrWhileItIsTheirReflectionsSetting()
        {
            Assert.Equal(new[] { "r_SSDO", "r_SSR" }, Plan(new LauncherSettings()).KeptKeys);
            Assert.Equal(new[] { "r_SSDO", "r_SSR" }, Plan(new LauncherSettings { AntiAliasing = AntiAliasingMode.Dlss }).KeptKeys);
            // Mono: no follow in the layer, put back as before.
            Assert.Empty(Plan(new LauncherSettings { Mode = VrMode.Mono }).KeptKeys);
            // The launcher's Off, or the game's own r_SSDO 0 and r_SSR 0 after r_TAASafeMode 1 (anti-aliasing Off): put back.
            Assert.Equal(new[] { "r_SSDO" }, Plan(new LauncherSettings { Reflections = ReflectionsMode.Off }).KeptKeys);
            Assert.Empty(Plan(new LauncherSettings { AntiAliasing = AntiAliasingMode.Off }).KeptKeys);
        }

        [Fact]
        public void APlayersLowMadeInTheMenuStaysAfterTheSessionWhenTheLayerFollowedIt()
        {
            // A player's 0.1.33 export: Reflections set to Low in a VR session; the restore took the "0" out again.
            var keys = new[] { "r_SSDO", "r_SSR", "r_hdrDisplay" };
            string Run(string[] kept, string[] followed, string extra = null, RestoreReport[] report = null)
            {
                using (var t = new TempDir())
                {
                    t.Write("saved/base/DOOMEternalConfig.local", "r_SSRQuality \"2\"\n");
                    var locs = GameLayout.FindSettingsLocations(t.Combine("saved"), null, null);
                    var snap = SettingsSnapshot.Take(t.Combine("snaps"), "s1", locs, extra == null ? null : new[] { extra }, kept);
                    t.Write("saved/base/DOOMEternalConfig.local", "r_SSRQuality \"0\"\nr_SSR \"0\"\nr_SSDO \"0\"\n");
                    var r = SettingsSnapshot.Restore(snap, keys, followed);
                    if (report != null) report[0] = r;
                    return t.Read("saved/base/DOOMEternalConfig.local");
                }
            }
            var both = new[] { "r_SSDO", "r_SSR" };
            var result = new RestoreReport[1];
            // The plan kept it and the layer's status file said it followed the game's setting: as the game saved it.
            Assert.Equal("r_SSRQuality \"0\"\nr_SSR \"0\"\n", Run(both, new[] { "r_SSR" }, report: result));
            Assert.Contains(result[0].Lines, l => l.StartsWith("kept saved-games/base/DOOMEternalConfig.local: r_SSR as the game saved it (\"0\"; absent before)"));
            Assert.Equal("r_SSRQuality \"0\"\nr_SSR \"0\"\nr_SSDO \"0\"\n", Run(both, both));
            // No follow reported (quit before a map, failed closed, the setter not found, an older layer): put back.
            Assert.Equal("r_SSRQuality \"0\"\n", Run(both, null));
            Assert.Equal("r_SSRQuality \"0\"\n", Run(both, new string[0]));
            // Not kept by the plan (the launcher's Off, anti-aliasing Off): put back whatever the layer said.
            Assert.Equal("r_SSRQuality \"0\"\n", Run(null, both));
            // Set by an Extra game argument: put back as before.
            Assert.Equal("r_SSRQuality \"0\"\n", Run(new[] { "r_SSR" }, new[] { "r_SSR" }, extra: "r_SSR"));
        }

        [Fact]
        public void TheLayersStatusFileNamesTheFollowedCvars()
        {
            Assert.Empty(LayerStatusFile.Parse("state=vr\nstereo=on\n").FollowedKeys);
            Assert.Equal(new[] { "r_SSR", "r_SSDO" }, LayerStatusFile.Parse("state=vr\nssr_follow=1\nssdo_follow=1\n").FollowedKeys);
            Assert.Equal(new[] { "r_SSDO" }, LayerStatusFile.Parse("ssr_follow=1\nssdo_follow=1\nssr_follow=0\n").FollowedKeys);
            Assert.Empty(LayerStatusFile.Parse("ssr_follow=0\nssdo_follow=yes\n").FollowedKeys);
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
