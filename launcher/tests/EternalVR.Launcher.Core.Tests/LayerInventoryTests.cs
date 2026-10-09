using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using EternalVR.Launcher.Core.Preflight;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>The registered OpenXR and Vulkan layers in system.txt and the launch's log (fake registry and manifests).</summary>
    public class LayerInventoryTests
    {
        private const string ReShadeXr = @"C:\Program Files\ReShade\ReShade64_XR.json";
        private const string ReShadeXr32 = @"C:\Program Files\ReShade\ReShade32_XR.json";
        private const string VdXr = @"C:\Program Files\Virtual Desktop Streamer\openxr-oculus-compatibility.json";
        private const string SteamVk = @"C:\Program Files (x86)\Steam\SteamOverlayVulkanLayer64.json";
        private const string ObsVk = @"C:\Program Files\obs-studio\data\obs-plugins\win-capture\obs-vulkan64.json";
        private const string NvVk = @"C:\Windows\System32\DriverStore\FileRepository\nv_dispi.inf_amd64\nv-vk64.json";

        private static readonly Dictionary<string, string> Files = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
        {
            [ReShadeXr] = @"{ ""file_format_version"": ""1.0.0"", ""api_layer"": { ""name"": ""XR_APILAYER_reshade"",
                ""library_path"": "".\\ReShade64.dll"", ""disable_environment"": ""DISABLE_RESHADE"" } }",
            [ReShadeXr32] = @"{ ""file_format_version"": ""1.0.0"", ""api_layer"": { ""name"": ""XR_APILAYER_reshade"",
                ""library_path"": "".\\ReShade32.dll"", ""disable_environment"": ""DISABLE_RESHADE"" } }",
            [VdXr] = "\uFEFF{ \"file_format_version\" : \"1.0.0\", \"api_layer\": { \"name\": \"XR_APILAYER_VIRTUALDESKTOP_oculus_compatibility\", "
                + "\"library_path\": \".\\\\openxr-oculus-compatibility.dll\", \"disable_environment\": \"DISABLE_XR_APILAYER_VIRTUALDESKTOP_OCULUS_COMPATIBILITY\" } }",
            [SteamVk] = @"{ ""file_format_version"" : ""1.0.0"", ""layer"" : { ""name"": ""VK_LAYER_VALVE_steam_overlay"",
                ""library_path"": "".\\SteamOverlayVulkanLayer64.dll"",
                ""disable_environment"": { ""DISABLE_VK_LAYER_VALVE_steam_overlay_1"": ""1"" },
                ""enable_environment"": { ""ENABLE_VK_LAYER_VALVE_steam_overlay_1"": ""1"" } } }",
            [ObsVk] = @"{ ""file_format_version"": ""1.0.0"", ""layer"": { ""name"": ""VK_LAYER_OBS_HOOK"", ""library_path"": "".\\graphics-hook64.dll"",
                ""disable_environment"": { ""DISABLE_VULKAN_OBS_CAPTURE"": ""1"" } } }",
            [NvVk] = @"{ ""file_format_version"" : ""1.0.1"", ""ICD"": { ""library_path"": "".\\nvoglv64.dll"" },
                ""layers"": [ { ""name"": ""VK_LAYER_NV_optimus"", ""library_path"": "".\\nvoglv64.dll"",
                ""disable_environment"": { ""DISABLE_LAYER_NV_OPTIMUS_1"": """" } } ] }",
            [@"C:\EternalVR\layer\VK_LAYER_ETERNALVR.json"] = @"{ ""layer"": { ""name"": ""VK_LAYER_ETERNALVR"", ""library_path"": "".\\EternalVR.dll"",
                ""disable_environment"": { ""ETERNALVR_DISABLE_LAYER"": ""1"" } } }",
            [@"C:\broken.json"] = "{ not json",
            [@"C:\huge-number.json"] = @"{ ""layer"": { ""name"": ""VK_LAYER_x"", ""api_version"": 1e400 } }",
            [@"C:\deep.json"] = new string('[', 100000) + new string(']', 100000),
            [@"C:\arch32.json"] = @"{ ""layer"": { ""name"": ""VK_LAYER_x32"", ""library_path"": ""x32.dll"", ""library_arch"": ""32"",
                ""disable_environment"": { ""DISABLE_X"": ""1"" } } }",
            [@"C:\arch64.json"] = @"{ ""layer"": { ""name"": ""VK_LAYER_x64"", ""library_path"": ""x64.dll"", ""library_arch"": ""64"",
                ""disable_environment"": { ""DISABLE_X"": ""1"" } } }",
        };

        private static string Read(string path)
        {
            if (path == @"C:\locked.json") throw new UnauthorizedAccessException("Access to the path is denied.");
            if (path == @"C:\odd.json") throw new InvalidOperationException("something else went wrong");
            return Files.TryGetValue(path, out var text) ? text : null;
        }

        private static LayerRegistration Reg(LayerApi api, string path, object value, string scope = "HKLM", LayerKind kind = LayerKind.Implicit) =>
            new LayerRegistration(api, kind, scope, path, value, is32Bit: scope.EndsWith("32-bit", StringComparison.Ordinal), driver: scope == "driver");

        private static IReadOnlyList<RegisteredManifest> Machine() => LayerInventory.Read(new[]
        {
            Reg(LayerApi.OpenXR, VdXr, 0),
            Reg(LayerApi.OpenXR, ReShadeXr, 0),
            Reg(LayerApi.OpenXR, ReShadeXr32, 1, "HKLM 32-bit"),
            Reg(LayerApi.Vulkan, SteamVk, 0),
            Reg(LayerApi.Vulkan, ObsVk, 0, "HKCU"),
            Reg(LayerApi.Vulkan, NvVk, null, "driver"),
        }, Read);

        private static Func<string, string> Env(params string[] pairs)
        {
            var d = pairs.Select(p => p.Split(new[] { '=' }, 2)).ToDictionary(p => p[0], p => p[1], StringComparer.OrdinalIgnoreCase);
            return n => d.TryGetValue(n, out var v) ? v : null;
        }

        private const string Prediction = " (prediction from the registry and the launch environment; the layer log shows what loaded)";

        [Fact]
        public void ReportListsEveryLayerWithWhetherItLoads()
        {
            // The launch turns ReShade's OpenXR layer off (known-layers.txt: disable).
            var lines = LayerInventory.ReportLines(Machine(), Env("DISABLE_RESHADE=1"), Env("VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation"))
                .Select(kv => kv.Key + ": " + kv.Value).ToList();
            Assert.Equal(new[]
            {
                "openxr layers: 3 manifest(s) registered, 1 layer(s) load with a launch" + Prediction,
                @"openxr layer: XR_APILAYER_VIRTUALDESKTOP_oculus_compatibility (implicit, HKLM, enabled): loads with a launch; library .\openxr-oculus-compatibility.dll; "
                    + @"off with DISABLE_XR_APILAYER_VIRTUALDESKTOP_OCULUS_COMPATIBILITY; manifest " + VdXr,
                @"openxr layer: XR_APILAYER_reshade (implicit, HKLM, enabled): not loaded, off: DISABLE_RESHADE is set; library .\ReShade64.dll; off with DISABLE_RESHADE; manifest " + ReShadeXr,
                @"openxr layer: XR_APILAYER_reshade (implicit, HKLM 32-bit, disabled (1)): not loaded, 32-bit only; library .\ReShade32.dll; off with DISABLE_RESHADE; manifest " + ReShadeXr32,
                "openxr layer environment: none set (XR_ENABLE_API_LAYERS, XR_API_LAYER_PATH, XR_RUNTIME_JSON)",
                "vulkan layers: 3 manifest(s) registered, 2 layer(s) load with a launch" + Prediction,
                @"vulkan layer: VK_LAYER_VALVE_steam_overlay (implicit, HKLM, enabled): not loaded, only with ENABLE_VK_LAYER_VALVE_steam_overlay_1=1 (not set); "
                    + @"library .\SteamOverlayVulkanLayer64.dll; off with DISABLE_VK_LAYER_VALVE_steam_overlay_1=1; on only with ENABLE_VK_LAYER_VALVE_steam_overlay_1=1; manifest " + SteamVk,
                @"vulkan layer: VK_LAYER_OBS_HOOK (implicit, HKCU, enabled): loads with a launch; library .\graphics-hook64.dll; off with DISABLE_VULKAN_OBS_CAPTURE=1; manifest " + ObsVk,
                @"vulkan layer: VK_LAYER_NV_optimus (implicit, driver, always on): registered by a display driver; loads if that adapter is present; library .\nvoglv64.dll; off with DISABLE_LAYER_NV_OPTIMUS_1; manifest " + NvVk,
                "vulkan layer environment: VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation",
            }, lines);
        }

        [Fact]
        public void LaunchLogNamesOtherLayersThatLoad()
        {
            var manifests = LayerInventory.Read(Machine().Select(m => m.Registration)
                .Concat(new[] { Reg(LayerApi.Vulkan, @"C:\EternalVR\layer\VK_LAYER_ETERNALVR.json", 0, "HKCU") }), Read);
            // Steam's overlay loads only with its enable variable (Steam sets it): system.txt says so, the launch log does not.
            Assert.Equal(new[]
            {
                "An OpenXR layer should also load with the game: XR_APILAYER_VIRTUALDESKTOP_oculus_compatibility (" + VdXr + ")",
                "An OpenXR layer should also load with the game: XR_APILAYER_reshade (" + ReShadeXr + ")",
                "A Vulkan layer should also load with the game: VK_LAYER_OBS_HOOK (" + ObsVk + ")",
            }, LayerInventory.LaunchLines(manifests, Env()));
            // Turned off for the launch, or enabled by its variable.
            var lines = LayerInventory.LaunchLines(manifests, Env("DISABLE_RESHADE=1", "ENABLE_VK_LAYER_VALVE_steam_overlay_1=1"));
            Assert.DoesNotContain(lines, l => l.Contains("XR_APILAYER_reshade"));
            Assert.Contains("A Vulkan layer should also load with the game: VK_LAYER_VALVE_steam_overlay (" + SteamVk + ")", lines);
        }

        [Fact]
        public void ALayerRegisteredInHklmAndHkcuCountsOnce()
        {
            var m = LayerInventory.Read(new[] { Reg(LayerApi.Vulkan, ObsVk, 0), Reg(LayerApi.Vulkan, ObsVk, 0, "HKCU") }, Read);
            Assert.Contains(LayerInventory.ReportLines(m, Env(), Env()),
                kv => kv.Key == "vulkan layers" && kv.Value == "2 manifest(s) registered, 1 layer(s) load with a launch" + Prediction);
            Assert.Equal(new[] { "A Vulkan layer should also load with the game: VK_LAYER_OBS_HOOK (" + ObsVk + ")" }, LayerInventory.LaunchLines(m, Env()));
        }

        [Fact]
        public void LaunchLogIsCapped()
        {
            var regs = Enumerable.Range(0, 20).Select(i => Reg(LayerApi.Vulkan, @"C:\l" + i + ".json", 0, i % 2 == 0 ? "HKLM" : "HKCU"));
            var lines = LayerInventory.LaunchLines(LayerInventory.Read(regs, p => @"{ ""layer"": { ""name"": ""VK_LAYER_"
                + Path.GetFileNameWithoutExtension(p) + @""", ""disable_environment"": { ""DISABLE_L"": ""1"" } } }"), Env());
            Assert.Equal(LayerInventory.MostLogged + 1, lines.Count);
            Assert.Equal("... and 8 more such layer(s) (an exported report's system.txt lists every layer)", lines.Last());
        }

        [Fact]
        public void UnreadableManifestsAndRegistryValues()
        {
            var m = LayerInventory.Read(new[]
            {
                Reg(LayerApi.Vulkan, @"C:\gone.json", 0),
                Reg(LayerApi.Vulkan, @"C:\locked.json", 0),
                Reg(LayerApi.Vulkan, @"C:\broken.json", 0),
                Reg(LayerApi.Vulkan, ObsVk, "0"),
                Reg(LayerApi.OpenXR, @"C:\nothing.json", 0),
                Reg(LayerApi.Vulkan, @"C:\huge-number.json", 0),
                Reg(LayerApi.Vulkan, @"C:\deep.json", 0),
                Reg(LayerApi.Vulkan, @"C:\odd.json", 0),
            }, p => p == @"C:\nothing.json" ? "{ \"file_format_version\": \"1.0.0\" }" : Read(p));
            Assert.Equal("manifest missing", m[0].Problem);
            Assert.Equal("manifest unreadable: Access to the path is denied.", m[1].Problem);
            Assert.Equal("not valid JSON", m[2].Problem);
            Assert.False(m[3].Registration.Enabled); // a string, not a DWORD: the loader skips it
            Assert.Equal("disabled (not a DWORD)", m[3].Registration.State);
            Assert.Equal("no layer in it", m[4].Problem);
            // A number out of range and nesting past the reader's depth are bad JSON, not a crash of the checks.
            Assert.Equal("not valid JSON", m[5].Problem);
            Assert.Equal("not valid JSON", m[6].Problem);
            // Whatever else a read throws is that manifest's problem too, and a disabled layer for the checks.
            Assert.Equal("manifest unreadable: something else went wrong", m[7].Problem);
            Assert.False(LayerInventory.Implicit(m).Single(l => l.Name == @"C:\odd.json").EnabledInRegistry);
            var lines = LayerInventory.ReportLines(m, Env(), Env()).Select(kv => kv.Value).ToList();
            Assert.Contains(@"C:\gone.json (implicit, HKLM, enabled): manifest missing", lines);
            Assert.Contains(@"C:\broken.json (implicit, HKLM, enabled): not valid JSON", lines);
            Assert.Empty(LayerInventory.LaunchLines(m, Env()));
        }

        [Fact]
        public void ImplicitKeepsTheChecksInputs()
        {
            var m = LayerInventory.Read(new[]
            {
                Reg(LayerApi.OpenXR, VdXr, 0),
                Reg(LayerApi.OpenXR, ReShadeXr32, 0, "HKLM 32-bit"),
                Reg(LayerApi.Vulkan, ObsVk, 1, "HKCU"),
                Reg(LayerApi.Vulkan, @"C:\gone.json", 0),
                Reg(LayerApi.Vulkan, @"C:\broken.json", 0),
                Reg(LayerApi.Vulkan, NvVk, null, "driver"),
                Reg(LayerApi.Vulkan, SteamVk, 0, "HKLM", LayerKind.Explicit),
            }, Read);
            var layers = LayerInventory.Implicit(m);
            // The 64-bit implicit keys only, as before; a missing manifest is a disabled layer named by its path.
            Assert.Equal(new[] { "XR_APILAYER_VIRTUALDESKTOP_oculus_compatibility", "VK_LAYER_OBS_HOOK", @"C:\gone.json", @"C:\broken.json" },
                layers.Select(l => l.Name));
            Assert.Equal(new[] { true, false, false, true }, layers.Select(l => l.EnabledInRegistry));
            Assert.Equal("DISABLE_VULKAN_OBS_CAPTURE", layers[1].DisableEnvName);
        }

        [Fact]
        public void LoaderVariablesDecide()
        {
            var m = LayerInventory.Read(new[]
            {
                Reg(LayerApi.Vulkan, ObsVk, 0),
                Reg(LayerApi.Vulkan, SteamVk, 0, "HKLM", LayerKind.Explicit),
                Reg(LayerApi.OpenXR, ReShadeXr, 0, "HKLM", LayerKind.Explicit),
            }, Read);
            string Why(int i, Func<string, string> env) => LayerInventory.NotLoaded(m[i], m[i].Layers[0], env, out _);
            Assert.Null(Why(0, Env()));
            Assert.Equal("off through VK_LOADER_LAYERS_DISABLE", Why(0, Env("VK_LOADER_LAYERS_DISABLE=~implicit~")));
            Assert.Equal("off through VK_LOADER_LAYERS_DISABLE", Why(0, Env("VK_LOADER_LAYERS_DISABLE=VK_LAYER_OBS*")));
            Assert.Null(Why(0, Env("VK_LOADER_LAYERS_DISABLE=~all~", "VK_LOADER_LAYERS_ENABLE=*OBS_HOOK")));
            Assert.Null(Why(0, Env("VK_LOADER_LAYERS_DISABLE=*steam*")));
            Assert.Equal("explicit: loads only when named", Why(1, Env()));
            Assert.Null(Why(1, Env("VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation;VK_LAYER_VALVE_steam_overlay")));
            Assert.Null(Why(2, Env("XR_ENABLE_API_LAYERS=XR_APILAYER_reshade")));
            // A Vulkan enable variable must hold the manifest's value.
            var steam = LayerInventory.Read(new[] { Reg(LayerApi.Vulkan, SteamVk, 0) }, Read)[0];
            Assert.NotNull(LayerInventory.NotLoaded(steam, steam.Layers[0], Env("ENABLE_VK_LAYER_VALVE_steam_overlay_1=0"), out var needsEnable));
            Assert.True(needsEnable);
        }

        [Fact]
        public void ImplicitLayerWithoutDisableVariableIsSkipped()
        {
            var m = LayerInventory.Read(new[] { Reg(LayerApi.OpenXR, @"C:\x.json", 0) },
                _ => @"{ ""api_layer"": { ""name"": ""XR_APILAYER_x"", ""library_path"": ""x.dll"" } }")[0];
            Assert.Equal("no disable_environment (the OpenXR loader skips it)", LayerInventory.NotLoaded(m, m.Layers[0], Env(), out _));
            // The Vulkan loader too (LoaderLayerInterface.md, LLP_LAYER_9), whatever VK_LOADER_LAYERS_ENABLE says; an explicit
            // layer needs none.
            var vk = LayerInventory.Read(new[]
            {
                Reg(LayerApi.Vulkan, @"C:\v.json", 0),
                Reg(LayerApi.Vulkan, @"C:\v.json", 0, "HKLM", LayerKind.Explicit),
            }, _ => @"{ ""layer"": { ""name"": ""VK_LAYER_v"", ""library_path"": ""v.dll"" } }");
            Assert.Equal("no disable_environment (the Vulkan loader skips it)", LayerInventory.NotLoaded(vk[0], vk[0].Layers[0], Env("VK_LOADER_LAYERS_ENABLE=~all~"), out _));
            Assert.Null(LayerInventory.NotLoaded(vk[1], vk[1].Layers[0], Env("VK_INSTANCE_LAYERS=VK_LAYER_v"), out _));
        }

        [Fact]
        public void A32BitLibraryArchDoesNotLoadInTheGame()
        {
            var m = LayerInventory.Read(new[] { Reg(LayerApi.Vulkan, @"C:\arch32.json", 0), Reg(LayerApi.Vulkan, @"C:\arch64.json", 0) }, Read);
            Assert.Equal("32-bit only (library_arch)", LayerInventory.NotLoaded(m[0], m[0].Layers[0], Env(), out _));
            Assert.Null(LayerInventory.NotLoaded(m[1], m[1].Layers[0], Env(), out _));
            Assert.Contains(LayerInventory.ReportLines(m, Env(), Env()), kv => kv.Value.StartsWith(
                "VK_LAYER_x32 (implicit, HKLM, enabled): not loaded, 32-bit only (library_arch)", StringComparison.Ordinal));
        }

        [Fact]
        public void ADriversExplicitLayerLoadsOnlyWhenNamed()
        {
            var m = LayerInventory.Read(new[] { Reg(LayerApi.Vulkan, SteamVk, null, "driver", LayerKind.Explicit) }, Read)[0];
            Assert.Equal("explicit: loads only when named", LayerInventory.NotLoaded(m, m.Layers[0], Env(), out _));
            Assert.Null(LayerInventory.NotLoaded(m, m.Layers[0], Env("VK_INSTANCE_LAYERS=VK_LAYER_VALVE_steam_overlay"), out _));
            // The loader compares names exactly.
            Assert.NotNull(LayerInventory.NotLoaded(m, m.Layers[0], Env("VK_INSTANCE_LAYERS=vk_layer_valve_steam_overlay"), out _));
            // The display driver's layers are left out of the launch log.
            Assert.Empty(LayerInventory.LaunchLines(new[] { m }, Env("VK_INSTANCE_LAYERS=VK_LAYER_VALVE_steam_overlay")));
        }

        [Fact]
        public void HugeListsAreCapped()
        {
            var regs = Enumerable.Range(0, LayerInventory.MostRead + 5).Select(i => Reg(LayerApi.Vulkan, @"C:\l" + i + ".json", 0)).ToList();
            int reads = 0;
            var m = LayerInventory.Read(regs, p => { reads++; return null; });
            Assert.Equal(LayerInventory.MostRead, reads);
            Assert.Equal(LayerInventory.MostRead + 5, m.Count);
            var lines = LayerInventory.ReportLines(m, Env(), Env());
            Assert.Equal(LayerInventory.MostListed, lines.Count(kv => kv.Key == "vulkan layer"));
            Assert.Contains(lines, kv => kv.Key == "vulkan layers left out" && kv.Value == "165 more (the first 40 are listed)");
            Assert.Contains(lines, kv => kv.Key == "openxr layers" && kv.Value == "none registered");
        }

        [Fact]
        public void LaunchDisablesAreTheDisableDecisions()
        {
            var layer = new InstalledLayer(LayerApi.OpenXR, "XR_APILAYER_reshade", "r.json", "HKLM", true, "DISABLE_RESHADE", "1");
            var env = LayerInventory.LaunchDisables(new[]
            {
                new LayerDecision(layer, LayerAction.Disable, "off", new KeyValuePair<string, string>("DISABLE_RESHADE", "1")),
                new LayerDecision(layer, LayerAction.Warn, "warn", null),
            });
            Assert.Equal(new[] { new KeyValuePair<string, string>("DISABLE_RESHADE", "1") }, env);
        }
    }
}
