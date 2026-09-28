using System.Collections.Generic;
using System.IO;
using System.Linq;
using EternalVR.Launcher.Core.Game;
using EternalVR.Launcher.Core.Preflight;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    public class PreflightTests
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
            RuntimeManifest = @"C:\runtime.json",
            RuntimeManifestExists = true,
            HagsMode = 1,
            SettingsLocationCount = 2,
        };

        private static Check Only(PreflightResult r, string id) => r.Checks.Single(c => c.Id == id);

        [Fact]
        public void AllGoodCanLaunchWithoutWarnings()
        {
            var r = PreflightEvaluator.Evaluate(Good());
            Assert.True(r.CanLaunch);
            Assert.DoesNotContain(r.Checks, c => c.Severity != Severity.Pass);
        }

        public static IEnumerable<object[]> Refusals()
        {
            yield return new object[] { "elevation", (System.Action<PreflightFacts>)(f => f.IsElevated = true) };
            yield return new object[] { "steam", (System.Action<PreflightFacts>)(f => f.SteamRunning = false) };
            yield return new object[] { "steam", (System.Action<PreflightFacts>)(f => f.SteamLoggedIn = false) };
            yield return new object[] { "game-running", (System.Action<PreflightFacts>)(f => f.GameProcessesRunning = new[] { "idTechLauncher" }) };
            yield return new object[] { "pending-restore", (System.Action<PreflightFacts>)(f => f.SessionPending = true) };
            yield return new object[] { "data-folder", (System.Action<PreflightFacts>)(f => { f.DataFolderWritable = false; f.DataFolderError = "denied"; }) };
            yield return new object[] { "game", (System.Action<PreflightFacts>)(f => f.GameRoot = null) };
            yield return new object[] { "game", (System.Action<PreflightFacts>)(f => f.Build = new BuildCheck(BuildStatus.Missing, null, null)) };
            yield return new object[] { "anti-cheat", (System.Action<PreflightFacts>)(f => f.AntiCheatFiles = new[] { "EasyAntiCheat (Easy Anti-Cheat)" }) };
            yield return new object[] { "layer", (System.Action<PreflightFacts>)(f => f.LayerLibraryExists = false) };
            yield return new object[] { "version", (System.Action<PreflightFacts>)(f => f.LayerVersion = "0.2.0") };
            yield return new object[] { "openxr", (System.Action<PreflightFacts>)(f => f.RuntimeManifest = null) };
            yield return new object[] { "openxr", (System.Action<PreflightFacts>)(f => f.RuntimeManifestExists = false) };
            yield return new object[] { "single-player", (System.Action<PreflightFacts>)(f => f.ArgumentRefusal = "Refused: BATTLEMODE") };
            yield return new object[] { "game-build", (System.Action<PreflightFacts>)(f => f.Build = new BuildCheck(BuildStatus.Unknown, new string('b', 64), null)) };
            yield return new object[] { "game", (System.Action<PreflightFacts>)(f => f.StoreInstall = true) };
            yield return new object[] { "settings", (System.Action<PreflightFacts>)(f => f.SettingsLocationCount = 0) };
        }

        [Theory]
        [MemberData(nameof(Refusals))]
        public void EachRefusalBlocksTheLaunch(string id, System.Action<PreflightFacts> change)
        {
            var f = Good();
            change(f);
            var r = PreflightEvaluator.Evaluate(f);
            Assert.False(r.CanLaunch);
            Assert.Equal(Severity.Fail, r.Checks.First(c => c.Id == id && c.Severity == Severity.Fail).Severity);
        }

        [Fact]
        public void WarningsDoNotBlock()
        {
            var f = Good();
            f.SteamLoggedIn = null;
            f.ProgramFolderUnderProgramFiles = true;
            f.HagsMode = 2;
            f.DisableLayerInherited = true;
            f.Compatibility = new CompatibilityFacts
            {
                Gpus = new[] { new GpuAdapter("Small GPU", 8L << 30, true), new GpuAdapter("iGPU", 1L << 29, true), new GpuAdapter("VDD", null, false) },
                GameFolderDlls = new[] { "DOOMEternalx64vk.dll", "dxgi.dll", "OptiScaler.dll", "nvngx.dll" },
                VulkanLoaderVersion = "1.2.198.1",
                Paths = new[] { "D:\\Spiele\\DOOM\u00C9ternal", @"C:\EternalVR" },
            };
            var r = PreflightEvaluator.Evaluate(f);
            Assert.True(r.CanLaunch);
            foreach (var id in new[] { "steam", "program-folder", "hags", "layer-disabled", "vram", "gpus", "injectors", "vulkan-loader", "path" })
                Assert.Equal(Severity.Warn, Only(r, id).Severity);
            Assert.Contains("Hardware-accelerated GPU scheduling", Only(r, "hags").Message);
            Assert.Contains("Small GPU has 8 GB", Only(r, "vram").Message);
            Assert.DoesNotContain("VDD", Only(r, "gpus").Message);
            Assert.Contains("dxgi.dll, nvngx.dll", Only(r, "injectors").Message);
            Assert.DoesNotContain("OptiScaler.dll", Only(r, "injectors").Message);
            Assert.Contains("D:\\Spiele\\DOOM\u00C9ternal", Only(r, "path").Message);
            Assert.DoesNotContain(@"C:\EternalVR", Only(r, "path").Message);
        }

        [Fact]
        public void AHealthyMachineGetsNoCompatibilityWarnings()
        {
            var f = Good();
            f.Compatibility = new CompatibilityFacts
            {
                Gpus = new[] { new GpuAdapter("RTX 3080 Ti", 12L << 30, true), new GpuAdapter("Virtual Display Driver", null, false) },
                GameFolderDlls = new[] { "amd_ags_x64.dll", "bink2w64.dll" },
                VulkanLoaderVersion = "1.4.341.0",
                Paths = new[] { @"C:\Program Files (x86)\Steam\steamapps\common\DOOMEternal" },
            };
            Assert.DoesNotContain(PreflightEvaluator.Evaluate(f).Checks, c => c.Severity != Severity.Pass);
        }

        [Fact]
        public void AnUnknownBuildNamesTheInstalledAndSupportedBuilds()
        {
            var f = Good();
            f.Build = new BuildCheck(BuildStatus.Unknown, new string('c', 64), null);
            f.SteamBuildId = "26000001";
            f.KnownBuildIds = new[] { "25216728" };
            var check = Only(PreflightEvaluator.Evaluate(f), "game-build");
            Assert.Equal(Severity.Fail, check.Severity);
            Assert.Contains("build 26000001", check.Message);
            Assert.Contains("supports build 25216728", check.Message);
            Assert.Contains("wait for an EternalVR update", check.Message, System.StringComparison.OrdinalIgnoreCase);
        }

        [Fact]
        public void TestModeOnlyWarnsForTheStandInBuildAndMissingSettings()
        {
            var f = Good();
            f.TestMode = true;
            f.Build = new BuildCheck(BuildStatus.Unknown, new string('b', 64), null);
            f.SettingsLocationCount = 0;
            var r = PreflightEvaluator.Evaluate(f);
            Assert.True(r.CanLaunch);
            Assert.Equal(Severity.Warn, Only(r, "game-build").Severity);
            Assert.Equal(Severity.Warn, Only(r, "settings").Severity);
        }

        [Theory]
        [InlineData(@"C:\Program Files\WindowsApps\BethesdaSoftworks.DOOMEternal-PC_1.0.0.0_x64__3275kfvn8vcwc", true)]
        [InlineData(@"D:\XboxGames\DOOM Eternal\Content", true)]
        [InlineData(@"C:\Program Files (x86)\Steam\steamapps\common\DOOMEternal", false)]
        public void StoreInstallsAreRecognisedByPath(string root, bool store)
        {
            Assert.Equal(store, GameLayout.IsStoreInstall(root));
        }

        [Fact]
        public void AStoreInstallIsRecognisedByItsConfigFile()
        {
            using (var dir = new TempDir())
            {
                Assert.False(GameLayout.IsStoreInstall(dir.Path));
                File.WriteAllText(Path.Combine(dir.Path, "MicrosoftGame.config"), "<Game/>");
                Assert.True(GameLayout.IsStoreInstall(dir.Path));
            }
        }

        [Theory]
        [InlineData("1.3.234.0", true)]
        [InlineData("1.4.341.0 (release)", true)]
        [InlineData("1.3", true)]
        [InlineData("", false)]
        [InlineData("n/a", false)]
        public void LoaderVersionsParse(string text, bool parses)
        {
            Assert.Equal(parses, CompatibilityChecks.TryParseVersion(text, out _));
        }

        [Fact]
        public void RuntimeRefusalNamesTheRuntimeAndHowToSwitch()
        {
            var f = Good();
            f.RuntimeChosen = true;
            f.RuntimeManifestExists = false;
            var msg = Only(PreflightEvaluator.Evaluate(f), "openxr").Message;
            Assert.Contains(@"C:\runtime.json", msg);
            Assert.Contains("Choose another runtime", msg);
        }

        [Fact]
        public void LayerDecisionsAppearAsChecks()
        {
            var f = Good();
            var layer = new InstalledLayer(LayerApi.Vulkan, "VK_LAYER_RTSS", "rtss.json", "HKLM", true, null, null);
            f.Layers = new[]
            {
                new LayerDecision(layer, LayerAction.Warn, "rtss warning", null),
                new LayerDecision(layer, LayerAction.Ignore, "ignored", null),
            };
            var r = PreflightEvaluator.Evaluate(f);
            Assert.Single(r.Checks, c => c.Id == "layers");
            Assert.True(r.CanLaunch);
        }
    }

    public class LayerTests
    {
        private const string VulkanManifest = @"{ ""file_format_version"": ""1.2.0"", ""layer"": { ""name"": ""VK_LAYER_OW_OVERLAY"",
            ""enable_environment"": { ""ENABLE_OW_OVERLAY"": ""1"" }, ""disable_environment"": { ""DISABLE_OW_OVERLAY"": ""1"" } } }";
        private const string OpenXrManifest = "\uFEFF{ \"file_format_version\" : \"1.0.0\", \"api_layer\": { \"name\": \"XR_APILAYER_VIRTUALDESKTOP_oculus_compatibility\", \"disable_environment\": \"DISABLE_XR_APILAYER_VIRTUALDESKTOP_OCULUS_COMPATIBILITY\" } }";

        private static KnownLayers Known() => KnownLayers.Parse(TestData.Read("known-layers.txt"));

        [Fact]
        public void ReadsVulkanAndOpenXrManifests()
        {
            var vk = InstalledLayer.FromManifest(LayerApi.Vulkan, "ow.json", VulkanManifest, "HKCU", true).Single();
            Assert.Equal("VK_LAYER_OW_OVERLAY", vk.Name);
            Assert.Equal("DISABLE_OW_OVERLAY", vk.DisableEnvName);
            var xr = InstalledLayer.FromManifest(LayerApi.OpenXR, "vd.json", OpenXrManifest, "HKLM", true).Single();
            Assert.Equal("DISABLE_XR_APILAYER_VIRTUALDESKTOP_OCULUS_COMPATIBILITY", xr.DisableEnvName);
            Assert.Equal("1", xr.DisableEnvValue);
        }

        [Fact]
        public void MultiLayerAndBrokenManifests()
        {
            var multi = InstalledLayer.FromManifest(LayerApi.Vulkan, "m.json", "{\"layers\":[{\"name\":\"A\"},{\"name\":\"B\"}]}", "HKLM", true);
            Assert.Equal(new[] { "A", "B" }, multi.Select(l => l.Name));
            var broken = InstalledLayer.FromManifest(LayerApi.Vulkan, "b.json", "{ not json", "HKLM", true).Single();
            Assert.Equal("b.json", broken.Name);
        }

        [Fact]
        public void VirtualDesktopLayerIsDisabledOnlyForOtherRuntimes()
        {
            var xr = InstalledLayer.FromManifest(LayerApi.OpenXR, "vd.json", OpenXrManifest, "HKLM", true);
            var withVdxr = Known().Evaluate(xr, runtimeIsVdxr: true).Single();
            Assert.Equal(LayerAction.Ignore, withVdxr.Action);
            var withOther = Known().Evaluate(xr, runtimeIsVdxr: false).Single();
            Assert.Equal(LayerAction.Disable, withOther.Action);
            Assert.Equal("DISABLE_XR_APILAYER_VIRTUALDESKTOP_OCULUS_COMPATIBILITY", withOther.Environment.Value.Key);
        }

        [Fact]
        public void ListedLayerIsDisabledThroughItsOwnVariable()
        {
            var vk = InstalledLayer.FromManifest(LayerApi.Vulkan, "ow.json", VulkanManifest, "HKCU", true);
            var d = Known().Evaluate(vk, false).Single();
            Assert.Equal(LayerAction.Disable, d.Action);
            Assert.Equal("DISABLE_OW_OVERLAY", d.Environment.Value.Key);
        }

        [Fact]
        public void RegistryDisabledLayersAreSkipped()
        {
            var vk = InstalledLayer.FromManifest(LayerApi.Vulkan, "ow.json", VulkanManifest, "HKCU", enabled: false);
            Assert.Empty(Known().Evaluate(vk, false));
        }

        [Fact]
        public void UnlistedLayersWarnAndUndisableableLayersWarn()
        {
            var unknown = new InstalledLayer(LayerApi.Vulkan, "VK_LAYER_SOMETHING", "s.json", "HKLM", true, "X", "1");
            Assert.Equal(LayerAction.Warn, Known().Evaluate(new[] { unknown }, false).Single().Action);
            var reshadeNoEnv = new InstalledLayer(LayerApi.Vulkan, "VK_LAYER_reshade_1", "r.json", "HKLM", true, null, null);
            var d = Known().Evaluate(new[] { reshadeNoEnv }, false).Single();
            Assert.Equal(LayerAction.Warn, d.Action);
            Assert.Null(d.Environment);
        }

        [Fact]
        public void SteamOverlayIsIgnored()
        {
            var steam = new InstalledLayer(LayerApi.Vulkan, "VK_LAYER_VALVE_steam_overlay_64", "s.json", "HKLM", true, "DISABLE_VK_LAYER_VALVE_steam_overlay_1", "1");
            Assert.Equal(LayerAction.Ignore, Known().Evaluate(new[] { steam }, false).Single().Action);
        }
    }

    public class AntiCheatTests
    {
        [Fact]
        public void FindsAntiCheatInTheGameFolderOnly()
        {
            using (var t = new TempDir())
            {
                var ac = AntiCheat.Parse(TestData.Read("anti-cheat.txt"));
                t.Write("game/DOOMEternalx64vk.exe", "x");
                t.Write("game/base/readme.txt", "x");
                Assert.Empty(ac.Scan(t.Combine("game")));
                t.Write("game/EasyAntiCheat/EasyAntiCheat_Setup.exe", "x");
                t.Write("game/a/b/BEService.exe", "deep: not looked at");
                var found = ac.Scan(t.Combine("game"));
                Assert.Equal(2, found.Count); // the folder and the file inside it
                Assert.All(found, f => Assert.Contains("Easy Anti-Cheat", f));
            }
        }
    }
}
