using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>"Revenant aim with": what aims the Revenant while piloting it, apart from Aim with.</summary>
    public class RevenantAimTests
    {
        private static LaunchInputs Inputs(LauncherSettings s = null) => new LaunchInputs
        {
            GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
            LayerDir = @"E:\EternalVR\layer",
            LogDir = @"E:\data\logs\20260926-010203",
            Settings = s ?? new LauncherSettings(),
            ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
        };

        private static Dictionary<string, string> Env(LaunchPlan p) => p.Environment.ToDictionary(e => e.Key, e => e.Value);

        [Fact]
        public void SameAsAimWithIsTheDefaultAndPassesNothing()
        {
            Assert.Equal(RevenantAimMode.Same, new LauncherSettings().RevenantAim);
            foreach (var aim in new[] { AimMode.Hand, AimMode.Head, AimMode.View })
                Assert.False(Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { Aim = aim }))).ContainsKey("ETERNALVR_DEMON_AIM"));
        }

        [Fact]
        public void AChoiceIsPassedWhateverAimWithSays()
        {
            var head = new LauncherSettings { Aim = AimMode.Hand, RevenantAim = RevenantAimMode.Head };
            var env = Env(LaunchPlanBuilder.Build(Inputs(head)));
            Assert.Equal("hand", env["ETERNALVR_AIM"]);
            Assert.Equal("head", env["ETERNALVR_DEMON_AIM"]);
            var hand = new LauncherSettings { Aim = AimMode.Head, RevenantAim = RevenantAimMode.Hand };
            env = Env(LaunchPlanBuilder.Build(Inputs(hand)));
            Assert.Equal("head", env["ETERNALVR_AIM"]);
            Assert.Equal("hand", env["ETERNALVR_DEMON_AIM"]);
        }

        [Fact]
        public void HandAimWithoutControllersFallsBackToTheHead()
        {
            var s = new LauncherSettings { Controllers = false, RevenantAim = RevenantAimMode.Hand };
            var env = Env(LaunchPlanBuilder.Build(Inputs(s)));
            Assert.Equal("head", env["ETERNALVR_AIM"]);
            Assert.Equal("head", env["ETERNALVR_DEMON_AIM"]);
        }

        [Fact]
        public void TheSettingRoundTripsAndAnOlderFileTakesTheDefault()
        {
            foreach (var mode in new[] { RevenantAimMode.Same, RevenantAimMode.Hand, RevenantAimMode.Head })
                Assert.Equal(mode, LauncherSettings.Parse(new LauncherSettings { RevenantAim = mode }.Serialize()).RevenantAim);
            Assert.Contains("revenant_aim = head", new LauncherSettings { RevenantAim = RevenantAimMode.Head }.Serialize());
            Assert.Contains("revenant_aim = same", new LauncherSettings().Serialize());
            Assert.Contains("schema_version = 2", new LauncherSettings { RevenantAim = RevenantAimMode.Hand }.Serialize());
            // A file from before the key, or a value this version does not know: the default.
            var old = LauncherSettings.Parse("schema_version = 2\naim = head\nworld_scale = 1.10\n");
            Assert.Equal(RevenantAimMode.Same, old.RevenantAim);
            Assert.Equal(AimMode.Head, old.Aim);
            Assert.Equal(RevenantAimMode.Same, LauncherSettings.Parse("schema_version = 1\naim = view\n").RevenantAim);
            Assert.Equal(RevenantAimMode.Same, LauncherSettings.Parse("schema_version = 2\nrevenant_aim = feet\n").RevenantAim);
            Assert.Equal(RevenantAimMode.Hand, LauncherSettings.Parse("schema_version = 2\nrevenant_aim = Hand\n").RevenantAim);
            // A known key: not kept among the unknown ones, so it is not written twice.
            Assert.Empty(LauncherSettings.Parse("schema_version = 2\nrevenant_aim = head\n").UnknownKeys);
            // Reset to defaults goes back to the same as Aim with.
            Assert.Equal(RevenantAimMode.Same, new LauncherSettings { RevenantAim = RevenantAimMode.Head }.WithDefaults().RevenantAim);
        }

        [Fact]
        public void ItAppliesOnlyWithControllersAndTheHandOrTheHeadAiming()
        {
            Assert.Null(SettingRules.WhyNot(Setting.RevenantAimWith, new LauncherSettings()));
            Assert.Null(SettingRules.WhyNot(Setting.RevenantAimWith, new LauncherSettings { Aim = AimMode.Head }));
            Assert.Null(SettingRules.WhyNot(Setting.RevenantAimWith, new LauncherSettings { Mode = VrMode.Mono }));
            Assert.Equal(SettingRules.NeedsHeadOrHandAim, SettingRules.WhyNot(Setting.RevenantAimWith, new LauncherSettings { Aim = AimMode.View }));
            Assert.Equal(SettingRules.NeedsControllers, SettingRules.WhyNot(Setting.RevenantAimWith, new LauncherSettings { Controllers = false }));
        }

        [Fact]
        public void TheRowShowsItsChoicesInEnumOrder()
        {
            var text = SettingTexts.For(Setting.RevenantAimWith);
            Assert.Equal("Revenant aim with", text.Label);
            Assert.Equal(new[] { "Same as Aim with", "Weapon hand", "Head" }, text.Choices);
            Assert.Contains("Revenant", text.Tooltip);
            Assert.Contains("out of the way", text.Tooltip);
        }
    }
}
