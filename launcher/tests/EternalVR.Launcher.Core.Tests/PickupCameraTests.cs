using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>"Pickup animations": the view still (default) or the camera's motion on top of the head.</summary>
    public class PickupCameraTests
    {
        private static LaunchInputs Inputs(LauncherSettings s = null) => new LaunchInputs
        {
            GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
            LayerDir = @"E:\EternalVR\layer",
            LogDir = @"E:\data\logs\20261111-010203",
            Settings = s ?? new LauncherSettings(),
            ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
        };

        private static Dictionary<string, string> Env(LaunchPlan p) => p.Environment.ToDictionary(e => e.Key, e => e.Value);

        [Fact]
        public void StillIsTheDefaultAndIsPassedExplicitly()
        {
            Assert.Equal(PickupCameraView.Still, new LauncherSettings().PickupCamera);
            Assert.Equal("still", Env(LaunchPlanBuilder.Build(Inputs()))["ETERNALVR_PICKUP_CAMERA"]);
            Assert.Equal(PickupCameraView.Still, SettingsPresets.Recommended.Values().PickupCamera);
        }

        [Fact]
        public void EachViewIsPassedInStereoAndMono()
        {
            var expected = new Dictionary<PickupCameraView, string>
            {
                [PickupCameraView.Still] = "still",
                [PickupCameraView.Play] = "play",
            };
            foreach (var kv in expected)
            {
                Assert.Equal(kv.Value, Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { PickupCamera = kv.Key })))["ETERNALVR_PICKUP_CAMERA"]);
                var mono = new LauncherSettings { Mode = VrMode.Mono, PickupCamera = kv.Key };
                Assert.Equal(kv.Value, Env(LaunchPlanBuilder.Build(Inputs(mono)))["ETERNALVR_PICKUP_CAMERA"]);
            }
        }

        [Fact]
        public void TheSettingRoundTripsAndAnOlderFileTakesStill()
        {
            foreach (var view in new[] { PickupCameraView.Still, PickupCameraView.Play })
                Assert.Equal(view, LauncherSettings.Parse(new LauncherSettings { PickupCamera = view }.Serialize()).PickupCamera);
            Assert.Contains("pickup_camera = play", new LauncherSettings { PickupCamera = PickupCameraView.Play }.Serialize());
            Assert.Contains("pickup_camera = still", new LauncherSettings().Serialize());
            // A file from before the key, or a value this version does not know: still.
            Assert.Equal(PickupCameraView.Still, LauncherSettings.Parse("schema_version = 2\nglory_kills = steady\n").PickupCamera);
            Assert.Equal(PickupCameraView.Still, LauncherSettings.Parse("schema_version = 2\npickup_camera = follow\n").PickupCamera);
            Assert.Equal(PickupCameraView.Play, LauncherSettings.Parse("schema_version = 2\npickup_camera = Play\n").PickupCamera);
            // A known key: not kept among the unknown ones, so it is not written twice.
            Assert.Empty(LauncherSettings.Parse("schema_version = 2\npickup_camera = play\n").UnknownKeys);
            Assert.Equal(PickupCameraView.Still, new LauncherSettings { PickupCamera = PickupCameraView.Play }.WithDefaults().PickupCamera);
        }

        [Fact]
        public void ItAppliesWithTheControllersOrWithout()
        {
            Assert.Null(SettingRules.WhyNot(Setting.PickupCamera, new LauncherSettings()));
            Assert.Null(SettingRules.WhyNot(Setting.PickupCamera, new LauncherSettings { Mode = VrMode.Mono }));
            // Gamepad mode: the layer tells a pickup from the game's sync animation, not the controllers.
            Assert.Null(SettingRules.WhyNot(Setting.PickupCamera, new LauncherSettings { Controllers = false }));
        }

        [Fact]
        public void TheRowShowsItsChoicesInEnumOrder()
        {
            var text = SettingTexts.For(Setting.PickupCamera);
            Assert.Equal("Pickup animations", text.Label);
            Assert.Equal(new[] { "Still view", "Camera moves (intense)" }, text.Choices);
            Assert.Equal(new[] { PickupCameraView.Still, PickupCameraView.Play },
                (PickupCameraView[])System.Enum.GetValues(typeof(PickupCameraView)));
            Assert.DoesNotContain("\u2014", text.Tooltip);
        }
    }
}
