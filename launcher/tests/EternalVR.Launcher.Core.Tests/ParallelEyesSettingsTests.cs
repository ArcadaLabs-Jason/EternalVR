using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>The Play tab's "Parallel Eye Rendering" option: its key, its variables, its rule and its words.</summary>
    public class ParallelEyesSettingsTests
    {
        private static LaunchPlan Plan(LauncherSettings s) =>
            LaunchPlanBuilder.Build(new LaunchInputs
            {
                GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
                LayerDir = @"E:\EternalVR\layer",
                Settings = s,
                ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
            });

        private static Dictionary<string, string> Env(LauncherSettings s) =>
            Plan(s).Environment.ToDictionary(e => e.Key, e => e.Value);

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
        public void NotWithDlssButWithTaaOrOff()
        {
            // DLSS runs per eye in the standard renderer: the layer refuses Parallel Eye Rendering with it, so the launch
            // does not ask for it and the option is greyed out.
            var dlss = new LauncherSettings { ParallelEyes = true, AntiAliasing = AntiAliasingMode.Dlss };
            Assert.False(dlss.ParallelEyesOn);
            Assert.False(Env(dlss).ContainsKey("ETERNALVR_PARALLEL_EYES"));
            Assert.Equal("1", Env(dlss)["ETERNALVR_STEREO_DLSS"]);
            Assert.Equal(SettingRules.NotWithDlss, SettingRules.WhyNot(Setting.ParallelEyes, dlss));
            // The value is kept: back on TAA it is on again.
            dlss.AntiAliasing = AntiAliasingMode.Taa;
            Assert.True(dlss.ParallelEyesOn);
            Assert.Null(SettingRules.WhyNot(Setting.ParallelEyes, dlss));
            // Off: the layer holds r_antialiasing 0 from ETERNALVR_STEREO_TAA=0, as for the standard renderer.
            var off = Env(new LauncherSettings { ParallelEyes = true, AntiAliasing = AntiAliasingMode.Off });
            Assert.Equal("1", off["ETERNALVR_PARALLEL_EYES"]);
            Assert.Equal("0", off["ETERNALVR_STEREO_TAA"]);
            Assert.Null(SettingRules.WhyNot(Setting.ParallelEyes, new LauncherSettings { AntiAliasing = AntiAliasingMode.Off }));
            // The reason is one terse sentence.
            Assert.Equal("Not with DLSS (Anti-aliasing).", SettingRules.NotWithDlss);
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
            // Without Parallel Eye Rendering (or with DLSS, where it does not run) alternate eyes is as before.
            foreach (var s in new[]
            {
                new LauncherSettings { AlternateEyes = AlternateEyesMode.Auto, Pacing = FramePacing.Headset },
                new LauncherSettings
                {
                    ParallelEyes = true, AntiAliasing = AntiAliasingMode.Dlss, AlternateEyes = AlternateEyesMode.Auto,
                    Pacing = FramePacing.Headset,
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
            Assert.Equal("Parallel Eye Rendering (experimental)", text.Label);
            Assert.Empty(text.Choices);
            Assert.Contains("at the same time", text.Tooltip);
            Assert.Contains("Off by default", text.Tooltip);
            Assert.Contains("Experimental", text.Tooltip);
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
