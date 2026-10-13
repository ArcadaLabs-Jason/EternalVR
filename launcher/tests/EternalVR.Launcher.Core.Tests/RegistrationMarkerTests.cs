using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Safety;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    public class RegistrationMarkerTests
    {
        private const string Layers = @"SOFTWARE\Khronos\Vulkan\ImplicitLayers";

        [Fact]
        public void OnAMachineWithoutKhronosKeysEveryKeyBelowSoftwareIsCreated()
        {
            var existing = new HashSet<string> { "SOFTWARE" };
            Assert.Equal(new[] { @"SOFTWARE\Khronos", @"SOFTWARE\Khronos\Vulkan", Layers },
                RegistrationMarker.MissingKeys(Layers, existing.Contains));
        }

        [Fact]
        public void KeysThatExistAreNotCreated()
        {
            var existing = new HashSet<string> { "SOFTWARE", @"SOFTWARE\Khronos", @"SOFTWARE\Khronos\Vulkan" };
            Assert.Equal(new[] { Layers }, RegistrationMarker.MissingKeys(Layers, existing.Contains));
            existing.Add(Layers);
            Assert.Empty(RegistrationMarker.MissingKeys(Layers, existing.Contains));
        }

        [Fact]
        public void BelowAMissingKeyEverythingIsMissing()
        {
            // The query for a child of a missing key is not even trusted: it cannot exist.
            var existing = new HashSet<string> { "SOFTWARE", @"SOFTWARE\Khronos\Vulkan" };
            Assert.Equal(new[] { @"SOFTWARE\Khronos", @"SOFTWARE\Khronos\Vulkan", Layers },
                RegistrationMarker.MissingKeys(Layers, existing.Contains));
        }

        [Fact]
        public void LinesRoundTripWithTheKeysDeepestFirst()
        {
            var lines = RegistrationMarker.Lines(@"C:\mod\layer\VK_LAYER_ETERNALVR.json",
                new[] { @"SOFTWARE\Khronos", @"SOFTWARE\Khronos\Vulkan", Layers }).ToList();
            Assert.Equal(@"C:\mod\layer\VK_LAYER_ETERNALVR.json", lines[0]);
            Assert.Equal(RegistrationMarker.CreatedKeyPrefix + @"SOFTWARE\Khronos", lines[1]);

            RegistrationMarker.Parse(lines, Layers, out var values, out var keys, out var ignored);
            Assert.Equal(new[] { @"C:\mod\layer\VK_LAYER_ETERNALVR.json" }, values);
            Assert.Equal(new[] { Layers, @"SOFTWARE\Khronos\Vulkan", @"SOFTWARE\Khronos" }, keys);
            Assert.Empty(ignored);
        }

        [Fact]
        public void AnOlderMarkerWithPathsOnlyNamesNoKeys()
        {
            RegistrationMarker.Parse(new[] { @"C:\a\VK_LAYER_ETERNALVR.json", "", @"c:\A\vk_layer_eternalvr.json  ", @"C:\b\VK_LAYER_ETERNALVR.json" },
                Layers, out var values, out var keys, out var ignored);
            Assert.Equal(new[] { @"C:\a\VK_LAYER_ETERNALVR.json", @"C:\b\VK_LAYER_ETERNALVR.json" }, values);
            Assert.Empty(keys);
            Assert.Empty(ignored);
        }

        [Fact]
        public void TwoSessionsAppendingTheSameKeysNameEachOnce()
        {
            var lines = RegistrationMarker.Lines(@"C:\a\x.json", new[] { Layers })
                .Concat(RegistrationMarker.Lines(@"C:\a\x.json", new[] { Layers.ToLowerInvariant() + "\\" })).ToList();
            RegistrationMarker.Parse(lines, Layers, out var values, out var keys, out var ignored);
            Assert.Single(values);
            Assert.Equal(new[] { Layers }, keys);
            Assert.Empty(ignored);
        }

        [Fact]
        public void OnlyTheLayersKeyAndItsParentsBelowTheRootAreTakenAsCreated()
        {
            // Positive control: a damaged or foreign marker naming other keys never gets them deleted.
            var lines = new[]
            {
                @"C:\a\x.json",
                RegistrationMarker.CreatedKeyPrefix + @"SOFTWARE\Khronos\Vulkan",
                RegistrationMarker.CreatedKeyPrefix + "SOFTWARE",
                RegistrationMarker.CreatedKeyPrefix + @"SOFTWARE\Microsoft\Windows\CurrentVersion\Run",
                RegistrationMarker.CreatedKeyPrefix + @"SOFTWARE\Khronos\Vulkan\ExplicitLayers",
                RegistrationMarker.CreatedKeyPrefix + @"SOFTWARE\Khronos\Vulkan\ImplicitLayers\Sub",
                RegistrationMarker.CreatedKeyPrefix + @"SOFTWARE\Khron",
                RegistrationMarker.CreatedKeyPrefix + Layers,
            };
            RegistrationMarker.Parse(lines, Layers, out var values, out var keys, out var ignored);
            Assert.Equal(new[] { @"C:\a\x.json" }, values);
            Assert.Equal(new[] { Layers, @"SOFTWARE\Khronos\Vulkan" }, keys);
            Assert.Equal(new[]
            {
                "SOFTWARE", @"SOFTWARE\Microsoft\Windows\CurrentVersion\Run", @"SOFTWARE\Khronos\Vulkan\ExplicitLayers",
                @"SOFTWARE\Khronos\Vulkan\ImplicitLayers\Sub", @"SOFTWARE\Khron",
            }, ignored);
        }
    }
}
