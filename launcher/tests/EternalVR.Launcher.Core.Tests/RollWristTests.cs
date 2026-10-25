using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>
    /// Roll wrist (the Play tab's Gestures, T-162): on by default and passed explicitly; and the settings the launcher no longer
    /// has (T-161, T-163): the holsters, the throw gesture, reach to use and rumble by hand are not read from an older file,
    /// dropped on the next save and not passed to the layer, which keeps its own defaults (rumble by hand on, reach to use off).
    /// </summary>
    public class RollWristTests
    {
        private static LaunchInputs Inputs(LauncherSettings s = null) => new LaunchInputs
        {
            GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
            LayerDir = @"E:\EternalVR\layer",
            LogDir = @"E:\data\logs\20261024-100000",
            Settings = s ?? new LauncherSettings(),
            ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
        };

        private static Dictionary<string, string> Env(LaunchPlan p) => p.Environment.ToDictionary(e => e.Key, e => e.Value);

        [Fact]
        public void RollWristIsOnByDefaultPassedExplicitlyAndRoundTrips()
        {
            Assert.True(new LauncherSettings().RollWrist);
            Assert.Equal("1", Env(LaunchPlanBuilder.Build(Inputs()))["ETERNALVR_WRIST_FLICK"]);
            Assert.Equal("0", Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { RollWrist = false })))["ETERNALVR_WRIST_FLICK"]);
            var off = new LauncherSettings { RollWrist = false };
            Assert.False(LauncherSettings.Parse(off.Serialize()).RollWrist);
            Assert.Contains("roll_wrist = 0", off.Serialize());
            Assert.Contains("roll_wrist = 1", new LauncherSettings().Serialize());
            Assert.True(off.WithDefaults().RollWrist);
            Assert.Empty(LauncherSettings.Parse("schema_version = 2\nroll_wrist = 0\n").UnknownKeys);
            Assert.Null(SettingRules.WhyNot(Setting.RollWrist, new LauncherSettings()));
            Assert.Equal(SettingRules.NeedsControllers, SettingRules.WhyNot(Setting.RollWrist, new LauncherSettings { Controllers = false }));
            var text = SettingTexts.For(Setting.RollWrist);
            Assert.Equal("Roll wrist", text.Label);
            Assert.DoesNotContain("flick", text.Tooltip, System.StringComparison.OrdinalIgnoreCase);
            Assert.DoesNotContain("\u2014", text.Tooltip);
            Assert.Equal(new[] { "roll_wrist" }, SettingKeys.Of(Setting.RollWrist));
        }

        [Fact]
        public void TheTestingBuildsWristFlickLineIsNotReadSoRollWristStartsOn()
        {
            var s = LauncherSettings.Parse("schema_version = 2\nwrist_flick = 0\n");
            Assert.True(s.RollWrist);
            Assert.Empty(s.UnknownKeys);
            Assert.DoesNotContain("wrist_flick", s.Serialize());
        }

        [Fact]
        public void RemovedSettingsAreNotReadNotPassedAndDropped()
        {
            var old = "schema_version = 2\nthrow_gesture = 1\nholsters = 1\nholster_shoulder = 8\nholster_other = off\nholster_hip = quick\n"
                + "reach_use = 1\nrumble_route = 0\n";
            var s = LauncherSettings.Parse(old);
            Assert.Empty(s.UnknownKeys);
            var written = s.Serialize();
            foreach (var key in new[] { "throw_gesture", "holsters", "holster_shoulder", "holster_other", "holster_hip", "reach_use", "rumble_route" })
                Assert.DoesNotContain(key, written);
            var env = Env(LaunchPlanBuilder.Build(Inputs(s)));
            foreach (var name in new[] { "ETERNALVR_THROW", "ETERNALVR_HOLSTERS", "ETERNALVR_HOLSTER_SHOULDER", "ETERNALVR_HOLSTER_OTHER",
                                         "ETERNALVR_HOLSTER_HIP", "ETERNALVR_REACH_USE", "ETERNALVR_RUMBLE_ROUTE" })
                Assert.False(env.ContainsKey(name), name + " is still passed");
            foreach (var name in new[] { "ThrowGesture", "Holsters", "HolsterShoulder", "HolsterOther", "HolsterHip", "ReachUse", "RumbleRoute", "WristFlick" })
                Assert.DoesNotContain(name, System.Enum.GetNames(typeof(Setting)));
        }
    }
}
