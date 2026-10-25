using System.Collections.Generic;
using System.IO;
using System.Linq;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>"Show arms": the Slayer's first-person arms shown (default), hidden with the weapon alone, or hands only.</summary>
    public class ShowArmsTests
    {
        private static LaunchInputs Inputs(LauncherSettings s = null) => new LaunchInputs
        {
            GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
            LayerDir = @"E:\EternalVR\layer",
            LogDir = @"E:\data\logs\20260930-120000",
            Settings = s ?? new LauncherSettings(),
            ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
        };

        private static Dictionary<string, string> Env(LaunchPlan p) => p.Environment.ToDictionary(e => e.Key, e => e.Value);

        [Fact]
        public void ShownByDefaultAndPassedExplicitly()
        {
            Assert.Equal(ArmsView.Shown, new LauncherSettings().Arms);
            Assert.Equal("shown", Env(LaunchPlanBuilder.Build(Inputs()))["ETERNALVR_ARMS"]);
            Assert.Equal("hidden", Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { Arms = ArmsView.Hidden })))["ETERNALVR_ARMS"]);
            Assert.Equal("hands", Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { Arms = ArmsView.HandsOnly })))["ETERNALVR_ARMS"]);
        }

        [Fact]
        public void HandsOnlyLeavesTheArmSettingsToTheLayer()
        {
            // The layer takes the free off hand, the weapon arm's ik and the bind solve for hands only itself; the launcher
            // passes the off hand and the solve as for the arms shown (T-160), and nothing else of its own.
            var shown = Env(LaunchPlanBuilder.Build(Inputs()));
            var hands = Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { Arms = ArmsView.HandsOnly })));
            Assert.Equal(shown.Keys.OrderBy(k => k), hands.Keys.OrderBy(k => k));
            foreach (var k in new[] { "ETERNALVR_WEAPON_ARM", "ETERNALVR_ARM_MODEL" })
                Assert.False(hands.ContainsKey(k), k);
            foreach (var env in new[] { shown, hands })
            {
                Assert.Equal("bind", env["ETERNALVR_ARM_SOLVE"]);
                Assert.Equal("free", env["ETERNALVR_OFFHAND"]);
            }
        }

        [Fact]
        public void EachValueRoundTripsAndAnOlderFileShowsTheArms()
        {
            Assert.Equal(ArmsView.Hidden, LauncherSettings.Parse(new LauncherSettings { Arms = ArmsView.Hidden }.Serialize()).Arms);
            Assert.Equal(ArmsView.HandsOnly, LauncherSettings.Parse(new LauncherSettings { Arms = ArmsView.HandsOnly }.Serialize()).Arms);
            Assert.Equal(ArmsView.Shown, LauncherSettings.Parse(new LauncherSettings().Serialize()).Arms);
            // Shown and hidden are written as before (an older launcher reads 1 and 0 as it did); hands only by name.
            Assert.Contains("show_arms = 0", new LauncherSettings { Arms = ArmsView.Hidden }.Serialize());
            Assert.Contains("show_arms = 1", new LauncherSettings().Serialize());
            Assert.Contains("show_arms = hands", new LauncherSettings { Arms = ArmsView.HandsOnly }.Serialize());
            Assert.Equal(ArmsView.Shown, LauncherSettings.Parse("schema_version = 2\nhandedness = left\n").Arms);
            Assert.Empty(LauncherSettings.Parse("schema_version = 2\nshow_arms = 0\n").UnknownKeys);
            Assert.Equal(ArmsView.Shown, new LauncherSettings { Arms = ArmsView.HandsOnly }.WithDefaults().Arms);
        }

        [Fact]
        public void AnOlderFilesTwoValuesStillLoad()
        {
            // launcher.ini and profiles from before hands only: show_arms 1 / 0, or true / false by hand.
            Assert.Equal(ArmsView.Shown, LauncherSettings.Parse("schema_version = 2\nshow_arms = 1\n").Arms);
            Assert.Equal(ArmsView.Hidden, LauncherSettings.Parse("schema_version = 2\nshow_arms = 0\n").Arms);
            Assert.Equal(ArmsView.Hidden, LauncherSettings.Parse("schema_version = 2\nshow_arms = False\n").Arms);
            Assert.Equal(ArmsView.Shown, LauncherSettings.Parse("schema_version = 2\nshow_arms = true\n").Arms);
            Assert.Equal(ArmsView.HandsOnly, LauncherSettings.Parse("schema_version = 2\nshow_arms = Hands \n").Arms);
            // An older profile file, as such a launcher wrote it (every setting, no preset line), loads its Show arms.
            using (var t = new TempDir())
            {
                var dir = t.Combine("profiles");
                Directory.CreateDirectory(dir);
                File.WriteAllText(Path.Combine(dir, "Old.ini"), "schema_version = 2\nturn = snap\nshow_arms = 0\n");
                var loaded = new ProfileStore(dir).Load("Old", new LauncherSettings());
                Assert.Equal(ArmsView.Hidden, loaded.Arms);
                Assert.Equal(TurnMode.Snap, loaded.Turn);
            }
        }

        [Fact]
        public void AHandsOnlyProfileRoundTrips()
        {
            using (var t = new TempDir())
            {
                var store = new ProfileStore(t.Combine("profiles"));
                foreach (var arms in new[] { ArmsView.HandsOnly, ArmsView.Hidden, ArmsView.Shown })
                {
                    store.Save("Gloves", new LauncherSettings { Arms = arms });
                    var text = File.ReadAllText(store.PathOf("Gloves"));
                    // The profile holds only the changes from Recommended, where the arms are shown.
                    Assert.Equal(arms != ArmsView.Shown, text.Contains("show_arms"));
                    Assert.Equal(arms, store.Load("Gloves", new LauncherSettings { Arms = ArmsView.Hidden }).Arms);
                }
            }
        }

        [Fact]
        public void ItAppliesWithMotionControllersAndSitsUnderTheWeaponHand()
        {
            Assert.Null(SettingRules.WhyNot(Setting.Arms, new LauncherSettings()));
            Assert.Null(SettingRules.WhyNot(Setting.Arms, new LauncherSettings { Arms = ArmsView.HandsOnly }));
            Assert.Equal(SettingRules.WhyNot(Setting.WeaponHand, new LauncherSettings { Controllers = false }),
                SettingRules.WhyNot(Setting.Arms, new LauncherSettings { Controllers = false }));
            Assert.Equal((int)Setting.WeaponHand + 1, (int)Setting.Arms);
            var text = SettingTexts.For(Setting.Arms);
            Assert.Equal("Show arms", text.Label);
            Assert.Equal(new[] { "Shown", "Hidden", "Hands only" }, text.Choices);
            Assert.Equal(System.Enum.GetValues(typeof(ArmsView)).Length, text.Choices.Count);
            Assert.DoesNotContain("\u2014", text.Tooltip);
        }
    }
}
