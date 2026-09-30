using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;

namespace EternalVR.Launcher.Core.Preflight
{
    /// <summary>A display adapter from the display class key: hardware ones are on the PCI bus.</summary>
    public sealed class GpuAdapter
    {
        public GpuAdapter(string name, long? dedicatedMemoryBytes, bool hardware)
        {
            Name = name;
            DedicatedMemoryBytes = dedicatedMemoryBytes;
            Hardware = hardware;
        }

        public string Name { get; }
        /// <summary>HardwareInformation.qwMemorySize (or MemorySize); null when the driver does not say.</summary>
        public long? DedicatedMemoryBytes { get; }
        /// <summary>A PCI device (not a virtual display adapter such as a VDD or a remote desktop one).</summary>
        public bool Hardware { get; }
    }

    /// <summary>Machine facts that make VR less likely to work well; each only warns.</summary>
    public sealed class CompatibilityFacts
    {
        public IReadOnlyList<GpuAdapter> Gpus { get; set; } = new GpuAdapter[0];
        /// <summary>The DLL file names directly in the game folder.</summary>
        public IReadOnlyList<string> GameFolderDlls { get; set; } = new string[0];
        /// <summary>The file version of System32\vulkan-1.dll; null when it cannot be read.</summary>
        public string VulkanLoaderVersion { get; set; }
        /// <summary>The folders whose paths are checked for characters outside ASCII (game, program, data).</summary>
        public IReadOnlyList<string> Paths { get; set; } = new string[0];
        /// <summary>The launcher's data folder (its sessions, snapshots and save backups); null when not known.</summary>
        public string DataRoot { get; set; }
    }

    public static class CompatibilityChecks
    {
        /// <summary>At or below this much video memory the game's own VRAM budget and the second eye's targets do not fit well.</summary>
        public const long LowVramBytes = 8L * 1024 * 1024 * 1024;
        /// <summary>The loader the layer route needs (T-079).</summary>
        public static readonly Version MinimumLoader = new Version(1, 3, 234);
        /// <summary>
        /// Beyond this many characters the files the launcher keeps under its data folder (session snapshots, logs) may pass
        /// Windows' classic 260-character path limit, which applies unless long paths are turned on in Windows. Save
        /// backups use extended-length paths and are not limited by it (<see cref="FileUtil.Long"/>).
        /// </summary>
        public const int LongDataRootChars = 150;

        /// <summary>DLL names that injectors and upscaler mods drop into a game folder (ReShade, SpecialK, OptiScaler, ...).</summary>
        public static readonly IReadOnlyList<string> InjectorDlls = new[]
        {
            "dxgi.dll", "d3d11.dll", "d3d12.dll", "winmm.dll", "version.dll", "dinput8.dll", "nvngx.dll", "vulkan-1.dll",
            "opengl32.dll", "reshade.dll",
        };

        public static IEnumerable<Check> Evaluate(CompatibilityFacts f, string gameRoot)
        {
            if (f == null) yield break;
            var hardware = f.Gpus.Where(g => g.Hardware).ToList();
            var largest = hardware.Where(g => g.DedicatedMemoryBytes.HasValue).OrderByDescending(g => g.DedicatedMemoryBytes).FirstOrDefault();
            if (largest != null && largest.DedicatedMemoryBytes.Value <= LowVramBytes)
                yield return new Check("vram", Severity.Warn,
                    $"{largest.Name} has {Gb(largest.DedicatedMemoryBytes.Value)} GB of video memory. Stereo renders two images; "
                    + "lower the render scale or the game's texture quality if the game stutters or crashes.");
            if (hardware.Count > 1)
                yield return new Check("gpus", Severity.Warn,
                    "More than one GPU: " + string.Join(", ", hardware.Select(g => g.Name)) + ". The game, the headset runtime and the "
                    + "display the headset uses must all run on the same GPU (on laptops: set DOOM Eternal to the high-performance GPU "
                    + "in Windows Settings > Display > Graphics).");

            var injectors = f.GameFolderDlls.Where(d => InjectorDlls.Contains(d, StringComparer.OrdinalIgnoreCase)).ToList();
            if (injectors.Count > 0)
                yield return new Check("injectors", Severity.Warn,
                    "The game folder holds " + string.Join(", ", injectors) + " (ReShade, SpecialK, OptiScaler or a similar mod). "
                    + "They are untested with EternalVR; if VR misbehaves, move them out of " + gameRoot + ".");

            if (TryParseVersion(f.VulkanLoaderVersion, out var loader) && loader < MinimumLoader)
                yield return new Check("vulkan-loader", Severity.Warn,
                    $"The Vulkan loader (System32\\vulkan-1.dll) is version {f.VulkanLoaderVersion}, older than {MinimumLoader}: the layer may not load. "
                    + "Update the GPU driver.");

            var nonAscii = f.Paths.Where(p => !string.IsNullOrEmpty(p) && p.Any(ch => ch > 127)).ToList();
            if (nonAscii.Count > 0)
                yield return new Check("path", Severity.Warn,
                    "These paths contain characters outside plain ASCII: " + string.Join(", ", nonAscii)
                    + ". It should work, but if the layer does not load, move the game or EternalVR to a plain path.");

            if (f.DataRoot != null && f.DataRoot.Length > LongDataRootChars)
                yield return new Check("data-path", Severity.Warn,
                    $"The data folder's path is {f.DataRoot.Length} characters long: {f.DataRoot}. Some of the launcher's files in it "
                    + "may pass Windows' 260-character path limit, and then backing up or restoring your settings fails. Use a "
                    + "shorter data folder, or turn on long paths in Windows.");
        }

        public static bool TryParseVersion(string text, out Version version)
        {
            version = null;
            if (string.IsNullOrWhiteSpace(text)) return false;
            // File versions may carry text after the numbers ("1.3.290.0 (branch)").
            var numbers = new string(text.Trim().TakeWhile(ch => char.IsDigit(ch) || ch == '.').ToArray()).Trim('.');
            return Version.TryParse(numbers, out version);
        }

        private static string Gb(long bytes) => (bytes / (1024.0 * 1024 * 1024)).ToString("0.#", CultureInfo.InvariantCulture);
    }
}
