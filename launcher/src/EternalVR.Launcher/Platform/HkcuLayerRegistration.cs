using System;
using System.IO;
using EternalVR.Launcher.Core.Safety;
using Microsoft.Win32;

namespace EternalVR.Launcher.Platform
{
    /// <summary>
    /// The HKCU implicit-layer route (ARCHITECTURE section 4), used only with
    /// <c>--register-hkcu</c>; the default route is <c>VK_ADD_IMPLICIT_LAYER_PATH</c> in the game's
    /// environment. Crash safety (T-093): <c>REGISTRATION_PENDING</c> naming the value, and the keys the
    /// registration has to create, is written before the registry value, and every launcher start removes what a
    /// marker names before anything else (<see cref="RegistrationMarker"/>). Only values named by our own marker are
    /// ever removed, and only keys the marker says we created (the layers key or its ancestors), once they are empty.
    /// The tests (launcher/tests, HkcuLayerRegistrationTests) compile this file and run it under a scratch key.
    /// </summary>
#if NET5_0_OR_GREATER
    [System.Runtime.Versioning.SupportedOSPlatform("windows")]
#endif
    public sealed class HkcuLayerRegistration
    {
        public const string DefaultLayersKey = @"SOFTWARE\Khronos\Vulkan\ImplicitLayers";
        private readonly string markerPath;
        private readonly Action<string> log;
        private readonly string layersKey;

        /// <param name="layersKey">The implicit-layer key under HKCU; another key only in the tests.</param>
        public HkcuLayerRegistration(string markerPath, Action<string> log, string layersKey = DefaultLayersKey)
        {
            this.markerPath = markerPath;
            this.log = log;
            this.layersKey = layersKey;
        }

        public void Register(string manifestPath)
        {
            var full = Path.GetFullPath(manifestPath);
            var created = RegistrationMarker.MissingKeys(layersKey, KeyExists);
            File.AppendAllLines(markerPath, RegistrationMarker.Lines(full, created));
            using (var key = Registry.CurrentUser.CreateSubKey(layersKey))
                key.SetValue(full, 0, RegistryValueKind.DWord);
            log("registered the layer under HKCU for this session: " + full +
                (created.Count > 0 ? " (created " + string.Join(", ", created) + ")" : ""));
        }

        /// <summary>Removes every value the marker names and the empty keys it created, then the marker. Safe to call at any time.</summary>
        public void RemoveStale()
        {
            if (!File.Exists(markerPath)) return;
            RegistrationMarker.Parse(File.ReadAllLines(markerPath), layersKey, out var names, out var createdKeys, out var ignored);
            foreach (var other in ignored)
                log("REGISTRATION_PENDING names a key that is not the layer key or one of its parents; left alone: " + other);
            using (var key = Registry.CurrentUser.OpenSubKey(layersKey, writable: true))
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
            // Deepest first: a parent is empty only once its created child is gone. A key that holds anything (another
            // program's layer, or a value added since) stays.
            foreach (var created in createdKeys)
            {
                bool empty;
                using (var key = Registry.CurrentUser.OpenSubKey(created, writable: false))
                {
                    if (key == null) continue;
                    empty = key.ValueCount == 0 && key.SubKeyCount == 0;
                }
                if (!empty) continue;
                // Between the check and the delete another program could still write a value here, which the delete
                // would take with the key; that window is microseconds, on a key we created, and accepted.
                Registry.CurrentUser.DeleteSubKey(created, throwOnMissingSubKey: false);
                log("removed the empty HKCU key the registration created: " + created);
            }
            File.Delete(markerPath);
        }

        private static bool KeyExists(string path)
        {
            using (var key = Registry.CurrentUser.OpenSubKey(path, writable: false))
                return key != null;
        }
    }
}
