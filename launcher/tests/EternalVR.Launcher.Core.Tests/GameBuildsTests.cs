using System.IO;
using System.Linq;
using EternalVR.Launcher.Core.Game;
using EternalVR.Launcher.Core.Preflight;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>
    /// Game builds (ROADMAP M8, REQ-08, T-094): a build the layer does not know keeps Launch VR off and names the supported
    /// builds, from the exe on disk through the check to the status line.
    /// </summary>
    public class GameBuildsTests
    {
        private static PreflightFacts Facts(string gameRoot, BuildCheck build, KnownBuilds known) => new PreflightFacts
        {
            SteamRunning = true,
            SteamLoggedIn = true,
            GameRoot = gameRoot,
            Build = build,
            SteamBuildId = ShippedSteamBuild,
            KnownBuildIds = known.IdsFor(GamePlatform.Steam),
            LayerDir = @"E:\EternalVR\layer",
            LayerManifestExists = true,
            LayerLibraryExists = true,
            LauncherVersion = "0.1.0+0123456789abcdef0123456789abcdef01234567",
            LayerVersion = "0.1.0",
            RuntimeManifest = @"C:\runtime.json",
            RuntimeManifestExists = true,
            SettingsLocationCount = 2,
        };

        private static KnownBuilds Shipped => KnownBuilds.Parse(TestData.Read("known-builds.txt"));

        /// <summary>The Steam build the shipped list supports (Steam still names it when the exe is patched).</summary>
        private static string ShippedSteamBuild => Shipped.IdsFor(GamePlatform.Steam).First();

        /// <summary>The shipped list, with a stand-in exe's hash added as the supported Steam build.</summary>
        private static KnownBuilds ShippedPlus(string exe) =>
            new KnownBuilds(Shipped.All
                .Concat(new[] { new KnownBuild(KnownBuilds.Sha256OfFile(exe), ShippedSteamBuild, "stand-in for the retail exe") }));

        [Fact]
        public void APatchedExeKeepsLaunchVrOffAndNamesTheSupportedBuilds()
        {
            using (var t = new TempDir())
            {
                var exe = t.Write("game/" + GameLayout.RetailExe, "the retail exe");
                var known = ShippedPlus(exe);
                var before = PreflightEvaluator.Evaluate(Facts(t.Combine("game"), known.Check(exe), known));
                Assert.True(before.CanLaunch);
                Assert.Null(LaunchBlock.Status(before));

                // One byte patched: Steam still names the same build, the hash no longer matches.
                var written = File.GetLastWriteTimeUtc(exe);
                File.WriteAllText(exe, "the retail exf");
                File.SetLastWriteTimeUtc(exe, written.AddSeconds(2)); // a patch is a write: the check hashes it again
                var check = known.Check(exe);
                Assert.Equal(BuildStatus.Unknown, check.Status);
                var result = PreflightEvaluator.Evaluate(Facts(t.Combine("game"), check, known));

                Assert.False(result.CanLaunch);
                var failed = Assert.Single(result.Failures);
                Assert.Equal("game-build", failed.Id);
                Assert.Contains("SHA-256 " + KnownBuilds.Sha256OfFile(exe), failed.Message);
                Assert.Contains("This EternalVR supports build " + string.Join(", ", known.IdsFor(GamePlatform.Steam)) + ";", failed.Message);
                Assert.Contains("VR would stay off", failed.Message);
                Assert.Equal("Launch VR is off: this version of DOOM Eternal is not supported yet (see Checks and log).", LaunchBlock.Status(result));
            }
        }

        [Fact]
        public void ARenamedExeKeepsLaunchVrOffAndSaysWhatIsMissing()
        {
            using (var t = new TempDir())
            {
                var exe = t.Write("game/" + GameLayout.RetailExe, "the retail exe");
                var known = ShippedPlus(exe);
                File.Move(exe, t.Combine("game", "DOOMEternalx64vk-renamed.exe"));

                var check = known.Check(exe);
                Assert.Equal(BuildStatus.Missing, check.Status);
                var result = PreflightEvaluator.Evaluate(Facts(t.Combine("game"), check, known));

                Assert.False(result.CanLaunch);
                var failed = Assert.Single(result.Failures);
                Assert.Equal("game", failed.Id);
                Assert.Equal(GameLayout.RetailExe + " was not found in " + t.Combine("game") + ".", failed.Message);
            }
        }

        [Fact]
        public void TheShippedListNamesEveryBuildInTheRefusal()
        {
            var shipped = KnownBuilds.Parse(TestData.Read("known-builds.txt"));
            var f = Facts(@"D:\Games\DOOMEternal", new BuildCheck(BuildStatus.Unknown, new string('c', 64), null), shipped);
            var message = PreflightEvaluator.Evaluate(f).Failures.Single().Message;
            foreach (var id in shipped.IdsFor(GamePlatform.Steam)) Assert.Contains(id, message);
            Assert.NotEmpty(shipped.IdsFor(GamePlatform.Steam));
        }
    }
}
