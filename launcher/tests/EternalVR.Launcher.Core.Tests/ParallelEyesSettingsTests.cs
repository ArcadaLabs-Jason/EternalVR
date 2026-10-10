using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using EternalVR.Launcher.Core.Game;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>The Play tab's "Parallel Eye Rendering" option: its key, its variables, its rule and its words.</summary>
    public class ParallelEyesSettingsTests
    {
        private static LaunchPlan Plan(LauncherSettings s, bool parallelEyesGame = true) =>
            LaunchPlanBuilder.Build(new LaunchInputs
            {
                GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
                LayerDir = @"E:\EternalVR\layer",
                Settings = s,
                ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
                ParallelEyesGame = parallelEyesGame,
            });

        private static Dictionary<string, string> Env(LauncherSettings s, bool parallelEyesGame = true) =>
            Plan(s, parallelEyesGame).Environment.ToDictionary(e => e.Key, e => e.Value);

        [Fact]
        public void TheLayerTurnsAsyncComputeOffNotTheCommandLine()
        {
            // The layer holds async compute off while Parallel Eye Rendering is on (a launch from a .cmd file has
            // no launcher command line), so the launch adds nothing the player's config could keep.
            Assert.DoesNotContain("+r_enableAsyncCompute", Plan(new LauncherSettings { ParallelEyes = true }).Arguments);
            Assert.DoesNotContain("+r_enableAsyncCompute", Plan(new LauncherSettings()).Arguments);
        }

        [Fact]
        public void OffByDefaultAndThenNeitherVariableIsSet()
        {
            Assert.False(new LauncherSettings().ParallelEyes);
            var env = Env(new LauncherSettings());
            Assert.False(env.ContainsKey("ETERNALVR_PARALLEL_EYES"));
            Assert.False(env.ContainsKey("ETERNALVR_STEREO_EXPERIMENT"));
        }

        [Fact]
        public void OnSetsTheLayerSwitchInStereoOnly()
        {
            var env = Env(new LauncherSettings { ParallelEyes = true });
            Assert.Equal("1", env["ETERNALVR_PARALLEL_EYES"]);
            // The layer picks its renderer from the switch (and the game version): not the experiment variables.
            Assert.False(env.ContainsKey("ETERNALVR_STEREO_EXPERIMENT"));
            Assert.False(env.ContainsKey("ETERNALVR_VIEW_SLOTS"));
            // The layer reads it only in stereo: a mono launch does not set it.
            var mono = Env(new LauncherSettings { ParallelEyes = true, Mode = VrMode.Mono });
            Assert.False(mono.ContainsKey("ETERNALVR_PARALLEL_EYES"));
        }

        [Fact]
        public void WithDlssTaaOrOff()
        {
            // DLSS: each view runs its own DLSS feature, at the launcher's quality, as the standard renderer runs it per eye.
            var dlss = new LauncherSettings { ParallelEyes = true, AntiAliasing = AntiAliasingMode.Dlss, Dlss = DlssQuality.Balanced };
            Assert.True(dlss.ParallelEyesAsked);
            Assert.True(dlss.ParallelEyesOn(true));
            Assert.Null(SettingRules.WhyNot(Setting.ParallelEyes, dlss));
            var env = Env(dlss);
            Assert.Equal("1", env["ETERNALVR_PARALLEL_EYES"]);
            Assert.Equal("1", env["ETERNALVR_STEREO_DLSS"]);
            Assert.Equal(LauncherSettings.DlssQualityName(DlssQuality.Balanced), env["ETERNALVR_STEREO_DLSS_QUALITY"]);
            // The layer's fallback switch is the layer's own: the launch does not set it.
            Assert.False(env.ContainsKey("ETERNALVR_PE_DLSS"));
            // Its DLSS rows apply as without Parallel Eye Rendering.
            Assert.Null(SettingRules.WhyNot(Setting.DlssQuality, dlss));
            dlss.AntiAliasing = AntiAliasingMode.Taa;
            Assert.True(dlss.ParallelEyesAsked);
            Assert.True(dlss.ParallelEyesOn(true));
            Assert.Null(SettingRules.WhyNot(Setting.ParallelEyes, dlss));
            Assert.False(Env(dlss).ContainsKey("ETERNALVR_STEREO_DLSS"));
            // Off: the layer holds r_antialiasing 0 from ETERNALVR_STEREO_TAA=0, as for the standard renderer.
            var off = Env(new LauncherSettings { ParallelEyes = true, AntiAliasing = AntiAliasingMode.Off });
            Assert.Equal("1", off["ETERNALVR_PARALLEL_EYES"]);
            Assert.Equal("0", off["ETERNALVR_STEREO_TAA"]);
            Assert.Null(SettingRules.WhyNot(Setting.ParallelEyes, new LauncherSettings { AntiAliasing = AntiAliasingMode.Off }));
        }

        [Fact]
        public void AlternateEyesIsPassedAsZeroAndKeepsNoPacingOff()
        {
            foreach (var mode in new[] { AlternateEyesMode.On, AlternateEyesMode.Auto })
            {
                var s = new LauncherSettings { ParallelEyes = true, AlternateEyes = mode, Pacing = FramePacing.Headset };
                var env = Env(s);
                Assert.Equal("1", env["ETERNALVR_PARALLEL_EYES"]);
                Assert.Equal("0", env["ETERNALVR_ALTERNATE_EYES"]);
                // Auto no longer turns frame pacing off: Parallel Eye Rendering ignores alternate eyes.
                Assert.Equal(LauncherSettings.PacingName(FramePacing.Headset), env["ETERNALVR_PACE"]);
                Assert.Equal(SettingRules.NotWithParallelEyes, SettingRules.WhyNot(Setting.AlternateEyes, s));
                Assert.Null(SettingRules.WhyNot(Setting.FramePacing, s));
            }
            // With DLSS too.
            var dlss = new LauncherSettings
            {
                ParallelEyes = true, AntiAliasing = AntiAliasingMode.Dlss, AlternateEyes = AlternateEyesMode.Auto,
                Pacing = FramePacing.Headset,
            };
            Assert.Equal("0", Env(dlss)["ETERNALVR_ALTERNATE_EYES"]);
            Assert.Equal(SettingRules.NotWithParallelEyes, SettingRules.WhyNot(Setting.AlternateEyes, dlss));
            // Without Parallel Eye Rendering alternate eyes is as before.
            foreach (var s in new[]
            {
                new LauncherSettings { AlternateEyes = AlternateEyesMode.Auto, Pacing = FramePacing.Headset },
                new LauncherSettings
                {
                    AntiAliasing = AntiAliasingMode.Dlss, AlternateEyes = AlternateEyesMode.Auto, Pacing = FramePacing.Headset,
                },
            })
            {
                var env = Env(s);
                Assert.Equal("auto", env["ETERNALVR_ALTERNATE_EYES"]);
                Assert.Equal(LauncherSettings.PacingName(FramePacing.Off), env["ETERNALVR_PACE"]);
                Assert.Null(SettingRules.WhyNot(Setting.AlternateEyes, s));
                Assert.Equal(SettingRules.NotWithAutoEyes, SettingRules.WhyNot(Setting.FramePacing, s));
            }
            Assert.Equal("Not with Parallel Eye Rendering (Play tab).", SettingRules.NotWithParallelEyes);
        }

        [Fact]
        public void OnAnotherBuildAlternateEyesAndFoveationApplyAsWithoutIt()
        {
            // Game Pass (or any build but the Steam one the layer knows): the layer refuses Parallel Eye Rendering and runs
            // the standard renderer, so the launch keeps the player's Alternate eyes and Foveation.
            var s = new LauncherSettings
            {
                ParallelEyes = true, AlternateEyes = AlternateEyesMode.Auto, Foveation = FoveationMode.Balanced, Pacing = FramePacing.Headset,
            };
            Assert.True(s.ParallelEyesAsked);
            Assert.False(s.ParallelEyesOn(false));
            var env = Env(s, parallelEyesGame: false);
            Assert.Equal("auto", env["ETERNALVR_ALTERNATE_EYES"]);
            Assert.Equal(LauncherSettings.FoveationName(FoveationMode.Balanced), env["ETERNALVR_FOVEATION"]);
            Assert.Equal(LauncherSettings.PacingName(FramePacing.Off), env["ETERNALVR_PACE"]);
            // Not asked of the layer, which would refuse it there.
            Assert.False(env.ContainsKey("ETERNALVR_PARALLEL_EYES"));
            // The rows: Parallel Eye Rendering greyed out with the reason, Alternate eyes and Foveation open.
            Assert.Equal(SettingRules.NeedsParallelEyesBuild, SettingRules.WhyNot(Setting.ParallelEyes, s, false));
            Assert.Null(SettingRules.WhyNot(Setting.AlternateEyes, s, false));
            Assert.Null(SettingRules.WhyNot(Setting.Foveation, s, false));
            Assert.Equal(SettingRules.NotWithAutoEyes, SettingRules.WhyNot(Setting.FramePacing, s, false));
            Assert.False(SettingRules.Inapplicable(s, false).ContainsKey(Setting.Foveation));
            // On the build it runs on, as before.
            var on = Env(s);
            Assert.Equal("0", on["ETERNALVR_ALTERNATE_EYES"]);
            Assert.False(on.ContainsKey("ETERNALVR_FOVEATION"));
            Assert.Equal(SettingRules.NotWithParallelEyes, SettingRules.WhyNot(Setting.Foveation, s, true));
            Assert.Equal(SettingRules.NotWithParallelEyes, SettingRules.Inapplicable(s, true)[Setting.Foveation]);
            Assert.Equal("1", on["ETERNALVR_PARALLEL_EYES"]);
            // One reason for Game Pass and for a later Steam build alike.
            Assert.Equal("Not on this game version yet.", SettingRules.NeedsParallelEyesBuild);
            // Mono stays the first reason.
            Assert.Equal(SettingRules.NeedsStereo, SettingRules.WhyNot(Setting.ParallelEyes, new LauncherSettings { Mode = VrMode.Mono }, false));
        }

        [Fact]
        public void OnlyTheSteamBuildTheLayerKnowsRunsIt()
        {
            var builds = KnownBuilds.Parse(TestData.Read("known-builds.txt"));
            var steam = builds.All.Single(b => b.Platform == GamePlatform.Steam && b.BuildId == BuildCheck.ParallelEyesSteamBuild);
            Assert.True(new BuildCheck(BuildStatus.Known, steam.Sha256, steam).RunsParallelEyes);
            // Game Pass: a supported build, but not one Parallel Eye Rendering runs on.
            var gamePass = builds.All.First(b => b.Platform == GamePlatform.GamePass);
            var store = builds.CheckStore(new StoreIdentity(gamePass.StoreName, null, gamePass.BuildId));
            Assert.Equal(BuildStatus.Known, store.Status);
            Assert.False(store.RunsParallelEyes);
            // Another Steam build, an unknown exe or none.
            var other = new KnownBuild(new string('0', 64), "99999999", "Steam, some other build");
            Assert.False(new BuildCheck(BuildStatus.Known, other.Sha256, other).RunsParallelEyes);
            Assert.False(new BuildCheck(BuildStatus.Unknown, new string('1', 64), null).RunsParallelEyes);
            Assert.False(new BuildCheck(BuildStatus.Missing, null, null).RunsParallelEyes);
        }

        [Fact]
        public void ItsSteamBuildIsTheLayers()
        {
            // The layer checks the exe's timestamp (view_install.cpp); its comment names the Steam build.
            var install = File.ReadAllText(RepoFile("src", "vkcore", "view_install.cpp"));
            Assert.Contains("kKnownTimestamp = 0x6A7B9B8C; // Steam build " + BuildCheck.ParallelEyesSteamBuild, install);
        }

        private static string RepoFile(params string[] parts)
        {
            for (var dir = new DirectoryInfo(AppContext.BaseDirectory); dir != null; dir = dir.Parent)
            {
                var path = Path.Combine(new[] { dir.FullName }.Concat(parts).ToArray());
                if (File.Exists(path)) return path;
            }
            throw new FileNotFoundException("not found above the test folder: " + string.Join("/", parts));
        }

        [Fact]
        public void StoredLikeTheOtherSettings()
        {
            var on = new LauncherSettings { ParallelEyes = true };
            Assert.Contains("parallel_eyes = 1", on.Serialize());
            Assert.True(LauncherSettings.Parse(on.Serialize()).ParallelEyes);
            Assert.Contains("parallel_eyes = 0", new LauncherSettings().Serialize());
            Assert.False(LauncherSettings.Parse(new LauncherSettings().Serialize()).ParallelEyes);
            // A file without the key (an older launcher's), or with a value it does not know, keeps it off.
            Assert.False(LauncherSettings.Parse("schema_version = 2\nmode = stereo\n").ParallelEyes);
            Assert.False(LauncherSettings.Parse("schema_version = 2\nparallel_eyes = maybe\n").ParallelEyes);
            Assert.True(LauncherSettings.Parse("schema_version = 2\nparallel_eyes = TRUE\n").ParallelEyes);
            Assert.True(LauncherSettings.Parse("schema_version = 2\nparallel_eyes = on\n").ParallelEyes);
            // A known key: not kept as an unknown one.
            Assert.Empty(LauncherSettings.Parse("schema_version = 2\nparallel_eyes = 1\n").UnknownKeys);
        }

        [Fact]
        public void ResetTurnsItOffAndProfilesKeepIt()
        {
            Assert.False(new LauncherSettings { ParallelEyes = true }.WithDefaults().ParallelEyes);
            Assert.True(new LauncherSettings { ParallelEyes = true }.Clone().ParallelEyes);
        }

        [Fact]
        public void OnlyAppliesInStereo()
        {
            Assert.Null(SettingRules.WhyNot(Setting.ParallelEyes, new LauncherSettings()));
            Assert.Equal(SettingRules.NeedsStereo, SettingRules.WhyNot(Setting.ParallelEyes, new LauncherSettings { Mode = VrMode.Mono }));
        }

        [Fact]
        public void ItsWordsSayWhatItDoes()
        {
            var text = SettingTexts.For(Setting.ParallelEyes);
            Assert.Equal("Parallel Eye Rendering (highly experimental)", text.Label);
            Assert.Empty(text.Choices);
            Assert.Contains("at the same time", text.Tooltip);
            Assert.Contains("Off by default", text.Tooltip);
            Assert.Contains("Highly experimental", text.Tooltip);
            // One Steam build, not every Steam version: a Steam update falls back to the standard renderer.
            Assert.Contains("the Steam version this release supports", text.Tooltip);
            Assert.DoesNotContain("Steam version only", text.Tooltip);
            // No promised speed-up: no number of percent.
            Assert.DoesNotContain("%", text.Tooltip);
            // Plain words: no em or en dashes.
            Assert.DoesNotContain("\u2014", text.Tooltip);
            Assert.DoesNotContain("\u2013", text.Tooltip);
        }
    }
}
