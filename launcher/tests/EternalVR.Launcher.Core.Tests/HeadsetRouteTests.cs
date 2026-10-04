using EternalVR.Launcher.Core.Headsets;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Report;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>Which headset and route the launcher found, in the window's words (<see cref="HeadsetIdentity"/>).</summary>
    public class HeadsetRouteTests
    {
        private static HeadsetTable Table => HeadsetTable.Parse(TestData.Read("headsets.txt"));

        private static SteamVrHeadset Seen(string maker, string model, string driver) =>
            new SteamVrHeadset { Manufacturer = maker, Model = model, Driver = driver };

        [Theory]
        // The probe's runtime names (layer logs: "xr: runtime 'VirtualDesktopXR' 1.0.10") and the manifests' runtime.name.
        [InlineData("VirtualDesktopXR", RouteKind.VirtualDesktop, "Virtual Desktop (VDXR)")]
        [InlineData("VirtualDesktopXR (Bundled)", RouteKind.VirtualDesktop, "Virtual Desktop (VDXR)")]
        [InlineData("Oculus", RouteKind.MetaLink, "Meta Link")]
        [InlineData("Oculus OpenXR", RouteKind.MetaLink, "Meta Link")]
        [InlineData("SteamVR/OpenXR", RouteKind.SteamVr, "SteamVR")]
        [InlineData("SteamVR", RouteKind.SteamVr, "SteamVR")]
        [InlineData("Pimax OpenXR", RouteKind.PimaxPlay, "Pimax Play")]
        [InlineData("PimaxXR (Unofficial)", RouteKind.PimaxPlay, "Pimax Play")]
        [InlineData("Varjo OpenXR Runtime", RouteKind.VarjoBase, "Varjo Base")]
        [InlineData("Windows Mixed Reality Runtime", RouteKind.Other, "Windows Mixed Reality Runtime")]
        [InlineData("OpenXR Simulator Runtime", RouteKind.Other, "OpenXR Simulator Runtime")]
        public void RoutesAreNamedFromTheRuntimesName(string runtime, RouteKind route, string name)
        {
            Assert.Equal(route, HeadsetIdentity.RouteOf(runtime));
            Assert.Equal(name, HeadsetIdentity.RouteNameOf(route, runtime));
        }

        [Fact]
        public void NoRuntimeIsAnUnknownRoute()
        {
            Assert.Equal(RouteKind.Unknown, HeadsetIdentity.RouteOf(null));
            Assert.Equal(RouteKind.Unknown, HeadsetIdentity.RouteOf("  "));
            var id = HeadsetIdentity.Identify(null, null, null, Table);
            Assert.Null(id.Model);
            Assert.Equal("The runtime asks for", id.AsksLabel);
        }

        [Fact]
        public void VirtualDesktopNamesTheHeadsetAndAsksAsVd()
        {
            var id = HeadsetIdentity.Identify("VirtualDesktopXR", "Meta Quest 3", null, Table);
            Assert.Equal(RouteKind.VirtualDesktop, id.Route);
            Assert.Equal("Meta Quest 3 via Virtual Desktop (VDXR)", id.Describe());
            Assert.Equal("VD asks for", id.AsksLabel);
            Assert.Equal(new Extent(2064, 2208), id.Known.Panel);
            Assert.True(id.ModelKnown);
            Assert.Null(id.TrackingSystem);
            // SteamVR's last seen headset does not count on another route.
            Assert.Equal("Meta Quest 3", HeadsetIdentity.Identify("VirtualDesktopXR", "Meta Quest 3", Seen("Valve Corporation", "Index", "lighthouse"), Table).Model);
        }

        [Fact]
        public void MetaLinkNamesTheHeadsetToo()
        {
            var id = HeadsetIdentity.Identify("Oculus", "Oculus Quest2", null, Table);
            Assert.Equal("Oculus Quest2 via Meta Link", id.Describe());
            Assert.Equal("Meta Link asks for", id.AsksLabel);
            Assert.Equal(new Extent(1832, 1920), id.Known.Panel);
        }

        [Fact]
        public void AModelNotInTheListIsStillNamedButHasNoPanel()
        {
            var id = HeadsetIdentity.Identify("VirtualDesktopXR", "PICO 4 Ultra", null, Table);
            Assert.Equal("PICO 4 Ultra via Virtual Desktop (VDXR)", id.Describe());
            Assert.Null(id.Known);
            var pimax = HeadsetIdentity.Identify("Pimax OpenXR", "Pimax Crystal", null, Table);
            Assert.Equal("Pimax Crystal via Pimax Play", pimax.Describe());
            Assert.Equal("The runtime asks for", pimax.AsksLabel);
            Assert.Equal(new Extent(2880, 2880), pimax.Known.Panel);
            var other = HeadsetIdentity.Identify("OpenXR Simulator Runtime", "OpenXR Simulator", null, Table);
            Assert.Equal("OpenXR Simulator via OpenXR Simulator Runtime", other.Describe());
            Assert.Null(other.Known);
        }

        [Fact]
        public void SteamVrNamesOnlyItsTrackingSystemSoTheModelIsLastSeenBySteamVr()
        {
            Assert.Equal("lighthouse", HeadsetIdentity.TrackingSystemOf("SteamVR/OpenXR : lighthouse"));
            Assert.Equal("playstation_vr2", HeadsetIdentity.TrackingSystemOf("SteamVR/OpenXR : playstation_vr2"));
            Assert.Equal("Windows Mixed Reality (holographic)", HeadsetIdentity.TrackingSystemOf("SteamVR/OpenXR : Windows Mixed Reality (holographic)"));
            Assert.Null(HeadsetIdentity.TrackingSystemOf("Meta Quest 3"));
            Assert.Null(HeadsetIdentity.TrackingSystemOf("SteamVR/OpenXR"));

            var index = HeadsetIdentity.Identify("SteamVR/OpenXR", "SteamVR/OpenXR : lighthouse", Seen("Valve Corporation", "Index", "lighthouse"), Table);
            Assert.Equal(RouteKind.SteamVr, index.Route);
            Assert.Equal("Valve Index (last seen by SteamVR) via SteamVR", index.Describe());
            Assert.Equal("SteamVR asks for", index.AsksLabel);
            Assert.Equal(new Extent(1440, 1600), index.Known.Panel);
            Assert.Equal("lighthouse", index.TrackingSystem);

            // A model not in the list: its maker and model as SteamVR gives them, no panel.
            var vive = HeadsetIdentity.Identify("SteamVR/OpenXR", "SteamVR/OpenXR : lighthouse", Seen("HTC", "Vive Focus 9", "lighthouse"), Table);
            Assert.Equal("HTC Vive Focus 9 (last seen by SteamVR) via SteamVR", vive.Describe());
            Assert.Null(vive.Known);
            // The model already starting with its maker is not doubled.
            var quest = HeadsetIdentity.Identify("SteamVR/OpenXR", "SteamVR/OpenXR : oculus", Seen("Oculus", "Oculus Quest2", "oculus"), Table);
            Assert.Equal("Meta Quest 2 (last seen by SteamVR) via SteamVR", quest.Describe());
        }

        [Fact]
        public void SteamVrWithoutAKnownModelSaysSoInsteadOfGuessing()
        {
            var none = HeadsetIdentity.Identify("SteamVR/OpenXR", "SteamVR/OpenXR : lighthouse", null, Table);
            Assert.Equal("a SteamVR headset (lighthouse)", none.Model);
            Assert.Equal("A SteamVR headset (lighthouse)", none.Describe());
            Assert.False(none.ModelKnown);
            Assert.Null(none.Known);
            // SteamVR last saw another headset (its driver is not this tracking system): not taken.
            var stale = HeadsetIdentity.Identify("SteamVR/OpenXR", "SteamVR/OpenXR : cv", Seen("Valve Corporation", "Index", "lighthouse"), Table);
            Assert.Equal("A SteamVR headset (cv)", stale.Describe());
            Assert.Null(stale.Known);
            // No tracking system in the name: just a SteamVR headset.
            Assert.Equal("a SteamVR headset", HeadsetIdentity.Identify("SteamVR/OpenXR", null, null, Table).Model);
        }

        [Fact]
        public void TheSteamFramesDriverIsTakenOnItsTrackingSystem()
        {
            // A player's report: SteamVR last saw "Valve" "Steam Frame" on driver vrlink, the system was "SteamVR/OpenXR : cv".
            var frame = HeadsetIdentity.Identify("SteamVR/OpenXR", "SteamVR/OpenXR : cv", Seen("Valve", "Steam Frame", "vrlink"), Table);
            Assert.Equal("Steam Frame (last seen by SteamVR) via SteamVR", frame.Describe());
            Assert.Equal(new Extent(2160, 2160), frame.Known.Panel);
            // The pair is that driver on that tracking system only.
            Assert.Null(HeadsetIdentity.Identify("SteamVR/OpenXR", "SteamVR/OpenXR : lighthouse", Seen("Valve", "Steam Frame", "vrlink"), Table).Known);
            Assert.Null(HeadsetIdentity.Identify("SteamVR/OpenXR", "SteamVR/OpenXR : cv", Seen("Valve", "Steam Frame", "lighthouse"), Table).Known);
        }

        [Fact]
        public void ATrackingSystemOneHeadsetAloneUsesNamesIt()
        {
            // PlayStation VR2's own SteamVR driver (a player's report: "xr: system 'SteamVR/OpenXR : playstation_vr2'").
            var psvr2 = HeadsetIdentity.Identify("SteamVR/OpenXR", "SteamVR/OpenXR : playstation_vr2", null, Table);
            Assert.Equal("PlayStation VR2 via SteamVR", psvr2.Describe());
            Assert.True(psvr2.ModelKnown);
            Assert.Equal(new Extent(2000, 2040), psvr2.Known.Panel);
            // SteamVR's last seen model wins when it is in the list...
            var index = HeadsetIdentity.Identify("SteamVR/OpenXR", "SteamVR/OpenXR : playstation_vr2",
                Seen("Valve Corporation", "Index", "playstation_vr2"), Table);
            Assert.Equal("Valve Index (last seen by SteamVR) via SteamVR", index.Describe());
            Assert.Equal(new Extent(1440, 1600), index.Known.Panel);
            // ...and when it is not, the tracking system still gives the panel.
            var sony = HeadsetIdentity.Identify("SteamVR/OpenXR", "SteamVR/OpenXR : playstation_vr2", Seen("Sony", "PS VR2", "playstation_vr2"), Table);
            Assert.Equal("Sony PS VR2 (last seen by SteamVR) via SteamVR", sony.Describe());
            Assert.Equal("PlayStation VR2", sony.Known.Name);
            // Tracking systems shared by many headsets name none.
            foreach (var token in new[] { "lighthouse", "oculus", "aapvr", "pico", "vrlink", "cv" })
            {
                var id = HeadsetIdentity.Identify("SteamVR/OpenXR", "SteamVR/OpenXR : " + token, null, Table);
                Assert.Null(id.Known);
                Assert.False(id.ModelKnown);
            }
        }

        [Fact]
        public void VirtualDesktopInSteamVrModeIsNamedSo()
        {
            var id = HeadsetIdentity.Identify("SteamVR/OpenXR", "SteamVR/OpenXR : oculus", Seen("Oculus", "Meta Quest 3", "oculus_virtualdesktop"), Table);
            Assert.Equal("Meta Quest 3 (last seen by SteamVR) via Virtual Desktop through SteamVR", id.Describe());
            // SteamVR's resolution sets the size there.
            Assert.Equal("SteamVR asks for", id.AsksLabel);
            Assert.Equal(new Extent(2064, 2208), id.Known.Panel);
        }

        [Fact]
        public void SteamVrsLastSeenHeadsetIsReadFromItsSettingsWithoutTheSerial()
        {
            const string Json = @"{ ""LastKnown"" : { ""ActualHMDDriver"" : ""lighthouse"", ""HMDManufacturer"" : ""Valve Corporation"",
                ""HMDModel"" : ""Index"", ""HMDSerialNumber"" : ""LHR-TEST0001"" } }";
            var seen = SteamVrSummary.LastKnown(Json);
            Assert.Equal("Valve Corporation", seen.Manufacturer);
            Assert.Equal("Index", seen.Model);
            Assert.Equal("lighthouse", seen.Driver);
            Assert.Null(SteamVrSummary.LastKnown(@"{ ""LastKnown"" : { ""ActualHMDDriver"" : ""lighthouse"" } }"));
            Assert.Null(SteamVrSummary.LastKnown("not json"));
            Assert.Null(SteamVrSummary.ReadLastKnown(null));
            using (var dir = new TempDir())
            {
                Assert.Null(SteamVrSummary.ReadLastKnown(dir.Path));
                dir.Write("config/steamvr.vrsettings", Json);
                Assert.Equal("Index", SteamVrSummary.ReadLastKnown(dir.Path).Model);
            }
        }
    }
}
