using System.Collections.Generic;
using System.IO;
using System.Linq;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>The player's controls folder (Edit controls) and the launch passing it to the layer.</summary>
    public class ControlsFolderTests
    {
        private static readonly string[] BuiltInMaps =
        {
            "hp_reverb_g2.toml", "htc_vive_cosmos.toml", "htc_vive_wand.toml", "oculus_touch.toml", "pico4.toml", "steam_frame.toml",
            "valve_index.toml", "windows_mixed_reality.toml",
        };

        private static string ShippedMaps => Path.Combine(TestData.Dir, "controllers");

        private static IEnumerable<string> Names(string dir) => Directory.GetFiles(dir).Select(Path.GetFileName).OrderBy(n => n);

        private static LaunchInputs Inputs(ControlsFolder controls, LauncherSettings s = null) => new LaunchInputs
        {
            GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
            LayerDir = @"E:\EternalVR\layer",
            LogDir = @"E:\data\logs\20260926-010203",
            Settings = s ?? new LauncherSettings(),
            ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
            Controls = controls,
        };

        private static Dictionary<string, string> Env(LaunchPlan p) => p.Environment.ToDictionary(e => e.Key, e => e.Value);

        [Fact]
        public void TheFolderIsInTheDataFolder()
        {
            using (var t = new TempDir())
                Assert.Equal(t.Combine("controls"), new DataPaths(t.Path).Controls);
        }

        [Fact]
        public void PrepareWritesTheReadmeAndACopyOfEveryBuiltInMap()
        {
            using (var t = new TempDir())
            {
                var controls = new ControlsFolder(t.Combine("controls"));
                Assert.False(Directory.Exists(controls.Dir));
                controls.Prepare(ShippedMaps);
                Assert.Equal(new[] { ControlsFolder.ReadmeName }, Names(controls.Dir));
                Assert.Equal(BuiltInMaps, Names(controls.DefaultsDir));
                Assert.Equal(File.ReadAllBytes(Path.Combine(ShippedMaps, "oculus_touch.toml")),
                    File.ReadAllBytes(Path.Combine(controls.DefaultsDir, "oculus_touch.toml")));
                Assert.Equal(ControlsFolder.Readme, File.ReadAllText(controls.ReadmeFile));
                // The copies alone are not the player's maps.
                Assert.False(controls.HasPlayerMaps);
            }
        }

        [Fact]
        public void PrepareRefreshesTheDefaultsAndLeavesThePlayersFilesAlone()
        {
            using (var t = new TempDir())
            {
                var source = t.Combine("shipped");
                t.Write("shipped/oculus_touch.toml", "new touch");
                t.Write("shipped/pico4.toml", "new pico");
                t.Write("controls/defaults/oculus_touch.toml", "edited by mistake");
                t.Write("controls/defaults/retired.toml", "no longer built in");
                t.Write("controls/README.txt", "old words");
                var mine = t.Write("controls/oculus_touch.toml", "my touch");
                t.Write("controls/notes.txt", "my notes");
                var controls = new ControlsFolder(t.Combine("controls"));
                File.SetAttributes(Path.Combine(controls.DefaultsDir, "oculus_touch.toml"), FileAttributes.ReadOnly);
                controls.Prepare(source);
                Assert.Equal(new[] { "oculus_touch.toml", "pico4.toml" }, Names(controls.DefaultsDir));
                Assert.Equal("new touch", t.Read("controls/defaults/oculus_touch.toml"));
                Assert.Equal(ControlsFolder.Readme, t.Read("controls/README.txt"));
                Assert.Equal("my touch", File.ReadAllText(mine));
                Assert.Equal("my notes", t.Read("controls/notes.txt"));
            }
        }

        [Fact]
        public void AMissingSourceChangesNothing()
        {
            using (var t = new TempDir())
            {
                var controls = new ControlsFolder(t.Combine("controls"));
                Assert.ThrowsAny<IOException>(() => controls.Prepare(t.Combine("missing")));
                Assert.False(Directory.Exists(controls.Dir));
            }
        }

        [Fact]
        public void OnlyTomlFilesDirectlyInTheFolderArePlayerMaps()
        {
            using (var t = new TempDir())
            {
                var controls = new ControlsFolder(t.Combine("controls"));
                Assert.False(controls.HasPlayerMaps); // no folder yet
                t.Write("controls/README.txt", "words");
                t.Write("controls/defaults/oculus_touch.toml", "built in");
                t.Write("controls/old/oculus_touch.toml", "kept aside");
                t.Write("controls/oculus_touch.toml.bak", "a backup");
                Assert.False(controls.HasPlayerMaps);
                t.Write("controls/My Touch.TOML", "mine");
                Assert.True(controls.HasPlayerMaps);
            }
        }

        [Fact]
        public void TheLaunchPassesTheFolderOnlyWithPlayerMapsAndControllers()
        {
            using (var t = new TempDir())
            {
                var controls = new ControlsFolder(t.Combine("controls"));
                controls.Prepare(ShippedMaps);
                Assert.False(Env(LaunchPlanBuilder.Build(Inputs(controls))).ContainsKey("ETERNALVR_CONTROLLER_DATA"));
                Assert.False(Env(LaunchPlanBuilder.Build(Inputs(null))).ContainsKey("ETERNALVR_CONTROLLER_DATA"));
                File.Copy(Path.Combine(controls.DefaultsDir, "oculus_touch.toml"), Path.Combine(controls.Dir, "oculus_touch.toml"));
                Assert.Equal(controls.Dir, Env(LaunchPlanBuilder.Build(Inputs(controls)))["ETERNALVR_CONTROLLER_DATA"]);
                var keyboard = new LauncherSettings { Controllers = false };
                Assert.False(Env(LaunchPlanBuilder.Build(Inputs(controls, keyboard))).ContainsKey("ETERNALVR_CONTROLLER_DATA"));
            }
        }
    }
}
