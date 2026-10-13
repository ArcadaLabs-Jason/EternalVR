using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using EternalVR.Launcher.Core.Safety;
using EternalVR.Launcher.Platform;
using Microsoft.Win32;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>
    /// The launcher's own HKCU registration (src/EternalVR.Launcher/Platform/HkcuLayerRegistration.cs, compiled into this
    /// project) against the real registry, under a scratch HKCU\SOFTWARE\EternalVR-test-&lt;guid&gt; that each test's
    /// Dispose removes, passed or failed. Windows only: elsewhere each test returns at once.
    /// </summary>
    [System.Runtime.Versioning.SupportedOSPlatform("windows")]
    public sealed class HkcuLayerRegistrationTests : IDisposable
    {
        private readonly string root = @"SOFTWARE\EternalVR-test-" + Guid.NewGuid().ToString("N");
        private readonly string dir = Path.Combine(Path.GetTempPath(), "evr-hkcu-" + Guid.NewGuid().ToString("N"));
        private readonly List<string> log = new List<string>();

        private string Layers => root + @"\Khronos\Vulkan\ImplicitLayers";
        private string Vulkan => root + @"\Khronos\Vulkan";
        private string Marker => Path.Combine(dir, "REGISTRATION_PENDING");
        private string Manifest => Path.Combine(dir, "layer", "VK_LAYER_ETERNALVR.json");

        public HkcuLayerRegistrationTests() => Directory.CreateDirectory(dir);

        public void Dispose()
        {
            if (OperatingSystem.IsWindows()) Registry.CurrentUser.DeleteSubKeyTree(root, throwOnMissingSubKey: false);
            if (Directory.Exists(dir)) Directory.Delete(dir, recursive: true);
        }

        private HkcuLayerRegistration New() => new HkcuLayerRegistration(Marker, log.Add, Layers);

        private static bool Exists(string key)
        {
            if (!OperatingSystem.IsWindows()) return false;
            using (var k = Registry.CurrentUser.OpenSubKey(key)) return k != null;
        }

        [Fact]
        public void TheKeysTheRegistrationCreatedAreDeletedDeepestFirst()
        {
            if (!OperatingSystem.IsWindows()) return;
            var reg = New();
            reg.Register(Manifest);
            using (var k = Registry.CurrentUser.OpenSubKey(Layers)) Assert.Equal(0, k.GetValue(Path.GetFullPath(Manifest)));
            Assert.Contains(RegistrationMarker.CreatedKeyPrefix + root, File.ReadAllLines(Marker));

            reg.RemoveStale();

            Assert.False(Exists(root));
            Assert.False(File.Exists(Marker));
            var removed = log.Where(l => l.StartsWith("removed the empty HKCU key", StringComparison.Ordinal)).ToList();
            Assert.Equal(new[] { Layers, Vulkan, root + @"\Khronos", root }, removed.Select(l => l.Substring(l.IndexOf(": ", StringComparison.Ordinal) + 2)));
        }

        [Fact]
        public void ACreatedKeyThatHoldsSomethingElseIsKept()
        {
            if (!OperatingSystem.IsWindows()) return;
            var reg = New();
            reg.Register(Manifest);
            // Another program's value under a key the registration created.
            using (var k = Registry.CurrentUser.OpenSubKey(Vulkan, writable: true)) k.SetValue("other", 1, RegistryValueKind.DWord);

            reg.RemoveStale();

            Assert.False(Exists(Layers));
            Assert.True(Exists(Vulkan));
            using (var k = Registry.CurrentUser.OpenSubKey(Vulkan)) Assert.Equal(1, k.GetValue("other"));
            Assert.True(Exists(root)); // its parents are not empty either
            Assert.False(File.Exists(Marker));
        }

        [Fact]
        public void AKeyThatExistedBeforeIsKeptEvenWhenEmpty()
        {
            if (!OperatingSystem.IsWindows()) return;
            Registry.CurrentUser.CreateSubKey(Layers).Dispose();
            var reg = New();
            reg.Register(Manifest);
            Assert.DoesNotContain(File.ReadAllLines(Marker), l => l.StartsWith(RegistrationMarker.CreatedKeyPrefix, StringComparison.Ordinal));

            reg.RemoveStale();

            Assert.True(Exists(Layers));
            using (var k = Registry.CurrentUser.OpenSubKey(Layers)) Assert.Equal(0, k.ValueCount);
            Assert.Contains(log, l => l.StartsWith("removed the HKCU layer registration: ", StringComparison.Ordinal));
        }

        [Fact]
        public void AForeignKeyInTheMarkerIsLeftAlone()
        {
            if (!OperatingSystem.IsWindows()) return;
            // Positive control for the marker check: an empty key that is not the layer key or a parent of it.
            var foreign = root + @"\Foreign";
            Registry.CurrentUser.CreateSubKey(foreign).Dispose();
            File.WriteAllLines(Marker, new[] { RegistrationMarker.CreatedKeyPrefix + foreign });

            New().RemoveStale();

            Assert.True(Exists(foreign));
            Assert.Contains(log, l => l.Contains("left alone: " + foreign));
            Assert.False(File.Exists(Marker));
        }
    }
}
