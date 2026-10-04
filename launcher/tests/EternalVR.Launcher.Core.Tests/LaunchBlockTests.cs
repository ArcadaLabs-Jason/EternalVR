using System.Linq;
using EternalVR.Launcher.Core.Game;
using EternalVR.Launcher.Core.Preflight;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>The status line's reason when the checks keep Launch VR off (<see cref="LaunchBlock"/>).</summary>
    public class LaunchBlockTests
    {
        private static PreflightFacts Good() => new PreflightFacts
        {
            SteamRunning = true,
            SteamLoggedIn = true,
            GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
            Build = new BuildCheck(BuildStatus.Known, "ab", new KnownBuild(new string('a', 64), "25216728", "test")),
            LayerDir = @"E:\EternalVR\layer",
            LayerManifestExists = true,
            LayerLibraryExists = true,
            LauncherVersion = "0.1.0+0123456789abcdef0123456789abcdef01234567",
            LayerVersion = "0.1.0",
            RuntimeManifest = @"E:\runtime.json",
            RuntimeManifestExists = true,
            HagsMode = 1,
            SettingsLocationCount = 2,
        };

        private static string StatusWith(System.Action<PreflightFacts> change)
        {
            var f = Good();
            change(f);
            return LaunchBlock.Status(PreflightEvaluator.Evaluate(f));
        }

        [Fact]
        public void TheDoomEternalLauncherAloneIsNamed()
        {
            Assert.Equal("Launch VR is off: close the DOOM Eternal Launcher first (see Checks and log).",
                StatusWith(f => f.GameProcessesRunning = new[] { "idTechLauncher" }));
            Assert.Equal("Launch VR is off: DOOM Eternal is already running (see Checks and log).",
                StatusWith(f => f.GameProcessesRunning = new[] { "idTechLauncher", "DOOMEternalx64vk" }));
        }

        [Fact]
        public void NothingIsSaidWhenLaunchVrIsOn()
        {
            Assert.Null(LaunchBlock.Status(PreflightEvaluator.Evaluate(Good())));
            // Warnings do not turn it off.
            Assert.Null(StatusWith(f => f.HagsMode = 2));
            Assert.Null(LaunchBlock.Status(null));
        }

        [Fact]
        public void TheFirstFailureIsSaidInPlainWords()
        {
            Assert.Equal("Launch VR is off: the EternalVR layer's files are missing (see Checks and log).", StatusWith(f => f.LayerLibraryExists = false));
            Assert.Equal("Launch VR is off: Steam is not running (see Checks and log).", StatusWith(f => f.SteamRunning = false));
            Assert.Equal("Launch VR is off: no one is logged in to Steam (see Checks and log).", StatusWith(f => f.SteamLoggedIn = false));
            Assert.Equal("Launch VR is off: no OpenXR runtime is set (see Checks and log).", StatusWith(f => f.RuntimeManifest = null));
            Assert.Equal("Launch VR is off: the OpenXR runtime's file is missing (see Checks and log).", StatusWith(f => f.RuntimeManifestExists = false));
            // Two failures: the first in the checks' order.
            Assert.Equal("Launch VR is off: Steam is not running (see Checks and log).",
                StatusWith(f => { f.SteamRunning = false; f.LayerLibraryExists = false; }));
        }

        [Theory]
        [MemberData(nameof(PreflightTests.Refusals), MemberType = typeof(PreflightTests))]
        public void EveryRefusalHasAShortReason(string id, System.Action<PreflightFacts> change)
        {
            var f = Good();
            change(f);
            var failed = PreflightEvaluator.Evaluate(f).Failures.First();
            Assert.Equal(id, failed.Id);
            var reason = LaunchBlock.Reason(failed);
            Assert.True(reason.Length < 80, reason);
            Assert.DoesNotContain(@"E:\", reason);
        }

        [Fact]
        public void ACheckWithoutItsOwnWordsGivesItsFirstSentence()
        {
            Assert.Equal("the thing is broken", LaunchBlock.Reason(new Check("new-check", Severity.Fail, "The thing is broken. Fix it like this.")));
            Assert.Equal("DOOM is odd", LaunchBlock.Reason(new Check("new-check", Severity.Fail, "DOOM is odd.")));
        }
    }
}
