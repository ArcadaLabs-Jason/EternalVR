using System;
using System.IO;
using System.Linq;
using Microsoft.Win32;

namespace EternalVR.Launcher.Platform
{
    /// <summary>
    /// The HKCU implicit-layer route (ARCHITECTURE section 4), used only with
    /// <c>--register-hkcu</c>; the default route is <c>VK_ADD_IMPLICIT_LAYER_PATH</c> in the game's
    /// environment. Crash safety (T-093): <c>REGISTRATION_PENDING</c> naming the value is written before
    /// the registry value, and every launcher start removes what a marker names before anything else.
    /// Only values named by our own marker are ever removed.
    /// </summary>
    public sealed class HkcuLayerRegistration
    {
        private const string LayersKey = @"SOFTWARE\Khronos\Vulkan\ImplicitLayers";
        private readonly string markerPath;
        private readonly Action<string> log;

        public HkcuLayerRegistration(string markerPath, Action<string> log)
        {
            this.markerPath = markerPath;
            this.log = log;
        }

        public void Register(string manifestPath)
        {
            var full = Path.GetFullPath(manifestPath);
            File.AppendAllText(markerPath, full + Environment.NewLine);
            using (var key = Registry.CurrentUser.CreateSubKey(LayersKey))
                key.SetValue(full, 0, RegistryValueKind.DWord);
            log("registered the layer under HKCU for this session: " + full);
        }

        /// <summary>Removes every value the marker names, then the marker. Safe to call at any time.</summary>
        public void RemoveStale()
        {
            if (!File.Exists(markerPath)) return;
            var names = File.ReadAllLines(markerPath).Select(l => l.Trim()).Where(l => l.Length > 0).Distinct(StringComparer.OrdinalIgnoreCase).ToList();
            using (var key = Registry.CurrentUser.OpenSubKey(LayersKey, writable: true))
            {
                foreach (var name in names)
                {
                    if (key?.GetValue(name) != null)
                    {
                        key.DeleteValue(name, throwOnMissingValue: false);
                        log("removed the HKCU layer registration: " + name);
                    }
                }
            }
            File.Delete(markerPath);
        }
    }
}
