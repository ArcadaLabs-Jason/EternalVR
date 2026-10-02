using System;
using System.Collections.Generic;
using EternalVR.Launcher.Core.Headsets;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>Whether the launcher reads the headset by itself when it opens (<see cref="HeadsetAutoRead"/>).</summary>
    public class HeadsetAutoReadTests
    {
        private static Func<string, bool> Running(params string[] names)
        {
            var set = new HashSet<string>(names, StringComparer.OrdinalIgnoreCase);
            return set.Contains;
        }

        [Fact]
        public void VirtualDesktopsRuntimeIsReadWhenTheStreamerRuns()
        {
            Assert.Null(HeadsetAutoRead.WhyNot("VirtualDesktopXR (Bundled)", Running("VirtualDesktop.Streamer"), true));
            Assert.Equal("the Virtual Desktop Streamer is not running",
                HeadsetAutoRead.WhyNot("VirtualDesktopXR (Bundled)", Running("vrserver", "OVRServer_x64"), true));
        }

        [Fact]
        public void MetasRuntimeIsReadWhenItsServerRuns()
        {
            Assert.Null(HeadsetAutoRead.WhyNot("Oculus OpenXR", Running("OVRServer_x64"), true));
            Assert.Equal("Meta's Link software is not running", HeadsetAutoRead.WhyNot("Oculus OpenXR", Running("VirtualDesktop.Streamer"), true));
        }

        [Fact]
        public void SteamVrIsReadOnlyWhenItRunsAlready()
        {
            // Virtual Desktop in SteamVR mode is SteamVR's runtime: read only once SteamVR runs, whatever VD does.
            Assert.Null(HeadsetAutoRead.WhyNot("SteamVR", Running("vrserver"), true));
            Assert.Equal("SteamVR is not running",
                HeadsetAutoRead.WhyNot("SteamVR", Running("VirtualDesktop.Streamer", "steam"), true));
        }

        [Fact]
        public void OtherRuntimesAndMissingPiecesAreNotRead()
        {
            Assert.Equal("Pimax Play is read only when you launch", HeadsetAutoRead.WhyNot("Pimax OpenXR", Running("anything"), true));
            Assert.Equal("Varjo Base is read only when you launch", HeadsetAutoRead.WhyNot("Varjo OpenXR Runtime", Running(), true));
            Assert.Equal("My Runtime is read only when you launch", HeadsetAutoRead.WhyNot("My Runtime", Running(), true));
            Assert.Equal("no OpenXR runtime is set", HeadsetAutoRead.WhyNot(null, Running("vrserver"), true));
            Assert.Equal("the EternalVR layer's OpenXR loader is missing", HeadsetAutoRead.WhyNot("SteamVR", Running("vrserver"), false));
        }

        [Fact]
        public void EachRouteHasItsProcess()
        {
            Assert.Equal("VirtualDesktop.Streamer", HeadsetAutoRead.ProcessOf(RouteKind.VirtualDesktop));
            Assert.Equal("OVRServer_x64", HeadsetAutoRead.ProcessOf(RouteKind.MetaLink));
            Assert.Equal("vrserver", HeadsetAutoRead.ProcessOf(RouteKind.SteamVr));
            Assert.Null(HeadsetAutoRead.ProcessOf(RouteKind.PimaxPlay));
            Assert.Null(HeadsetAutoRead.ProcessOf(RouteKind.Unknown));
        }
    }
}
