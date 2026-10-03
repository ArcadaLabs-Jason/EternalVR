using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>"Foveated rendering": the edges of each eye shaded at a lower rate, stereo only, off by default.</summary>
    public class FoveationTests
    {
        private static readonly FoveationMode[] AllModes =
            { FoveationMode.Off, FoveationMode.Subtle, FoveationMode.Balanced, FoveationMode.Aggressive, FoveationMode.Maximum };

        private static LaunchInputs Inputs(LauncherSettings s = null) => new LaunchInputs
        {
            GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
            LayerDir = @"E:\EternalVR\layer",
            LogDir = @"E:\data\logs\20260930-010203",
            Settings = s ?? new LauncherSettings(),
            ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
        };

        private static Dictionary<string, string> Env(LaunchPlan p) => p.Environment.ToDictionary(e => e.Key, e => e.Value);

        [Fact]
        public void OffIsTheDefaultAndPassesNothing()
        {
            Assert.Equal(FoveationMode.Off, new LauncherSettings().Foveation);
            Assert.False(Env(LaunchPlanBuilder.Build(Inputs())).ContainsKey("ETERNALVR_FOVEATION"));
            Assert.False(Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { Mode = VrMode.Mono }))).ContainsKey("ETERNALVR_FOVEATION"));
        }

        [Fact]
        public void APresetIsPassedInStereo()
        {
            var expected = new Dictionary<FoveationMode, string>
            {
                [FoveationMode.Subtle] = "subtle",
                [FoveationMode.Balanced] = "balanced",
                [FoveationMode.Aggressive] = "aggressive",
                [FoveationMode.Maximum] = "maximum",
            };
            foreach (var kv in expected)
            {
                var env = Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { Foveation = kv.Key })));
                Assert.Equal("stereo", env["ETERNALVR_MODE"]);
                Assert.Equal(kv.Value, env["ETERNALVR_FOVEATION"]);
            }
        }

        [Fact]
        public void NothingIsPassedInMono()
        {
            foreach (var mode in AllModes)
            {
                var s = new LauncherSettings { Mode = VrMode.Mono, Foveation = mode };
                Assert.False(Env(LaunchPlanBuilder.Build(Inputs(s))).ContainsKey("ETERNALVR_FOVEATION"));
            }
        }

        [Fact]
        public void TheSettingRoundTripsAndAnOlderFileTakesOff()
        {
            foreach (var mode in AllModes)
                Assert.Equal(mode, LauncherSettings.Parse(new LauncherSettings { Foveation = mode }.Serialize()).Foveation);
            Assert.Contains("foveation = balanced", new LauncherSettings { Foveation = FoveationMode.Balanced }.Serialize());
            Assert.Contains("foveation = off", new LauncherSettings().Serialize());
            Assert.Contains("foveation = maximum", new LauncherSettings { Foveation = FoveationMode.Maximum }.Serialize());
            Assert.Equal(FoveationMode.Maximum, LauncherSettings.Parse("schema_version = 2\nfoveation = maximum\n").Foveation);
            Assert.Contains("schema_version = 2", new LauncherSettings { Foveation = FoveationMode.Aggressive }.Serialize());
            // A file from before the key, or a value this version does not know: off.
            var old = LauncherSettings.Parse("schema_version = 2\nanti_aliasing = dlss\nworld_scale = 1.10\n");
            Assert.Equal(FoveationMode.Off, old.Foveation);
            Assert.Equal(AntiAliasingMode.Dlss, old.AntiAliasing);
            Assert.Equal(FoveationMode.Off, LauncherSettings.Parse("schema_version = 1\naim = view\n").Foveation);
            Assert.Equal(FoveationMode.Off, LauncherSettings.Parse("schema_version = 2\nfoveation = extreme\n").Foveation);
            Assert.Equal(FoveationMode.Off, LauncherSettings.Parse("schema_version = 2\nfoveation =\n").Foveation);
            Assert.Equal(FoveationMode.Subtle, LauncherSettings.Parse("schema_version = 2\nfoveation = Subtle\n").Foveation);
            // A known key: not kept among the unknown ones, so it is not written twice.
            Assert.Empty(LauncherSettings.Parse("schema_version = 2\nfoveation = aggressive\n").UnknownKeys);
            // Reset to defaults turns it off.
            Assert.Equal(FoveationMode.Off, new LauncherSettings { Foveation = FoveationMode.Balanced }.WithDefaults().Foveation);
        }

        [Fact]
        public void ItAppliesOnlyInStereo()
        {
            Assert.Null(SettingRules.WhyNot(Setting.Foveation, new LauncherSettings()));
            Assert.Null(SettingRules.WhyNot(Setting.Foveation, new LauncherSettings { Controllers = false }));
            Assert.Equal(SettingRules.NeedsStereo, SettingRules.WhyNot(Setting.Foveation, new LauncherSettings { Mode = VrMode.Mono }));
        }

        [Fact]
        public void NotWithParallelEyeRendering()
        {
            foreach (var mode in AllModes)
            {
                var s = new LauncherSettings { ParallelEyes = true, Foveation = mode };
                Assert.False(Env(LaunchPlanBuilder.Build(Inputs(s))).ContainsKey("ETERNALVR_FOVEATION"));
                Assert.Equal("1", Env(LaunchPlanBuilder.Build(Inputs(s)))["ETERNALVR_PARALLEL_EYES"]);
                Assert.Equal(SettingRules.NotWithParallelEyes, SettingRules.WhyNot(Setting.Foveation, s));
                // The value is kept for when Parallel Eye Rendering is off again.
                Assert.Equal(mode, LauncherSettings.Parse(s.Serialize()).Foveation);
            }
            Assert.Equal("Not with Parallel Eye Rendering (Play tab).", SettingRules.NotWithParallelEyes);
            // Mono comes first.
            Assert.Equal(SettingRules.NeedsStereo,
                SettingRules.WhyNot(Setting.Foveation, new LauncherSettings { Mode = VrMode.Mono, ParallelEyes = true }));
            // With DLSS, Parallel Eye Rendering does not run: foveation applies and is passed.
            var dlss = new LauncherSettings
            {
                ParallelEyes = true, AntiAliasing = AntiAliasingMode.Dlss, Foveation = FoveationMode.Balanced,
            };
            Assert.Null(SettingRules.WhyNot(Setting.Foveation, dlss));
            var env = Env(LaunchPlanBuilder.Build(Inputs(dlss)));
            Assert.Equal("balanced", env["ETERNALVR_FOVEATION"]);
            Assert.False(env.ContainsKey("ETERNALVR_PARALLEL_EYES"));
            // Parallel Eye Rendering off again: passed as before.
            var off = new LauncherSettings { ParallelEyes = false, Foveation = FoveationMode.Subtle };
            Assert.Null(SettingRules.WhyNot(Setting.Foveation, off));
            Assert.Equal("subtle", Env(LaunchPlanBuilder.Build(Inputs(off)))["ETERNALVR_FOVEATION"]);
        }

        [Fact]
        public void TheRowShowsItsChoicesInEnumOrder()
        {
            var text = SettingTexts.For(Setting.Foveation);
            Assert.Equal("Foveated rendering (experimental)", text.Label);
            Assert.Equal(new[] { "Off", "Subtle", "Balanced", "Aggressive", "Maximum" }, text.Choices);
            Assert.Contains("NVIDIA RTX only", text.Tooltip);
            Assert.Contains("Experimental", text.Tooltip);
            Assert.DoesNotContain("\u2014", text.Tooltip);
        }
    }
}
