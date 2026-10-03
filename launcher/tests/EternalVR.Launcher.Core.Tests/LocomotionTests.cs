using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>"Move toward": where you look (default), where the left hand points, or where the right hand points.</summary>
    public class LocomotionTests
    {
        private static LaunchInputs Inputs(LauncherSettings s = null) => new LaunchInputs
        {
            GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
            LayerDir = @"E:\EternalVR\layer",
            LogDir = @"E:\data\logs\20261002-010203",
            Settings = s ?? new LauncherSettings(),
            ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
        };

        private static string Env(LauncherSettings s) =>
            LaunchPlanBuilder.Build(Inputs(s)).Environment.ToDictionary(e => e.Key, e => e.Value)["ETERNALVR_LOCOMOTION"];

        private static LocomotionMode Parsed(string file) => LauncherSettings.Parse("schema_version = 2\n" + file).Locomotion;

        [Fact]
        public void LookIsTheDefault()
        {
            Assert.Equal(LocomotionMode.Look, new LauncherSettings().Locomotion);
            Assert.Equal("look", Env(null));
            Assert.Contains("locomotion = look", new LauncherSettings().Serialize());
            Assert.Equal(LocomotionMode.Look, new LauncherSettings { Locomotion = LocomotionMode.RightHand }.WithDefaults().Locomotion);
        }

        [Fact]
        public void EachChoiceIsPassedAndRoundTripsWhateverTheWeaponHand()
        {
            var names = new Dictionary<LocomotionMode, string>
            {
                [LocomotionMode.Look] = "look",
                [LocomotionMode.LeftHand] = "left",
                [LocomotionMode.RightHand] = "right",
            };
            foreach (var hand in new[] { Handedness.Right, Handedness.Left, Handedness.LeftMirrored })
                foreach (var kv in names)
                {
                    var s = new LauncherSettings { Hand = hand, Locomotion = kv.Key };
                    Assert.Equal(kv.Value, Env(s));
                    Assert.Equal(kv.Value, Env(new LauncherSettings { Mode = VrMode.Mono, Hand = hand, Locomotion = kv.Key }));
                    var back = LauncherSettings.Parse(s.Serialize());
                    Assert.Equal(kv.Key, back.Locomotion);
                    Assert.Equal(hand, back.Hand);
                }
        }

        [Fact]
        public void AnOlderFileKeepsWhereItMoved()
        {
            // head was where you look.
            Assert.Equal(LocomotionMode.Look, Parsed("locomotion = head\n"));
            // hand was the hand with the move stick: the left one, or the right one with Left (buttons and sticks).
            Assert.Equal(LocomotionMode.LeftHand, Parsed("handedness = right\nlocomotion = hand\n"));
            Assert.Equal(LocomotionMode.LeftHand, Parsed("handedness = left\nlocomotion = hand\n"));
            Assert.Equal(LocomotionMode.RightHand, Parsed("handedness = left_mirror\nlocomotion = hand\n"));
            Assert.Equal(LocomotionMode.RightHand, Parsed("locomotion = Hand\nhandedness = left_mirror\n"));
            Assert.Equal(LocomotionMode.LeftHand, Parsed("locomotion = hand\n"));
            // A file without the key, or a value this version does not know: look.
            Assert.Equal(LocomotionMode.Look, Parsed("turn = snap\n"));
            Assert.Equal(LocomotionMode.Look, Parsed("locomotion = toward\n"));
            Assert.Equal(LocomotionMode.RightHand, Parsed("locomotion = RIGHT\n"));
            // The older value is written back in the new form.
            Assert.Contains("locomotion = right", LauncherSettings.Parse("schema_version = 2\nhandedness = left_mirror\nlocomotion = hand\n").Serialize());
            Assert.Empty(LauncherSettings.Parse("schema_version = 2\nlocomotion = hand\n").UnknownKeys);
        }

        [Fact]
        public void TheRowShowsThreeChoicesInEnumOrder()
        {
            var text = SettingTexts.For(Setting.MoveToward);
            Assert.Equal("Move toward", text.Label);
            Assert.Equal(new[] { "Where you look", "Where your left hand points", "Where your right hand points" }, text.Choices);
            Assert.Equal(new[] { LocomotionMode.Look, LocomotionMode.LeftHand, LocomotionMode.RightHand },
                (LocomotionMode[])System.Enum.GetValues(typeof(LocomotionMode)));
            Assert.DoesNotContain("\u2014", text.Tooltip);
        }
    }
}
