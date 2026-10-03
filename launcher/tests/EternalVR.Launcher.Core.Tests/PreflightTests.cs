using System.Collections.Generic;
using System.IO;
using System.Linq;
using EternalVR.Launcher.Core.Game;
using EternalVR.Launcher.Core.Preflight;
using EternalVR.Launcher.Core.Report;
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
        public void ASteamVrBindingChosenForTheGameWarnsUnderSteamVrOnly()
        {
            var f = Good();
            f.SteamVrBindings = new[] { new SteamVrBinding("playstation_vr2_sense", SteamVrBindingKind.Workshop) };
            Assert.DoesNotContain(PreflightEvaluator.Evaluate(f).Checks, c => c.Id == "steamvr-binding");

            f.RuntimeManifest = @"D:\SteamLibrary\steamapps\common\SteamVR\steamxr_win64.json";
            var r = PreflightEvaluator.Evaluate(f);
            Assert.True(r.CanLaunch);
            var check = Only(r, "steamvr-binding");
            Assert.Equal(Severity.Warn, check.Severity);
            Assert.Equal("SteamVR uses a custom controller binding for DOOM Eternal (workshop binding for playstation_vr2_sense). If your "
                + "controllers do nothing in game, open SteamVR > Settings > Controllers > Manage Controller Bindings, pick DOOM Eternal "
                + "and choose the default binding.", check.Message);

            f.SteamVrBindings = new SteamVrBinding[0];
            Assert.DoesNotContain(PreflightEvaluator.Evaluate(f).Checks, c => c.Id == "steamvr-binding");
        }

        [Fact]
        public void SteamVrThrottlingForTheGameWarnsUnderSteamVrOnly()
        {
            var f = Good();
            f.SteamVrFramesToThrottle = 1;
            Assert.DoesNotContain(PreflightEvaluator.Evaluate(f).Checks, c => c.Id == "steamvr-throttle");

            f.RuntimeManifest = @"D:\SteamLibrary\steamapps\common\SteamVR\steamxr_win64.json";
            var r = PreflightEvaluator.Evaluate(f);
            Assert.True(r.CanLaunch);
            var check = Only(r, "steamvr-throttle");
            Assert.Equal(Severity.Warn, check.Severity);
            Assert.Equal("SteamVR limits DOOM Eternal to half the refresh rate (Throttling Behavior: Limit); frame pacing follows it. The setting: "
                + "SteamVR Settings > Video > Per-Application Video Settings > DOOM Eternal > Throttling Behavior.", check.Message);

            f.SteamVrFramesToThrottle = 2;
            Assert.Contains("to a third of the refresh rate (Throttling Behavior: Limit)", Only(PreflightEvaluator.Evaluate(f), "steamvr-throttle").Message);

            // Auto (the key removed) and Limit at the full rate (0) do not throttle.
            foreach (var none in new int?[] { null, 0 })
            {
                f.SteamVrFramesToThrottle = none;
                Assert.DoesNotContain(PreflightEvaluator.Evaluate(f).Checks, c => c.Id == "steamvr-throttle");
            }
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
                DataRoot = @"D:\" + new string('d', CompatibilityChecks.LongDataRootChars),
            };
            var r = PreflightEvaluator.Evaluate(f);
            Assert.True(r.CanLaunch);
            foreach (var id in new[] { "steam", "program-folder", "hags", "layer-disabled", "vram", "gpus", "injectors", "vulkan-loader", "path", "data-path" })
                Assert.Equal(Severity.Warn, Only(r, id).Severity);
            Assert.Contains("Hardware-accelerated GPU scheduling", Only(r, "hags").Message);
            // The fact only: what the player does about it is theirs to decide.
            Assert.Equal("Small GPU has 8 GB of video memory; stereo renders two images.", Only(r, "vram").Message);
            Assert.Equal("Hardware-accelerated GPU scheduling is on; it caused frame hitching in VR on the development PC.",
                Only(r, "hags").Message);
            foreach (var c in r.Checks) UiText.AssertNoAdvice(c.Message, c.Id);
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
                DataRoot = @"C:\Users\Someone\AppData\Local\EternalVR",
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
        public void ARuntimeFromTheEnvironmentIsNamedAsSuch()
        {
            var f = Good();
            f.RuntimeFromEnvironment = true;
            Assert.Contains("XR_RUNTIME_JSON in your environment", Only(PreflightEvaluator.Evaluate(f), "openxr").Message);
            f.RuntimeManifestExists = false;
            var msg = Only(PreflightEvaluator.Evaluate(f), "openxr").Message;
            Assert.Contains("XR_RUNTIME_JSON", msg);
            Assert.Contains(@"C:\runtime.json", msg);
        }

        [Fact]
        public void TheElevationRefusalSaysWhatIsIgnoredAndHowToStart()
        {
            var f = Good();
            f.IsElevated = true;
            var check = Only(PreflightEvaluator.Evaluate(f), "elevation");
            Assert.Equal(Severity.Fail, check.Severity);
            Assert.Equal("The launcher is running as administrator. Close it and start it normally (not as administrator): in elevated processes "
                + "Windows ignores the layer settings, and OpenXR ignores the helpers registered for your user (HKCU API layers) and the "
                + "runtime chosen here (XR_RUNTIME_JSON).", check.Message);
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
        public void OpenXrToolkitIsSwitchedOffUnderItsCurrentAndOldNames()
        {
            // Current versions register XR_APILAYER_MBUCCHIA_toolkit, older ones XR_APILAYER_NOVENDOR_toolkit. Each is switched off through
            // its own manifest's variable, whatever the runtime, before the MBUCCHIA_* prefix's warning can match.
            foreach (var name in new[] { "XR_APILAYER_MBUCCHIA_toolkit", "XR_APILAYER_NOVENDOR_toolkit" })
            {
                var manifest = "{ \"file_format_version\": \"1.0.0\", \"api_layer\": { \"name\": \"" + name
                    + "\", \"disable_environment\": \"DISABLE_" + name + "\" } }";
                var layer = InstalledLayer.FromManifest(LayerApi.OpenXR, name + ".json", manifest, "HKLM", true);
                foreach (var vdxr in new[] { false, true })
                {
                    var d = Known().Evaluate(layer, vdxr).Single();
                    Assert.Equal(LayerAction.Disable, d.Action);
                    Assert.Equal(new KeyValuePair<string, string>("DISABLE_" + name, "1"), d.Environment.Value);
                    Assert.Contains("OpenXR Toolkit", d.Message);
                }
            }
            // The same author's other layers are still only untested.
            var other = new InstalledLayer(LayerApi.OpenXR, "XR_APILAYER_MBUCCHIA_varjo_foveated", "v.json", "HKLM", true, "DISABLE_X", "1");
            Assert.Equal(LayerAction.Warn, Known().Evaluate(new[] { other }, false).Single().Action);
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
