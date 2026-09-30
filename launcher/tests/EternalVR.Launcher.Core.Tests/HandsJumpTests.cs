using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>"Jump with both hands" (Gestures): off by default, never when sitting.</summary>
    public class HandsJumpTests
    {
        private static LaunchInputs Inputs(LauncherSettings s = null) => new LaunchInputs
        {
            GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
            LayerDir = @"E:\EternalVR\layer",
            LogDir = @"E:\data\logs\20260930-130000",
            Settings = s ?? new LauncherSettings(),
            ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
        };

        private static Dictionary<string, string> Env(LaunchPlan p) => p.Environment.ToDictionary(e => e.Key, e => e.Value);

        [Fact]
        public void OffByDefaultAndPassedExplicitly()
        {
            Assert.False(new LauncherSettings().HandsJump);
            Assert.Equal("0", Env(LaunchPlanBuilder.Build(Inputs()))["ETERNALVR_HANDS_JUMP"]);
            Assert.Equal("1", Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { HandsJump = true })))["ETERNALVR_HANDS_JUMP"]);
        }

        [Fact]
        public void TheSettingRoundTripsWithTheOtherGestures()
        {
            var s = LauncherSettings.Parse(new LauncherSettings { HandsJump = true, ThrowGesture = true }.Serialize());
            Assert.True(s.HandsJump);
            Assert.True(s.ThrowGesture);
            Assert.False(s.SwingGesture);
            Assert.Contains("hands_jump = 1", new LauncherSettings { HandsJump = true }.Serialize());
            Assert.False(LauncherSettings.Parse("schema_version = 2\nthrow_gesture = 1\n").HandsJump);
            Assert.Empty(LauncherSettings.Parse("schema_version = 2\nhands_jump = 1\n").UnknownKeys);
            Assert.False(new LauncherSettings { HandsJump = true }.WithDefaults().HandsJump);
        }

        [Fact]
        public void ItNeedsTheControllersAndStanding()
        {
            Assert.Null(SettingRules.WhyNot(Setting.HandsJump, new LauncherSettings()));
            Assert.Equal(SettingRules.NeedsControllers, SettingRules.WhyNot(Setting.HandsJump, new LauncherSettings { Controllers = false }));
            Assert.Equal(SettingRules.NotSitting, SettingRules.WhyNot(Setting.HandsJump, new LauncherSettings { Posture = PostureMode.Seated }));
            var text = SettingTexts.For(Setting.HandsJump);
            Assert.Equal("Jump with both hands", text.Label);
            Assert.DoesNotContain("\u2014", text.Tooltip);
        }
    }
}
