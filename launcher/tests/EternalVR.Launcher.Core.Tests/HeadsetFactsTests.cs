using System;
using EternalVR.Launcher.Core.Launch;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>What <c>headset.txt</c> keeps of the runtime probe (<see cref="LastHeadset"/>, <see cref="HeadsetFacts"/>).</summary>
    public class HeadsetFactsTests
    {
        private static OpenXrProbeResult Answer(string runtime, string system, uint recW, uint recH) => new OpenXrProbeResult
        {
            RuntimeName = runtime,
            SystemName = system,
            Limits = new ViewLimits
            {
                Recommended = new Extent(recW, recH),
                MaxImageRect = new Extent(16384, 16384),
                MaxSwapchain = new Extent(16384, 16384),
            },
        };

        private static readonly DateTime At = new DateTime(2026, 10, 1, 9, 12, 30);
        private const string VdManifest = @"D:\Runtimes\VD\virtualdesktop-openxr.json";

        [Fact]
        public void AnOlderLaunchersFileKeepsItsSizesAndKnowsNothingElse()
        {
            // As launcher 0.1.11 wrote it.
            var facts = LastHeadset.Parse(
                "# EternalVR launcher: the headset's sizes from the last runtime probe that answered (the Play tab's Each eye line)\n"
                + "recommended = 2496x2688\nmax_image = 16384x16384\nmax_swapchain = 16384x16384\n");
            Assert.Equal(new Extent(2496, 2688), facts.Limits.Recommended);
            Assert.Equal(new Extent(16384, 16384), facts.Limits.MaxSwapchain);
            Assert.Null(facts.RuntimeName);
            Assert.Null(facts.SystemName);
            Assert.Null(facts.RuntimeManifest);
            Assert.Null(facts.ReadAt);
            Assert.Null(facts.FailedAt);
            // Written back, it gains nothing it does not know.
            var back = LastHeadset.Serialize(facts);
            Assert.Contains("recommended = 2496x2688\n", back);
            Assert.DoesNotContain("runtime", back.Substring(back.IndexOf('\n')));
            Assert.DoesNotContain("read_at", back);
        }

        [Fact]
        public void AProbeThatAnswersKeepsTheRuntimeTheSystemTheTimeAndTheManifest()
        {
            var facts = LastHeadset.After(null, Answer("VirtualDesktopXR", "Meta Quest 3", 2496, 2688), VdManifest, At, HeadsetReadBy.Launch);
            Assert.Equal("VirtualDesktopXR", facts.RuntimeName);
            Assert.Equal("Meta Quest 3", facts.SystemName);
            Assert.Equal(VdManifest, facts.RuntimeManifest);
            Assert.Equal(At, facts.ReadAt);
            Assert.Equal(HeadsetReadBy.Launch, facts.ReadBy);
            Assert.Equal(new Extent(2496, 2688), facts.Limits.Recommended);

            var text = LastHeadset.Serialize(facts);
            Assert.Contains("runtime = VirtualDesktopXR\n", text);
            Assert.Contains("system = Meta Quest 3\n", text);
            Assert.Contains("runtime_manifest = " + VdManifest + "\n", text);
            Assert.Contains("read_at = 2026-10-01 09:12:30\n", text);
            Assert.Contains("read_by = launch\n", text);
            var back = LastHeadset.Parse(text);
            Assert.Equal("VirtualDesktopXR", back.RuntimeName);
            Assert.Equal("Meta Quest 3", back.SystemName);
            Assert.Equal(VdManifest, back.RuntimeManifest);
            Assert.Equal(At, back.ReadAt);
            Assert.Equal(HeadsetReadBy.Launch, back.ReadBy);
            Assert.Equal(new Extent(2496, 2688), back.Limits.Recommended);
            Assert.Equal(1u, back.Limits.EyesSideBySide);

            // Detect again on SteamVR later: the new answer replaces all of it.
            var steam = LastHeadset.After(back, Answer("SteamVR/OpenXR", "SteamVR/OpenXR : lighthouse", 3188, 3540), @"D:\Steam\steamxr_win64.json",
                At.AddDays(1), HeadsetReadBy.Detect);
            Assert.Equal("SteamVR/OpenXR : lighthouse", steam.SystemName);
            Assert.Equal(HeadsetReadBy.Detect, LastHeadset.Parse(LastHeadset.Serialize(steam)).ReadBy);
            // The read when the launcher opens is kept as such; a value an older launcher never wrote reads as Launch VR's.
            var start = LastHeadset.After(back, Answer("VirtualDesktopXR", "Meta Quest 3", 2496, 2688), VdManifest, At.AddDays(2), HeadsetReadBy.Start);
            Assert.Contains("read_by = start\n", LastHeadset.Serialize(start));
            Assert.Equal(HeadsetReadBy.Start, LastHeadset.Parse(LastHeadset.Serialize(start)).ReadBy);
            Assert.Equal(HeadsetReadBy.Launch, LastHeadset.Parse("recommended = 2496x2688\nread_by = later\n").ReadBy);
            // The old facts are not changed in place.
            Assert.Equal("Meta Quest 3", back.SystemName);
        }

        [Fact]
        public void AProbeThatFailsKeepsTheOldValuesAndSaysTheyAreOld()
        {
            var good = LastHeadset.After(null, Answer("VirtualDesktopXR", "Meta Quest 3", 2496, 2688), VdManifest, At, HeadsetReadBy.Launch);
            var failed = LastHeadset.After(good, OpenXrProbeResult.Failed("xrGetSystem: XR_ERROR_FORM_FACTOR_UNAVAILABLE", true), VdManifest,
                At.AddHours(2), HeadsetReadBy.Detect);
            Assert.Equal(new Extent(2496, 2688), failed.Limits.Recommended);
            Assert.Equal("Meta Quest 3", failed.SystemName);
            Assert.Equal(At, failed.ReadAt);
            Assert.Equal(At.AddHours(2), failed.FailedAt);
            Assert.Equal("the runtime reports no headset", failed.FailedReason);
            var back = LastHeadset.Parse(LastHeadset.Serialize(failed));
            Assert.Equal(At.AddHours(2), back.FailedAt);
            Assert.Equal("the runtime reports no headset", back.FailedReason);
            // Another failure says why.
            Assert.Equal("xrCreateInstance: XR_ERROR_RUNTIME_UNAVAILABLE",
                LastHeadset.After(good, OpenXrProbeResult.Failed("xrCreateInstance: XR_ERROR_RUNTIME_UNAVAILABLE"), VdManifest, At, HeadsetReadBy.Launch).FailedReason);
            // The next answer clears it.
            var again = LastHeadset.After(failed, Answer("VirtualDesktopXR", "Meta Quest 3", 2112, 2304), VdManifest, At.AddHours(3), HeadsetReadBy.Launch);
            Assert.Null(again.FailedAt);
            Assert.Null(again.FailedReason);
            Assert.Equal(new Extent(2112, 2304), again.Limits.Recommended);
        }

        [Fact]
        public void TheFactsSurviveASaveAndARead()
        {
            using (var dir = new TempDir())
            {
                var path = dir.Combine("headset.txt");
                Assert.Null(LastHeadset.Read(path).Limits);
                var facts = LastHeadset.After(null, Answer("VirtualDesktopXR", "Meta Quest 3", 2496, 2688), VdManifest, At, HeadsetReadBy.Launch);
                facts.Limits.MaxSwapchain = new Extent(0, 0);
                LastHeadset.Save(path, facts);
                var loaded = LastHeadset.Read(path);
                Assert.Equal(new Extent(2496, 2688), loaded.Limits.Recommended);
                Assert.Equal(new Extent(16384, 16384), loaded.Limits.MaxImageRect);
                Assert.Equal(new Extent(0, 0), loaded.Limits.MaxSwapchain);
                Assert.Equal("Meta Quest 3", loaded.SystemName);
                Assert.Equal(At, loaded.ReadAt);

                // A file without a usable recommendation is no remembered size.
                System.IO.File.WriteAllText(path, "recommended = big\nmax_image = 4096x4096\n");
                Assert.Null(LastHeadset.Read(path).Limits);
                System.IO.File.WriteAllText(path, "recommended = 0x2688\n");
                Assert.Null(LastHeadset.Read(path).Limits);
            }
        }

        [Fact]
        public void AMissingFileIsNoFactsAndOddValuesStayHarmless()
        {
            using (var dir = new TempDir())
            {
                var facts = LastHeadset.Read(dir.Combine("missing", "headset.txt"));
                Assert.NotNull(facts);
                Assert.Null(facts.Limits);
                Assert.Null(facts.RuntimeName);
            }
            // A name with an equals sign or a bar keeps them; a bad time is no time.
            var odd = LastHeadset.Parse("runtime = A=B|C\nread_at = yesterday\n");
            Assert.Equal("A=B|C", odd.RuntimeName);
            Assert.Null(odd.ReadAt);
            // A name with a line break stays on one line.
            var text = LastHeadset.Serialize(new HeadsetFacts { SystemName = "Two\nLines" });
            Assert.Equal("Two Lines", LastHeadset.Parse(text).SystemName);
        }
    }
}
