using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.RegularExpressions;
using EternalVR.Launcher.Core.Headsets;

namespace EternalVR.Launcher.Core.Preflight
{
    /// <summary>
    /// The "untested on this hardware" notice (T-076, T-095): one warning for each entry of the release matrix the launcher
    /// can detect that has not been tested with EternalVR yet. Tested so far: NVIDIA RTX graphics cards, Virtual Desktop's
    /// own runtime (VDXR), and SteamVR with a Quest. Not detected: a gaze-capable headset, and the headset model on
    /// SteamVR when SteamVR names none the launcher knows (<see cref="HeadsetIdentity"/>). Each notice only warns, and a
    /// tester's run of an entry moves it to the tested side here.
    /// </summary>
    public static class UntestedHardware
    {
        public const string Id = "untested";

        private static readonly Regex RadeonSeries = new Regex(@"\bRX\s*(?<series>\d)\d{3}[A-Z]{0,2}\b", RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);

        /// <summary>
        /// The graphics card the game most likely runs on: the hardware adapter with the most video memory; when no driver
        /// gives a size, a tested one (NVIDIA RTX, the laptop's dedicated card beside the processor's graphics), else the first.
        /// </summary>
        public static GpuAdapter MainGpu(IReadOnlyList<GpuAdapter> gpus)
        {
            var hardware = (gpus ?? new GpuAdapter[0]).Where(g => g.Hardware).ToList();
            return hardware.Where(g => g.DedicatedMemoryBytes.HasValue).OrderByDescending(g => g.DedicatedMemoryBytes.Value).FirstOrDefault()
                ?? hardware.FirstOrDefault(g => IsNvidia(g.Name) && Has(g.Name, "RTX"))
                ?? hardware.FirstOrDefault();
        }

        /// <summary>
        /// The untested entry a graphics card belongs to, by its name ("AMD Radeon RX 6000 cards (RDNA 2)"); null for a tested
        /// one (NVIDIA RTX) or a name the launcher cannot place.
        /// </summary>
        public static string GpuEntry(string adapterName)
        {
            var n = adapterName ?? string.Empty;
            if (IsNvidia(n))
                return Has(n, "RTX") ? null : "NVIDIA cards without RTX in their name";
            if (Has(n, "Radeon") || n.StartsWith("AMD", StringComparison.OrdinalIgnoreCase))
            {
                var m = RadeonSeries.Match(n);
                switch (m.Success ? m.Groups["series"].Value : string.Empty)
                {
                    case "5": return "AMD Radeon RX 5000 cards (RDNA)";
                    case "6": return "AMD Radeon RX 6000 cards (RDNA 2)";
                    case "7": return "AMD Radeon RX 7000 cards (RDNA 3)";
                    case "9": return "AMD Radeon RX 9000 cards (RDNA 4)";
                    default: return "AMD Radeon graphics";
                }
            }
            if (Has(n, "Intel")) return Has(n, "Arc") ? "Intel Arc cards" : "Intel graphics";
            return null;
        }

        /// <summary>
        /// The untested entry of the route the next launch takes ("Meta Horizon Link", "SteamVR with a Valve Index"); null for a
        /// tested one (VDXR, SteamVR with a Quest), an unknown runtime, or SteamVR with a headset the launcher cannot name.
        /// </summary>
        public static string RouteEntry(HeadsetIdentity headset)
        {
            if (headset == null) return null;
            switch (headset.Route)
            {
                case RouteKind.VirtualDesktop:
                case RouteKind.Unknown:
                    return null;
                case RouteKind.SteamVr:
                    var model = headset.Known?.Name;
                    if (string.IsNullOrWhiteSpace(model) || IsQuest(model)) return null;
                    return "SteamVR with " + Article(model) + " " + model;
                case RouteKind.MetaLink:
                    return "Meta Horizon Link";
                default:
                    return "the OpenXR runtime " + headset.RouteName;
            }
        }

        /// <summary>One warning per untested entry found: the main graphics card's, then the route's.</summary>
        public static IEnumerable<Check> Evaluate(IReadOnlyList<GpuAdapter> gpus, HeadsetIdentity headset)
        {
            var gpu = MainGpu(gpus);
            var gpuEntry = gpu == null ? null : GpuEntry(gpu.Name);
            if (gpuEntry != null) yield return Notice(gpuEntry, " (this PC: " + (gpu.Name ?? string.Empty).Trim() + ")");
            var routeEntry = RouteEntry(headset);
            if (routeEntry != null) yield return Notice(routeEntry, string.Empty);
        }

        private static Check Notice(string entry, string thisPc) =>
            new Check(Id, Severity.Warn, "Untested on this hardware: EternalVR has not been tested with " + entry + " on the test PCs yet" + thisPc
                + "; it may well work. docs\\KNOWN-ISSUES.md has what players have reported, and an Export report after a session helps.");

        private static bool IsQuest(string model) =>
            model.StartsWith("Meta Quest", StringComparison.OrdinalIgnoreCase) || model.StartsWith("Oculus Quest", StringComparison.OrdinalIgnoreCase);

        private static string Article(string word) =>
            !string.IsNullOrEmpty(word) && "AEIOU".IndexOf(char.ToUpperInvariant(word[0])) >= 0 ? "an" : "a";

        private static bool IsNvidia(string name) => Has(name, "NVIDIA") || Has(name, "GeForce") || Has(name, "Quadro");

        private static bool Has(string text, string part) => (text ?? string.Empty).IndexOf(part, StringComparison.OrdinalIgnoreCase) >= 0;
    }
}
