using System;
using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Headsets;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Report;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>The headset as read and the last session in the report's system.txt (<see cref="HeadsetView.ReportLines"/>).</summary>
    public class HeadsetReportTests
    {
        private const string Profile = @"C:\Users\TestPlayer";

        private static HeadsetFacts Facts() => new HeadsetFacts
        {
            Limits = new ViewLimits { Recommended = new Extent(2496, 2688), MaxImageRect = new Extent(16384, 16384), MaxSwapchain = new Extent(16384, 16384) },
            RuntimeName = "VirtualDesktopXR",
            SystemName = "Meta Quest 3",
            RuntimeManifest = Profile + @"\Runtimes\virtualdesktop-openxr.json",
            ReadAt = new DateTime(2026, 10, 1, 9, 12, 30),
            Session = "20261001-091158",
            SessionAt = new DateTime(2026, 10, 1, 9, 40, 0),
            SessionRuntime = "VirtualDesktopXR",
            SessionRefreshHz = 90,
            SessionRefresh = "90 Hz, steady",
            SessionText = "The game kept up with your headset: about 90 new frames a second at 90 Hz.",
        };

        private static Dictionary<string, string> Lines(HeadsetFacts f) =>
            HeadsetView.ReportLines(f, HeadsetIdentity.Identify(f.RuntimeName, f.SystemName, null, HeadsetTable.Parse(TestData.Read("headsets.txt"))))
                .ToDictionary(kv => kv.Key, kv => kv.Value);

        [Fact]
        public void TheHeadsetAsReadAndTheLastSession()
        {
            var lines = Lines(Facts());
            Assert.Equal("Meta Quest 3 via Virtual Desktop (VDXR)", lines["headset"]);
            Assert.Equal("runtime 'VirtualDesktopXR', system 'Meta Quest 3'", lines["headset as read"]);
            Assert.Equal("Meta Quest 3, 2064x2208 per eye", lines["headset native panel"]);
            Assert.Equal("2496x2688 per eye (max image 16384x16384, max swapchain 16384x16384)", lines["headset asks for"]);
            Assert.Equal("2026-10-01 09:12:30 at Launch VR with " + Profile + @"\Runtimes\virtualdesktop-openxr.json", lines["headset read"]);
            Assert.False(lines.ContainsKey("headset last failed try"));
            Assert.Equal("20261001-091158, ended 2026-10-01 09:40:00, runtime 'VirtualDesktopXR': 90 Hz, steady", lines["last session"]);
            Assert.Equal("The game kept up with your headset: about 90 new frames a second at 90 Hz.", lines["last session summary"]);
        }

        [Fact]
        public void NothingReadYetAndAFailedTry()
        {
            var none = Lines(new HeadsetFacts());
            Assert.Equal("not read yet", none["headset"]);
            Assert.Equal("none with a display period in its log", none["last session"]);
            Assert.False(none.ContainsKey("headset asks for"));
            var failed = Facts();
            failed.FailedAt = new DateTime(2026, 10, 1, 10, 0, 0);
            failed.FailedReason = "the runtime reports no headset";
            Assert.Equal("2026-10-01 10:00:00: the runtime reports no headset", Lines(failed)["headset last failed try"]);
            var older = Lines(new HeadsetFacts { Limits = Facts().Limits });
            Assert.Equal("not named (read by an older launcher)", older["headset"]);
            Assert.Equal("at an earlier launch", older["headset read"]);
            Assert.Equal("not in the list", older["headset native panel"]);
        }

        [Fact]
        public void SystemTxtHoldsThemRedacted()
        {
            using (var t = new TempDir())
            {
                var report = ReportBuilder.Build(new ReportInputs
                {
                    DataRoot = t.Combine("data"),
                    ProgramDir = t.Combine("program"),
                    System = HeadsetView.ReportLines(Facts(), null),
                    UserProfile = Profile,
                    UserName = "TestPlayer",
                    Now = new DateTime(2026, 10, 1, 12, 0, 0),
                });
                var system = report.Files.Single(f => f.ZipPath == ReportManifest.SystemFile).Text;
                Assert.Contains("headset: Meta Quest 3 via Virtual Desktop (VDXR)\n", system);
                Assert.Contains("last session summary: The game kept up with your headset", system);
                Assert.DoesNotContain(Profile, system);
                Assert.DoesNotContain("TestPlayer", system);
            }
        }
    }
}
