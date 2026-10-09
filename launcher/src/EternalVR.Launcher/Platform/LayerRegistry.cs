using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using EternalVR.Launcher.Core.Preflight;
using Microsoft.Win32;

namespace EternalVR.Launcher.Platform
{
    /// <summary>
    /// The OpenXR API layers and Vulkan layers registered on the machine (<see cref="LayerInventory"/>): the loaders' registry
    /// keys in HKLM and HKCU, the 32-bit ones (WOW6432Node; HKCU's Software key is shared by both views) and the Vulkan
    /// layers the display drivers register. Read only: nothing here writes the registry.
    /// </summary>
    public static class LayerRegistry
    {
        private const string VulkanKey = @"SOFTWARE\Khronos\Vulkan\";
        private const string OpenXrKey = @"SOFTWARE\Khronos\OpenXR\1\ApiLayers\";
        private const string DisplayClassKey = @"SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}";
        /// <summary>The most values taken from one key.</summary>
        private const int MostPerKey = 256;

        /// <summary>Every registration with its manifest read (<see cref="LayerInventory.Read"/>).</summary>
        public static IReadOnlyList<RegisteredManifest> Manifests() => LayerInventory.Read(Registrations(), ReadManifest);

        public static IReadOnlyList<LayerRegistration> Registrations()
        {
            var list = new List<LayerRegistration>();
            foreach (var api in new[] { LayerApi.OpenXR, LayerApi.Vulkan })
                foreach (var kind in new[] { LayerKind.Implicit, LayerKind.Explicit })
                {
                    var key = api == LayerApi.Vulkan
                        ? VulkanKey + (kind == LayerKind.Implicit ? "ImplicitLayers" : "ExplicitLayers")
                        : OpenXrKey + (kind == LayerKind.Implicit ? "Implicit" : "Explicit");
                    Collect(list, api, kind, RegistryHive.LocalMachine, RegistryView.Registry64, key, "HKLM");
                    Collect(list, api, kind, RegistryHive.CurrentUser, RegistryView.Registry64, key, "HKCU");
                    Collect(list, api, kind, RegistryHive.LocalMachine, RegistryView.Registry32, key, "HKLM 32-bit");
                }
            CollectDriverLayers(list);
            return list;
        }

        // A key this account may not read lists nothing (as WindowsSystem.ReadValue does), never a crash of the checks.
        private static void Collect(List<LayerRegistration> list, LayerApi api, LayerKind kind, RegistryHive hive, RegistryView view,
                                    string key, string scope)
        {
            try
            {
                using (var baseKey = RegistryKey.OpenBaseKey(hive, view))
                using (var k = baseKey.OpenSubKey(key, writable: false))
                {
                    if (k == null) return;
                    foreach (var manifest in k.GetValueNames().Take(MostPerKey))
                        list.Add(new LayerRegistration(api, kind, scope, manifest, k.GetValue(manifest), is32Bit: view == RegistryView.Registry32));
                }
            }
            catch (System.Security.SecurityException) { }
            catch (UnauthorizedAccessException) { }
            catch (IOException) { }
        }

        /// <summary>The display adapters' <c>VulkanImplicitLayers</c> and <c>VulkanExplicitLayers</c> (a path or a list of paths).</summary>
        private static void CollectDriverLayers(List<LayerRegistration> list)
        {
            try
            {
                using (var baseKey = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry64))
                using (var k = baseKey.OpenSubKey(DisplayClassKey, writable: false))
                {
                    if (k == null) return;
                    var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                    foreach (var sub in k.GetSubKeyNames().Where(n => n.Length == 4 && n.All(char.IsDigit)))
                    {
                        try
                        {
                            using (var a = k.OpenSubKey(sub, writable: false))
                            {
                                if (a == null) continue;
                                foreach (var kind in new[] { LayerKind.Implicit, LayerKind.Explicit })
                                {
                                    var value = a.GetValue(kind == LayerKind.Implicit ? "VulkanImplicitLayers" : "VulkanExplicitLayers");
                                    var paths = value is string s ? new[] { s } : value as string[] ?? new string[0];
                                    foreach (var p in paths.Where(p => !string.IsNullOrWhiteSpace(p) && seen.Add(p)).Take(MostPerKey))
                                        list.Add(new LayerRegistration(LayerApi.Vulkan, kind, "driver", p, null, driver: true));
                                }
                            }
                        }
                        catch (System.Security.SecurityException) { }
                        catch (UnauthorizedAccessException) { }
                        catch (IOException) { } // the adapter's key deleted while read (a driver install)
                    }
                }
            }
            catch (System.Security.SecurityException) { }
            catch (UnauthorizedAccessException) { }
            catch (IOException) { }
        }

        /// <summary>A manifest's text; null when it does not exist. Throws on one it cannot read or that is too large.</summary>
        private static string ReadManifest(string path)
        {
            if (string.IsNullOrWhiteSpace(path) || !File.Exists(path)) return null;
            var length = new FileInfo(path).Length;
            if (length > LayerInventory.MostManifestBytes) throw new IOException($"larger than {LayerInventory.MostManifestBytes / 1024} KB ({length} bytes)");
            return File.ReadAllText(path);
        }
    }
}
