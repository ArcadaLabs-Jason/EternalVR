using System.Linq;
using EternalVR.Launcher.Core.Game;
using EternalVR.Launcher.Core.Headsets;
using EternalVR.Launcher.Core.Preflight;
using EternalVR.Launcher.Core.Report;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>
    /// The "untested on this hardware" notice (ROADMAP M8, T-076, T-095): an acceptance test for each untested entry the
    /// launcher can detect, by graphics card vendor and generation and by OpenXR runtime, and none for the tested ones.
    /// </summary>
    public class UntestedHardwareTests
    {
        private static readonly HeadsetTable Table = HeadsetTable.Parse(TestData.Read("headsets.txt"));

        private static HeadsetIdentity Route(string runtimeName, string systemName = null, SteamVrHeadset seen = null) =>
            HeadsetIdentity.Identify(runtimeName, systemName, seen, Table);

        private static PreflightFacts Facts(GpuAdapter gpu, HeadsetIdentity headset) => new PreflightFacts
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
            Compatibility = new CompatibilityFacts { Gpus = gpu == null ? new GpuAdapter[0] : new[] { gpu } },
            Headset = headset,
        };

        private static GpuAdapter Gpu(string name, long gb = 16) => new GpuAdapter(name, gb << 30, true);

        private static string[] Notices(PreflightFacts f)
        {
            var r = PreflightEvaluator.Evaluate(f);
            Assert.True(r.CanLaunch); // a notice never refuses
            var notices = r.Checks.Where(c => c.Id == UntestedHardware.Id).ToList();
            Assert.All(notices, c => Assert.Equal(Severity.Warn, c.Severity));
            foreach (var c in notices) UiText.AssertNoAdvice(c.Message, c.Id);
            return notices.Select(c => c.Message).ToArray();
        }

        [Theory]
        [InlineData("AMD Radeon RX 5700 XT", "AMD Radeon RX 5000 cards (RDNA)")]
        [InlineData("AMD Radeon RX 6750 XT", "AMD Radeon RX 6000 cards (RDNA 2)")]
        [InlineData("AMD Radeon RX 7900 XTX", "AMD Radeon RX 7000 cards (RDNA 3)")]
        [InlineData("AMD Radeon RX 9070 XT", "AMD Radeon RX 9000 cards (RDNA 4)")]
        [InlineData("AMD Radeon(TM) Graphics", "AMD Radeon graphics")]
        [InlineData("Radeon RX 580 Series", "AMD Radeon graphics")]
        [InlineData("Intel(R) Arc(TM) A770 Graphics", "Intel Arc cards")]
        [InlineData("Intel(R) UHD Graphics 770", "Intel graphics")]
        [InlineData("NVIDIA GeForce GTX 1080 Ti", "NVIDIA cards without RTX in their name")]
        [InlineData("NVIDIA A100-PCIE-40GB", "NVIDIA cards without RTX in their name")]
        [InlineData("NVIDIA L40", "NVIDIA cards without RTX in their name")]
        [InlineData("AMD Radeon RX 6800M", "AMD Radeon RX 6000 cards (RDNA 2)")]
        [InlineData("AMD Radeon RX 7600M XT", "AMD Radeon RX 7000 cards (RDNA 3)")]
        [InlineData("AMD Radeon RX 6700S", "AMD Radeon RX 6000 cards (RDNA 2)")]
        public void EachUntestedGraphicsCardGetsTheNotice(string adapter, string entry)
        {
            Assert.Equal(entry, UntestedHardware.GpuEntry(adapter));
            var notice = Assert.Single(Notices(Facts(Gpu(adapter), Route("VirtualDesktopXR", "Meta Quest 3"))));
            Assert.Equal("Untested on this hardware: EternalVR has not been tested with " + entry + " on the test PCs yet (this PC: " + adapter
                + "); it may well work. docs\\KNOWN-ISSUES.md has what players have reported, and an Export report after a session helps.", notice);
        }

        [Theory]
        [InlineData("NVIDIA GeForce RTX 3080 Ti")]
        [InlineData("NVIDIA GeForce RTX 4080")]
        [InlineData("NVIDIA GeForce RTX 5060 Laptop GPU")]
        [InlineData("NVIDIA RTX A4000")]
        [InlineData("Quadro RTX 5000")]
        public void NvidiaRtxIsTested(string adapter)
        {
            Assert.Null(UntestedHardware.GpuEntry(adapter));
            Assert.Empty(Notices(Facts(Gpu(adapter), Route("VirtualDesktopXR", "Meta Quest 3"))));
        }

        [Fact]
        public void TheCardWithTheMostVideoMemoryIsTheOneJudged()
        {
            // A laptop: the NVIDIA card runs the game, the AMD graphics in the processor does not.
            var gpus = new[] { new GpuAdapter("AMD Radeon(TM) Graphics", 512L << 20, true), Gpu("NVIDIA GeForce RTX 4070 Laptop GPU", 8),
                new GpuAdapter("Virtual Display Driver", null, false) };
            Assert.Equal("NVIDIA GeForce RTX 4070 Laptop GPU", UntestedHardware.MainGpu(gpus).Name);
            Assert.Empty(UntestedHardware.Evaluate(gpus, null));
            // A desktop AMD card next to Intel's processor graphics: the AMD card.
            var amd = new[] { Gpu("Intel(R) UHD Graphics 770", 0), Gpu("AMD Radeon RX 7800 XT", 16) };
            Assert.Contains("AMD Radeon RX 7000 cards (RDNA 3)", Assert.Single(UntestedHardware.Evaluate(amd, null)).Message);
            // No size from any driver: the first hardware adapter; a virtual one never counts.
            Assert.Equal("AMD Radeon RX 6800", UntestedHardware.MainGpu(new[] { new GpuAdapter("VDD", null, false), new GpuAdapter("AMD Radeon RX 6800", null, true) }).Name);
            Assert.Null(UntestedHardware.MainGpu(new[] { new GpuAdapter("VDD", null, false) }));
            // No size from any driver on a laptop: the NVIDIA RTX card rather than the processor's graphics listed first.
            var unsized = new[] { new GpuAdapter("AMD Radeon(TM) Graphics", null, true), new GpuAdapter("NVIDIA GeForce RTX 4060 Laptop GPU", null, true) };
            Assert.Equal("NVIDIA GeForce RTX 4060 Laptop GPU", UntestedHardware.MainGpu(unsized).Name);
            Assert.Empty(UntestedHardware.Evaluate(unsized, null));
            // An adapter without a name is no card the launcher can place.
            Assert.Empty(UntestedHardware.Evaluate(new[] { new GpuAdapter(null, 8L << 30, true) }, null));
            Assert.Empty(UntestedHardware.Evaluate(null, null));
        }

        [Theory]
        [InlineData("Oculus", "Meta Quest 3", "Meta Horizon Link")]
        [InlineData("Pimax OpenXR", "Pimax Crystal", "the OpenXR runtime Pimax Play")]
        [InlineData("Varjo OpenXR Runtime", "XR-4", "the OpenXR runtime Varjo Base")]
        [InlineData("Windows Mixed Reality Runtime", "Windows Mixed Reality", "the OpenXR runtime Windows Mixed Reality Runtime")]
        public void EachUntestedRuntimeGetsTheNotice(string runtime, string system, string entry)
        {
            var notice = Assert.Single(Notices(Facts(Gpu("NVIDIA GeForce RTX 4080"), Route(runtime, system))));
            Assert.Equal("Untested on this hardware: EternalVR has not been tested with " + entry
                + " on the test PCs yet; it may well work. docs\\KNOWN-ISSUES.md has what players have reported, and an Export report after a session helps.", notice);
        }

        [Fact]
        public void SteamVrWithAnIndexOrAPlayStationVr2GetsTheNotice()
        {
            var index = Route("SteamVR/OpenXR", "SteamVR/OpenXR : lighthouse", new SteamVrHeadset { Manufacturer = "Valve Corporation", Model = "Index", Driver = "lighthouse" });
            Assert.Equal("SteamVR with a Valve Index", UntestedHardware.RouteEntry(index));
            Assert.Contains("tested with SteamVR with a Valve Index on the test PCs", Assert.Single(Notices(Facts(Gpu("NVIDIA GeForce RTX 4080"), index))));
            // PlayStation VR2: its tracking system names it, with or without SteamVR's last seen headset.
            Assert.Equal("SteamVR with a PlayStation VR2", UntestedHardware.RouteEntry(Route("SteamVR/OpenXR", "SteamVR/OpenXR : playstation_vr2")));
        }

        [Fact]
        public void TestedRoutesAndUnnamedHeadsetsGetNoNotice()
        {
            // Virtual Desktop's VDXR, with any headset.
            Assert.Null(UntestedHardware.RouteEntry(Route("VirtualDesktopXR", "Meta Quest 3")));
            Assert.Null(UntestedHardware.RouteEntry(Route("VirtualDesktopXR (Bundled)", "Meta Quest Pro")));
            // SteamVR with a Quest (Steam Link, or Virtual Desktop's SteamVR mode).
            Assert.Null(UntestedHardware.RouteEntry(Route("SteamVR/OpenXR", "SteamVR/OpenXR : oculus", new SteamVrHeadset { Model = "Meta Quest 3", Driver = "oculus" })));
            Assert.Null(UntestedHardware.RouteEntry(Route("SteamVR/OpenXR", "SteamVR/OpenXR : oculus", new SteamVrHeadset { Model = "Oculus Quest2", Driver = "oculus_virtualdesktop" })));
            // SteamVR naming only its tracking system: the headset is not known, so nothing is said.
            Assert.Null(UntestedHardware.RouteEntry(Route("SteamVR/OpenXR", "SteamVR/OpenXR : lighthouse")));
            // No runtime known.
            Assert.Null(UntestedHardware.RouteEntry(Route(null)));
            Assert.Null(UntestedHardware.RouteEntry(null));
            Assert.Empty(Notices(Facts(Gpu("NVIDIA GeForce RTX 3080 Ti"), Route("VirtualDesktopXR", "Meta Quest 3"))));
        }

        [Fact]
        public void AnUntestedCardOnAnUntestedRouteGetsBothNotices()
        {
            var notices = Notices(Facts(Gpu("AMD Radeon RX 6750 XT"), Route("Oculus", "Meta Quest 3")));
            Assert.Equal(2, notices.Length);
            Assert.Contains("AMD Radeon RX 6000 cards (RDNA 2)", notices[0]);
            Assert.Contains("Meta Horizon Link", notices[1]);
        }
    }
}
