using System;
using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Safety;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>The one-time camera shake repair in the player's profile (ETERNALVR_PROFILE_REPAIR, docs/rig-findings/profile-leak.md).</summary>
    public class ProfileRepairTests
    {
        private const string HeldLine =
            "[   17.079] [75700] cvars: held at run time: r_TAASafeMode 1, r_antialiasing 0, pm_noBob 1, view_skipKicks 1, view_skipShakes 1, "
            + "hands_fovScale 1, swf_platformOverride 2";

        // The layer's line when its profile hooks are in (vkcore/profile_hooks.cpp, after the log's prefix).
        private const string HookedLine =
            "[    0.412] [38288] profile: profile cvar serializer hooked at RVA 0x141D040: the profile keeps the player's "
            + "view_skipShakes and r_sharpening while the layer holds them";

        private static LaunchInputs Inputs(VrMode mode, bool repair) => new LaunchInputs
        {
            GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
            LayerDir = @"E:\EternalVR\layer",
            LogDir = @"E:\data\logs\20261012-153805",
            Settings = new LauncherSettings { Mode = mode },
            ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
            ProfileRepair = repair,
        };

        private static Dictionary<string, string> Env(LaunchPlan p) => p.Environment.ToDictionary(e => e.Key, e => e.Value);

        [Fact]
        public void TheHoldLineIsRecognised()
        {
            Assert.True(ProfileRepair.ShowsShakesHeld(HeldLine));
            Assert.False(ProfileRepair.ShowsShakesHeld("cvars: held at run time: view_skipKicks 1, pm_noBob 1"));
            Assert.False(ProfileRepair.ShowsShakesHeld("cvars: held at run time: view_skipShakes 10"));
            Assert.False(ProfileRepair.ShowsShakesHeld("cvars: view_skipShakes 0 -> 1 (reads 1); held in VR"));
            Assert.False(ProfileRepair.ShowsShakesHeld(null));
            Assert.True(ProfileRepair.ShowsHooked(HookedLine));
            Assert.False(ProfileRepair.ShowsHooked("[    0.412] [38288] profile: not hooked; the game saves held values (view_skipShakes 1) into the player's profile"));
            Assert.False(ProfileRepair.ShowsHooked("profile: serializer hook failed: no room; the game saves held values into the player's profile"));
        }

        [Fact]
        public void OnlyALogWithoutTheHooksLeftShakesInTheProfile()
        {
            Assert.True(ProfileRepair.LeftShakesHeld(new[] { "layer start", HeldLine })); // a build before the hooks
            Assert.True(ProfileRepair.LeftShakesHeld(new[] { "profile: not hooked; ...", HeldLine })); // hooks that did not go in
            // Positive control: the same hold with the hooks in did not leak, whatever the order of the lines.
            Assert.False(ProfileRepair.LeftShakesHeld(new[] { "layer start", HookedLine, HeldLine }));
            Assert.False(ProfileRepair.LeftShakesHeld(new[] { HeldLine, HookedLine }));
            Assert.False(ProfileRepair.LeftShakesHeld(new[] { HookedLine }));
            Assert.False(ProfileRepair.LeftShakesHeld(new[] { "cvars: held at run time: r_dof 0" }));
            Assert.False(ProfileRepair.LeftShakesHeld(null));
        }

        [Fact]
        public void AskedOnlyInStereoWithoutTheMarkerAndAfterASessionThatLeftShakes()
        {
            bool scanned = false;
            Assert.False(ProfileRepair.Wanted(true, true, () => { scanned = true; return true; }));
            Assert.False(scanned); // the marker settles it without reading any log
            Assert.False(ProfileRepair.Wanted(false, false, () => { scanned = true; return true; }));
            Assert.False(scanned); // a mono launch reads no log either
            Assert.True(ProfileRepair.Wanted(true, false, () => true));
            Assert.False(ProfileRepair.Wanted(true, false, () => false)); // a fresh install, or mono only
        }

        [Fact]
        public void TheSessionLogsAreScannedExceptTheCurrentSession()
        {
            using (var t = new TempDir())
            {
                var logs = t.Combine("logs");
                t.Write("logs/20261001-100000/eternalvr-20261001-100001.log", "layer start\ncvars: held at run time: r_dof 0\n");
                Assert.False(ProfileRepair.AnySessionLeftShakes(logs));
                t.Write("logs/20261003-100000/eternalvr-20261003-100001.log", "layer start\n" + HookedLine + "\n" + HeldLine + "\n");
                Assert.False(ProfileRepair.AnySessionLeftShakes(logs)); // held with the hooks in: nothing leaked
                t.Write("logs/20261002-100000/eternalvr-20261002-100001.log", "layer start\n" + HeldLine + "\n");
                Assert.True(ProfileRepair.AnySessionLeftShakes(logs));
                Assert.False(ProfileRepair.AnySessionLeftShakes(logs, t.Combine("logs", "20261002-100000")));
                t.Write("logs/launcher.log", HeldLine + "\n"); // not a session's layer log
                Assert.False(ProfileRepair.AnySessionLeftShakes(logs, t.Combine("logs", "20261002-100000")));
                Assert.False(ProfileRepair.AnySessionLeftShakes(t.Combine("missing")));
            }
        }

        [Fact]
        public void ALogsFolderThatCannotBeListedAsksForNothing()
        {
            using (var t = new TempDir())
            {
                t.Write("logs/20261002-100000/eternalvr-20261002-100001.log", "layer start\n" + HeldLine + "\n");
                var logs = t.Combine("logs");
                Assert.True(ProfileRepair.AnySessionLeftShakes(logs, null, System.IO.Directory.GetDirectories)); // the control
                Assert.False(ProfileRepair.AnySessionLeftShakes(logs, null, d => throw new UnauthorizedAccessException(d)));
                Assert.False(ProfileRepair.AnySessionLeftShakes(logs, null, d => throw new System.IO.DirectoryNotFoundException(d)));
                Assert.False(ProfileRepair.AnySessionLeftShakes(logs, null, d => new[] { t.Combine("logs", "gone") }));
                // A session folder that cannot be listed is skipped; the leaking session after it is still found.
                var leaking = t.Combine("logs", "20261002-100000");
                Assert.True(ProfileRepair.AnySessionLeftShakes(logs, null, d => new[] { t.Combine("logs", "gone"), leaking }));
            }
        }

        [Fact]
        public void TheVariableIsSetInStereoOnly()
        {
            Assert.Equal("1", Env(LaunchPlanBuilder.Build(Inputs(VrMode.Stereo, true)))[ProfileRepair.Variable]);
            Assert.False(Env(LaunchPlanBuilder.Build(Inputs(VrMode.Stereo, false))).ContainsKey(ProfileRepair.Variable));
            Assert.False(Env(LaunchPlanBuilder.Build(Inputs(VrMode.Mono, true))).ContainsKey(ProfileRepair.Variable));
        }

        [Fact]
        public void TheLayersAnswerSettlesIt()
        {
            Assert.True(ProfileRepair.Settles("done"));
            Assert.True(ProfileRepair.Settles("not_needed"));
            Assert.True(ProfileRepair.Settles("off"));
            Assert.False(ProfileRepair.Settles(null)); // no answer: mono, an older layer, a session that ended before the load
            Assert.False(ProfileRepair.Settles(""));
            Assert.False(ProfileRepair.Settles("pending"));

            Assert.Equal("done", ProfileRepair.StatusValue("state=vr\r\nstereo=on\r\nprofile_repair=done\r\n"));
            Assert.Equal("not_needed", ProfileRepair.StatusValue("profile_repair = not_needed"));
            Assert.Null(ProfileRepair.StatusValue("state=vr\nstereo=on\n"));
            Assert.Null(ProfileRepair.StatusValue(null));

            var text = ProfileRepair.MarkerText(new DateTime(2026, 10, 12, 16, 0, 0), "20261012-160000", "done");
            Assert.Contains("profile_repair=done", text);
            Assert.Contains("session=20261012-160000", text);
        }
    }
}
