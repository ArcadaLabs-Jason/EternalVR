using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>"Glory kills": follow the camera (default), a steady view, a flat screen or a fade.</summary>
    public class GloryKillsTests
    {
        private static readonly GloryKillView[] AllViews =
            { GloryKillView.Follow, GloryKillView.Steady, GloryKillView.Screen, GloryKillView.Fade };

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
        public void FollowIsTheDefaultAndIsPassedExplicitly()
        {
            Assert.Equal(GloryKillView.Follow, new LauncherSettings().GloryKills);
            Assert.Equal("follow", Env(LaunchPlanBuilder.Build(Inputs()))["ETERNALVR_GLORY_KILLS"]);
        }

        [Fact]
        public void EachViewIsPassedInStereoAndMono()
        {
            var expected = new Dictionary<GloryKillView, string>
            {
                [GloryKillView.Follow] = "follow",
                [GloryKillView.Steady] = "steady",
                [GloryKillView.Fade] = "fade",
                [GloryKillView.Screen] = "screen",
            };
            foreach (var kv in expected)
            {
                Assert.Equal(kv.Value, Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { GloryKills = kv.Key })))["ETERNALVR_GLORY_KILLS"]);
                var mono = new LauncherSettings { Mode = VrMode.Mono, GloryKills = kv.Key };
                Assert.Equal(kv.Value, Env(LaunchPlanBuilder.Build(Inputs(mono)))["ETERNALVR_GLORY_KILLS"]);
            }
        }

        [Fact]
        public void TheSettingRoundTripsAndAnOlderFileTakesFollow()
        {
            foreach (var view in AllViews)
                Assert.Equal(view, LauncherSettings.Parse(new LauncherSettings { GloryKills = view }.Serialize()).GloryKills);
            Assert.Contains("glory_kills = steady", new LauncherSettings { GloryKills = GloryKillView.Steady }.Serialize());
            Assert.Contains("glory_kills = follow", new LauncherSettings().Serialize());
            // A file from before the key, or a value this version does not know: follow.
            Assert.Equal(GloryKillView.Follow, LauncherSettings.Parse("schema_version = 2\nvignette = light\n").GloryKills);
            Assert.Equal(GloryKillView.Follow, LauncherSettings.Parse("schema_version = 1\naim = view\n").GloryKills);
            Assert.Equal(GloryKillView.Follow, LauncherSettings.Parse("schema_version = 2\nglory_kills = blink\n").GloryKills);
            Assert.Equal(GloryKillView.Screen, LauncherSettings.Parse("schema_version = 2\nglory_kills = Screen\n").GloryKills);
            // A known key: not kept among the unknown ones, so it is not written twice.
            Assert.Empty(LauncherSettings.Parse("schema_version = 2\nglory_kills = fade\n").UnknownKeys);
            Assert.Equal(GloryKillView.Follow, new LauncherSettings { GloryKills = GloryKillView.Fade }.WithDefaults().GloryKills);
        }

        [Fact]
        public void ItNeedsTheControllers()
        {
            Assert.Null(SettingRules.WhyNot(Setting.GloryKills, new LauncherSettings()));
            Assert.Null(SettingRules.WhyNot(Setting.GloryKills, new LauncherSettings { Mode = VrMode.Mono }));
            Assert.Equal(SettingRules.NeedsControllers, SettingRules.WhyNot(Setting.GloryKills, new LauncherSettings { Controllers = false }));
        }

        [Fact]
        public void TheRowShowsItsChoicesInEnumOrder()
        {
            var text = SettingTexts.For(Setting.GloryKills);
            Assert.Equal("Glory kills", text.Label);
            Assert.Equal(new[] { "Follow the camera (intense)", "Steady view", "Flat screen", "Fade out" }, text.Choices);
            // The window maps the list's index to the enum value: the default first, Fade out last.
            Assert.Equal(new[] { GloryKillView.Follow, GloryKillView.Steady, GloryKillView.Screen, GloryKillView.Fade },
                (GloryKillView[])System.Enum.GetValues(typeof(GloryKillView)));
            Assert.DoesNotContain("\u2014", text.Tooltip);
        }
    }
}
