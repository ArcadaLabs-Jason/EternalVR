using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using EternalVR.Launcher.Core.Game;
using EternalVR.Launcher.Core.Preflight;
using EternalVR.Launcher.Core.Safety;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    public class GamePassTests
    {
        /// <summary>The Identity line of the rig's Game Pass install (Content\MicrosoftGame.Config, 2026-09-29).</summary>
        private const string RigConfig = "<?xml version=\"1.0\" encoding=\"utf-8\"?><Game configVersion=\"0\">"
            + "<Identity Name=\"BethesdaSoftworks.DOOMEternal-PC\" Publisher=\"CN=21E520D9-F467-4438-A16E-79ADDBE4ECB1\" Version=\"1.0.56.0\" />"
            + "<ExecutableList><Executable Name=\"launcher/idTechLauncher.exe\" Id=\"Game\" TargetDeviceFamily=\"PC\" /></ExecutableList></Game>";

        private static string Config(string name, string version) =>
            $"<Game configVersion=\"0\"><Identity Name=\"{name}\" Publisher=\"CN=X\" Version=\"{version}\" /></Game>";

        private static byte[] GamingRoot(params string[] folders)
        {
            var bytes = new List<byte>(Encoding.ASCII.GetBytes("RGBX"));
            bytes.AddRange(BitConverter.GetBytes(folders.Length));
            foreach (var f in folders) bytes.AddRange(Encoding.Unicode.GetBytes(f + "\0"));
            return bytes.ToArray();
        }

        /// <summary>A Game Pass install under <paramref name="folder"/> of a fake drive; returns its Content folder.</summary>
        private static string Install(TempDir t, string drive, string folder, string game, string config, bool exe = true)
        {
            var content = t.Combine(drive, folder, game, GamePassInstall.ContentFolder);
            t.Write($"{drive}/{folder}/{game}/Content/{GamePassInstall.ConfigFile}", config);
            if (exe) t.Write($"{drive}/{folder}/{game}/Content/{GameLayout.RetailExe}", "unreadable on the real install");
            return content;
        }

        [Fact]
        public void TheGamingRootFileNamesItsFolders()
        {
            // The bytes of the rig's E:\.GamingRoot.
            var rig = new byte[] { 0x52, 0x47, 0x42, 0x58, 0x01, 0, 0, 0 }.Concat(Encoding.Unicode.GetBytes("XboxGames\0")).ToArray();
            Assert.Equal(new[] { "XboxGames" }, GamePassInstall.ParseGamingRoot(rig));
            Assert.Equal(new[] { "Games", "More Games" }, GamePassInstall.ParseGamingRoot(GamingRoot("Games", "\\More Games")));
            Assert.Empty(GamePassInstall.ParseGamingRoot(Encoding.ASCII.GetBytes("nothing here")));
            Assert.Empty(GamePassInstall.ParseGamingRoot(null));
        }

        [Fact]
        public void TheIdentityIsReadFromTheConfig()
        {
            var id = GamePassInstall.ParseIdentity(RigConfig);
            Assert.Equal(GamePassInstall.PackageName, id.Name);
            Assert.Equal("1.0.56.0", id.Version);
            Assert.Equal("CN=21E520D9-F467-4438-A16E-79ADDBE4ECB1", id.Publisher);
            Assert.True(GamePassInstall.IsDoomEternal(id));
            Assert.False(GamePassInstall.IsDoomEternal(GamePassInstall.ParseIdentity(Config("Other.Game", "1.0.0.0"))));
            Assert.Null(GamePassInstall.ParseIdentity("<Game/>"));
            Assert.Null(GamePassInstall.ParseIdentity("not xml"));
        }

        [Fact]
        public void TheInstallIsFoundThroughTheGamingRootFolders()
        {
            using (var t = new TempDir())
            {
                // Another game in the default folder of the first drive; DOOM Eternal in the second drive's named folder.
                Install(t, "c", GamePassInstall.DefaultGamingFolder, "Other", Config("Other.Game", "1.0.0.0"));
                var content = Install(t, "e", "Games", "Doom Eternal - PC", RigConfig);
                File.WriteAllBytes(t.Combine("e", GamePassInstall.GamingRootFile), GamingRoot("Games"));

                var found = GamePassInstall.FindInDrives(new[] { t.Combine("c"), t.Combine("d-missing"), t.Combine("e") });
                Assert.NotNull(found);
                Assert.Equal(content, found.GameRoot);
                Assert.Equal(GamePlatform.GamePass, found.Platform);
                Assert.Equal("1.0.56.0", found.BuildId);
                Assert.Null(GamePassInstall.FindInDrives(new[] { t.Combine("c") }));
            }
        }

        [Fact]
        public void TheDefaultFolderIsSearchedWithoutAGamingRootFile()
        {
            using (var t = new TempDir())
            {
                var content = Install(t, "e", GamePassInstall.DefaultGamingFolder, "Doom Eternal - PC", RigConfig);
                Assert.Equal(content, GamePassInstall.FindInDrives(new[] { t.Combine("e") }).GameRoot);
            }
        }

        [Fact]
        public void AChosenFolderIsTheContentFolderOrItsParent()
        {
            using (var t = new TempDir())
            {
                var content = Install(t, "e", GamePassInstall.DefaultGamingFolder, "Doom Eternal - PC", RigConfig);
                Assert.Equal(content, GamePassInstall.ContentFolderOf(content));
                Assert.Equal(content, GamePassInstall.ContentFolderOf(Path.GetDirectoryName(content)));
                Assert.Null(GamePassInstall.ContentFolderOf(t.Combine("e")));
            }
            Assert.True(GamePassInstall.IsPackageStorePath(@"C:\Program Files\WindowsApps\BethesdaSoftworks.DOOMEternal-PC_1.0.56.0_x64__3275kfvn8vcwc"));
            Assert.False(GamePassInstall.IsPackageStorePath(@"E:\XboxGames\Doom Eternal - PC\Content"));
        }

        [Fact]
        public void TheBuildIsKnownByItsPackageVersion()
        {
            using (var t = new TempDir())
            {
                var builds = KnownBuilds.Parse(TestData.Read("known-builds.txt"));
                var known = Install(t, "a", "XboxGames", "Doom", RigConfig);
                var check = GamePassInstall.Check(builds, known);
                Assert.Equal(BuildStatus.Known, check.Status);
                Assert.Equal(GamePlatform.GamePass, check.Build.Platform);
                Assert.Equal("1.0.56.0", check.Version);
                Assert.Null(check.Sha256);

                var newer = Install(t, "b", "XboxGames", "Doom", Config(GamePassInstall.PackageName, "1.0.57.0"));
                Assert.Equal(BuildStatus.Unknown, GamePassInstall.Check(builds, newer).Status);
                Assert.Equal("1.0.57.0", GamePassInstall.Check(builds, newer).Version);

                Assert.Equal(BuildStatus.Missing, GamePassInstall.Check(builds, Install(t, "c", "XboxGames", "Doom", RigConfig, exe: false)).Status);
                Assert.Equal(BuildStatus.Missing, GamePassInstall.Check(builds, Install(t, "d", "XboxGames", "Doom", Config("Other.Game", "1.0.56.0"))).Status);
                Assert.Equal(BuildStatus.Missing, GamePassInstall.Check(builds, t.Combine("nowhere")).Status);
            }
        }

        [Fact]
        public void KnownBuildsHoldGamePassRecords()
        {
            var shipped = KnownBuilds.Parse(TestData.Read("known-builds.txt"));
            Assert.Equal(new[] { "1.0.56.0" }, shipped.IdsFor(GamePlatform.GamePass));
            Assert.Equal(new[] { "25216728" }, shipped.IdsFor(GamePlatform.Steam));

            var b = KnownBuilds.Parse("GamePass | Some.Package | 2.0.1.0 | test");
            var build = b.All.Single();
            Assert.Equal("Some.Package", build.StoreName);
            Assert.Equal("2.0.1.0", build.BuildId);
            Assert.Null(build.Sha256);
            Assert.Null(b.Find(null));
            Assert.Equal(BuildStatus.Known, b.CheckStore(new StoreIdentity("some.package", null, "2.0.1.0")).Status);
            Assert.Equal(BuildStatus.Missing, b.CheckStore(null).Status);
            Assert.Throws<FormatException>(() => KnownBuilds.Parse("gamepass | Some.Package | not a version | x"));
            Assert.Throws<FormatException>(() => KnownBuilds.Parse("gamepass |  | 1.0.0.0 | x"));
        }

        [Fact]
        public void GamePassSettingsAreTheSavedGamesFolderOnly()
        {
            using (var t = new TempDir())
            {
                t.Write("saved/base/DOOMEternalConfig.cfg", "x");
                t.Write("steam/userdata/111/782330/remote/PROFILE/profile.bin", "p");
                var steam = GameLayout.FindSettingsLocations(GamePlatform.Steam, t.Combine("saved"), t.Combine("steam"), "111");
                var gamePass = GameLayout.FindSettingsLocations(GamePlatform.GamePass, t.Combine("saved"), t.Combine("steam"), "111");
                Assert.Equal(2, steam.Count);
                Assert.Equal(new[] { SettingsLocationKind.SavedGames }, gamePass.Select(l => l.Kind));
            }
        }

        [Fact]
        public void TheSaveContainersAreFoundInThePackageFolder()
        {
            using (var t = new TempDir())
            {
                Assert.Null(GamePassInstall.SaveLocation(t.Path));
                t.Write("Packages/Other.Game_abc/SystemAppData/wgs/containers.index", "x");
                Assert.Null(GamePassInstall.SaveContainerDir(t.Path));
                t.Write($"Packages/{GamePassInstall.PackageName}_3275kfvn8vcwc/SystemAppData/wgs/containers.index", "x");
                var loc = GamePassInstall.SaveLocation(t.Path);
                Assert.Equal(t.Combine("Packages", GamePassInstall.PackageName + "_3275kfvn8vcwc", "SystemAppData", "wgs"), loc.Path);
                Assert.Equal(SettingsLocationKind.GamePassSaves, loc.Kind);
            }
        }

        [Fact]
        public void TheSaveContainersAreBackedUpWholeAndNeverRestored()
        {
            using (var t = new TempDir())
            {
                t.Write("wgs/0009_7803BEF0/12CD/315EA462", "save data");
                t.Write("wgs/0009_7803BEF0/containers.index", "index");
                var locs = new[] { new SettingsLocation(GamePassInstall.SaveLocationName, SettingsLocationKind.GamePassSaves, t.Combine("wgs")) };
                var dir = SaveBackups.Create(t.Combine("backups"), "20260929-120000", locs);
                Assert.NotNull(dir);
                Assert.Empty(SaveBackups.Verify(dir));
                Assert.Equal("save data", File.ReadAllText(Path.Combine(dir, "gamepass", "0009_7803BEF0", "12CD", "315EA462")));
                Assert.True(SaveBackups.HoldsGamePassSaves(dir));
                Assert.Equal(SettingsLocationKind.GamePassSaves, SaveBackups.LocationsOf(dir).Single().Kind);
                Assert.Throws<IOException>(() => SaveBackups.Restore(t.Combine("backups"), dir, new DateTime(2026, 9, 29)));

                var host = new FakeHost();
                var r = SaveRestore.Run(t.Combine("backups"), dir, new DateTime(2026, 9, 29), new SettingsLocation[0], host,
                                        new CloudResyncOptions(), _ => { });
                Assert.Equal(SaveRestoreOutcome.Refused, r.Outcome);
                Assert.Contains("Game Pass", r.Summary);
                Assert.Empty(host.Started);
                Assert.Equal("save data", t.Read("wgs/0009_7803BEF0/12CD/315EA462"));

                // No container folder: nothing is backed up.
                var none = new[] { new SettingsLocation(GamePassInstall.SaveLocationName, SettingsLocationKind.GamePassSaves, t.Combine("absent")) };
                Assert.Null(SaveBackups.Create(t.Combine("backups"), "20260929-120001", none));
            }
        }

        private static PreflightFacts GamePassFacts() => new PreflightFacts
        {
            Platform = GamePlatform.GamePass,
            // Steam is neither running nor logged in, and a stale cloud record is left over: none of it counts.
            SteamRunning = false,
            SteamLoggedIn = false,
            CloudRecordStale = new[] { "PROFILE/profile.bin" },
            CloudRecordUnreadable = new[] { "remotecache.vdf" },
            GameRoot = @"E:\XboxGames\Doom Eternal - PC\Content",
            Build = new BuildCheck(BuildStatus.Known, null, KnownBuild.GamePass(GamePassInstall.PackageName, "1.0.56.0", "Game Pass"), "1.0.56.0"),
            KnownBuildIds = new[] { "1.0.56.0" },
            LayerDir = @"E:\EternalVR\layer",
            LayerManifestExists = true,
            LayerLibraryExists = true,
            LauncherVersion = "0.1.0",
            LayerVersion = "0.1.0",
            RuntimeManifest = @"C:\runtime.json",
            RuntimeManifestExists = true,
            SettingsLocationCount = 1,
        };

        [Fact]
        public void AGamePassInstallLaunchesWithoutSteam()
        {
            var r = PreflightEvaluator.Evaluate(GamePassFacts());
            Assert.True(r.CanLaunch);
            Assert.DoesNotContain(r.Checks, c => c.Severity != Severity.Pass);
            Assert.DoesNotContain(r.Checks, c => c.Id == "steam" || c.Id == "cloud-record");
            Assert.Contains("1.0.56.0", r.Checks.Single(c => c.Id == "game-build").Message);
        }

        [Fact]
        public void AnUnknownGamePassVersionIsRefusedLikeAnUnknownSteamBuild()
        {
            var f = GamePassFacts();
            f.Build = new BuildCheck(BuildStatus.Unknown, null, null, "1.0.57.0");
            var check = PreflightEvaluator.Evaluate(f).Checks.Single(c => c.Id == "game-build");
            Assert.Equal(Severity.Fail, check.Severity);
            Assert.Contains("version 1.0.57.0", check.Message);
            Assert.Contains("supports Game Pass version 1.0.56.0", check.Message);
        }

        [Fact]
        public void AFolderThatIsNotTheContentFolderIsRefused()
        {
            var f = GamePassFacts();
            f.Build = new BuildCheck(BuildStatus.Missing, null, null);
            var r = PreflightEvaluator.Evaluate(f);
            Assert.False(r.CanLaunch);
            Assert.Contains("Content", r.Checks.Single(c => c.Id == "game").Message);
        }

        [Fact]
        public void AGamePassInstallNeverStartedNamesTheXboxApp()
        {
            var f = GamePassFacts();
            f.SettingsLocationCount = 0;
            var check = PreflightEvaluator.Evaluate(f).Checks.Single(c => c.Id == "settings");
            Assert.Equal(Severity.Fail, check.Severity);
            Assert.Contains("the Xbox app", check.Message);
        }
    }
}
