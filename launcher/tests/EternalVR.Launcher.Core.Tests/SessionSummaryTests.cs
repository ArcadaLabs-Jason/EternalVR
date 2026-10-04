using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using EternalVR.Launcher.Core.Headsets;
using EternalVR.Launcher.Core.Launch;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>What a session's headset did, from the layer's 10 s lines (<see cref="SessionSummary"/>). The lines below are in the
    /// layer's own formats, as players' reports show them.</summary>
    public class SessionSummaryTests
    {
        private const string Vd = "VirtualDesktopXR";
        private const string Quest3 = "Meta Quest 3";
        private const string Steam = "SteamVR/OpenXR";

        private static string Xr(double t, long frames, double periodMs) => FormattableString.Invariant(
            $"[{t,9:0.000}] [24384] xr: {frames} frame(s), {Math.Max(0, frames - 5)} new image(s), {Math.Min(frames, 5)} repeat(s); 0 head-tracked, {frames} on the screen; pose age average 0.0 ms, max 0.0 ms; display period {periodMs:0.00} ms, pose lead 0.0 ms");

        private static string Rates(double t, double pairs, double xr) => FormattableString.Invariant(
            $"[{t,9:0.000}] [24384] rates: game {pairs * 2:0.0} present(s)/s, {pairs:0.0} tick(s)/s, {pairs:0.0} stereo pair(s)/s shown; XR {xr:0.0} frame(s)/s, {pairs:0.0} new image(s)/s");

        /// <summary>A session's lines: the runtime and system, the first frame's period, then one window per (period, pairs).</summary>
        private static List<string> Session(string runtime, string system, double firstPeriod, params (double Period, double Pairs)[] windows)
        {
            var lines = new List<string>
            {
                "[    7.657] [10176] xr: runtime '" + runtime + "' 1.0.10, api 1.0",
                "[    7.672] [10176] xr: system '" + system + "', max swapchain 16384x16384",
                "[    7.672] [10176] size: the runtime recommends 2496x2688 per eye (max image 16384x16384, max swapchain 16384x16384, 2 eye(s) side by side); render size 2056x2216 gives 2056x2216",
                Xr(7.8, 1, firstPeriod),
            };
            long frames = 1;
            double t = 7.8;
            foreach (var w in windows)
            {
                t += 10;
                frames += (long)(10000 / w.Period);
                lines.Add(Xr(t, frames, w.Period));
                lines.Add(Rates(t, w.Pairs, 1000 / w.Period));
                lines.Add(FormattableString.Invariant($"[{t,9:0.000}] [24384] size: render size 2056x2216 (swapchain 4112x2216, eye image 2056x2216, window client 1280x720)"));
            }
            return lines;
        }

        private static (double, double)[] Repeat(int n, double period, double pairs) => Enumerable.Repeat((period, pairs), n).ToArray();

        [Fact]
        public void ASteadyHeadsetTheGameKeptUpWith()
        {
            var s = SessionSummary.FromLines(Session(Vd, Quest3, 11.11, Repeat(10, 11.11, 89.6)));
            Assert.Equal(90, s.RefreshHz);
            Assert.Equal(RouteKind.VirtualDesktop, s.Route);
            Assert.Equal(Vd, s.RuntimeName);
            Assert.Equal(Quest3, s.SystemName);
            Assert.True(s.InPlay);
            Assert.Equal(10, s.Windows);
            Assert.Equal(0.0, s.HeldShare);
            Assert.Equal("90 Hz, steady", s.Compact());
            Assert.Equal("The game kept up with your headset: about 90 new frames a second at 90 Hz.", s.Describe());
        }

        [Fact]
        public void BelowTheRefreshRateItSaysTheRateWithoutAdvice()
        {
            var vd = SessionSummary.FromLines(Session(Vd, Quest3, 11.11, Repeat(8, 11.11, 72)));
            Assert.Equal("The game drew about 72 new frames a second at 90 Hz.", vd.Describe());
            var low = SessionSummary.FromLines(Session(Vd, Quest3, 13.89, Repeat(8, 13.89, 50)));
            Assert.Equal("The game drew about 50 new frames a second at 72 Hz.", low.Describe());
            var link = SessionSummary.FromLines(Session("Oculus", "Meta Quest 3", 11.11, Repeat(8, 11.11, 72)));
            Assert.Equal("The game drew about 72 new frames a second at 90 Hz.", link.Describe());
            var other = SessionSummary.FromLines(Session("Windows Mixed Reality Runtime", "WMR", 11.11, Repeat(8, 11.11, 60)));
            Assert.DoesNotContain("lower", other.Describe());
        }

        [Fact]
        public void SteamVrThrottlingIsTheShareOfWindowsAtTwiceThePeriod()
        {
            // A SteamVR headset at 144 Hz (a player's report, 2026-09-30): 6.94 ms, with windows at 13.89 ms while throttled.
            var windows = Repeat(7, 6.94, 110).Concat(Repeat(3, 13.89, 70)).ToArray();
            var s = SessionSummary.FromLines(Session(Steam, "SteamVR/OpenXR : cv", 6.94, windows));
            Assert.Equal(144, s.RefreshHz);
            Assert.Equal(0.3, s.HeldShare, 6);
            Assert.Equal(72, s.HeldHz);
            Assert.Equal("144 Hz, throttled to 72 for 30% of play", s.Compact());
            Assert.Equal("SteamVR throttled the game to 72 of 144 Hz for 30% of play (Motion Smoothing or throttling). "
                + "The game drew about 110 new frames a second at 144 Hz.", s.Describe());
            // A third of the rate counts too.
            var third = SessionSummary.FromLines(Session(Steam, "SteamVR/OpenXR : cv", 6.94, Repeat(9, 6.94, 140).Concat(Repeat(1, 20.83, 45)).ToArray()));
            Assert.Equal(0.1, third.HeldShare, 6);
            Assert.Equal(48, third.HeldHz);
        }

        private static string Vram(double t, int over, int readings, int percent) => FormattableString.Invariant(
            $"[{t,9:0.000}] [28900] vram: the process uses 11433 MB of local video memory, budget 8878 MB ({percent}%); last 10 s: peak 11433 MB, {over} of {readings} reading(s) over the budget");

        [Fact]
        public void VideoMemoryOverTheBudgetIsSaidWithWhatHelps()
        {
            // A Steam Frame on a 12 GB card at Resolution 2.00 with ray tracing (a player's report, 2026-10-04): over all session.
            var lines = Session(Steam, "SteamVR/OpenXR : cv", 8.33, Repeat(10, 8.33, 100));
            for (int i = 0; i < 10; i++) lines.Add(Vram(20 + 10 * i, i < 8 ? 10 : 2, 10, i < 8 ? 129 : 95));
            var s = SessionSummary.FromLines(lines);
            Assert.Equal(10, s.VramWindows);
            Assert.Equal(0.8, s.VramOverShare, 6);
            Assert.EndsWith("The game used more video memory than your graphics card had free for 80% of the session: a lower Resolution, "
                + "or ray tracing off in the game, makes it smoother.", s.Describe());
            Assert.Contains("video memory over the budget in 8 of 10 window(s)", s.LogText());
            // Now and then over the budget, or too few windows: nothing said.
            var rare = Session(Steam, "SteamVR/OpenXR : cv", 8.33, Repeat(10, 8.33, 100));
            for (int i = 0; i < 20; i++) rare.Add(Vram(20 + 10 * i, i == 3 ? 10 : 0, 10, 80));
            Assert.DoesNotContain("video memory", SessionSummary.FromLines(rare).Describe());
            var short_ = Session(Steam, "SteamVR/OpenXR : cv", 8.33, Repeat(10, 8.33, 100));
            short_.Add(Vram(20, 10, 10, 129));
            Assert.DoesNotContain("video memory", SessionSummary.FromLines(short_).Describe());
            // No vram line (an older layer): nothing said, none counted.
            Assert.Equal(0, SessionSummary.FromLines(Session(Steam, "SteamVR/OpenXR : cv", 8.33, Repeat(10, 8.33, 100))).VramWindows);
        }

        [Fact]
        public void VirtualDesktopsSswAndMetasAswAreNamed()
        {
            var windows = Repeat(6, 11.11, 89).Concat(Repeat(4, 22.22, 44)).ToArray();
            var vd = SessionSummary.FromLines(Session(Vd, Quest3, 11.11, windows));
            Assert.Equal("90 Hz, held to 45 for 40% of play", vd.Compact());
            Assert.StartsWith("Virtual Desktop's SSW held the game to 45 of 90 Hz for 40% of play. The game kept up with your headset", vd.Describe());
            var link = SessionSummary.FromLines(Session("Oculus", Quest3, 11.11, windows));
            Assert.StartsWith("Meta's ASW held the game to 45 of 90 Hz for 40% of play.", link.Describe());
            var pimax = SessionSummary.FromLines(Session("Pimax OpenXR", "Pimax Crystal", 11.11, windows));
            Assert.StartsWith("Pimax's Smart Smoothing held the game to 45 of 90 Hz", pimax.Describe());
            var other = SessionSummary.FromLines(Session("Varjo OpenXR Runtime", "XR-4", 11.11, windows));
            Assert.StartsWith("The runtime held the game to 45 of 90 Hz for 40% of play (its reprojection or throttling).", other.Describe());
        }

        [Fact]
        public void ASteadyPeriodThatIsNoMultipleIsAnotherRefreshRate()
        {
            // The same player's session of 2026-09-29: 144 Hz, four windows at 90 Hz, throttled windows besides.
            var windows = Repeat(6, 6.94, 120).Concat(Repeat(4, 11.11, 85)).Concat(Repeat(2, 13.89, 70)).ToArray();
            var s = SessionSummary.FromLines(Session(Steam, "SteamVR/OpenXR : cv", 6.94, windows));
            Assert.Equal(144, s.RefreshHz);
            Assert.Equal(90, Assert.Single(s.OtherRates).Key);
            Assert.Equal("varied 90-144 Hz, mostly 144", s.Compact());
            Assert.StartsWith("The headset ran at 144 Hz, and at 90 Hz for 40 seconds. SteamVR throttled the game to 72 of 144 Hz for 17% of play", s.Describe());
            // Mostly at the other rate, and long enough to say minutes.
            var mostly = SessionSummary.FromLines(Session(Steam, "SteamVR/OpenXR : cv", 6.94, Repeat(3, 6.94, 140).Concat(Repeat(12, 11.11, 88)).ToArray()));
            Assert.Equal("varied 90-144 Hz, mostly 90", mostly.Compact());
            Assert.StartsWith("The headset ran at 144 Hz, and at 90 Hz for about 2 minutes.", mostly.Describe());
        }

        [Fact]
        public void OneOddWindowNeitherSetsNorChangesTheRefreshRate()
        {
            var windows = Repeat(5, 11.11, 89).Concat(new[] { (6.5, 89.0) }).ToArray();
            var s = SessionSummary.FromLines(Session(Vd, Quest3, 11.11, windows));
            Assert.Equal(90, s.RefreshHz);
            Assert.Empty(s.OtherRates);
            Assert.Equal("90 Hz, steady", s.Compact());
        }

        [Fact]
        public void AGameThatNeverPlayedIsSummedUpOverTheWholeSession()
        {
            // A player's report (2026-10-01): VDXR started at 72 Hz, then halved its rate while the game drew nothing.
            var s = SessionSummary.FromLines(Session(Vd, Quest3, 13.89, Repeat(8, 27.78, 0)));
            Assert.Equal(72, s.RefreshHz);
            Assert.False(s.InPlay);
            Assert.Null(s.GameRate);
            Assert.Equal("72 Hz, held to 36 for 100% of the session", s.Compact());
            Assert.Equal("Virtual Desktop's SSW held the game to 36 of 72 Hz for 100% of the session.", s.Describe());
        }

        [Fact]
        public void TheFirstFramesPeriodAloneGivesTheRefreshRate()
        {
            var s = SessionSummary.FromLines(Session(Vd, Quest3, 13.89));
            Assert.Equal(72, s.RefreshHz);
            Assert.Equal(0, s.Windows);
            Assert.Equal("72 Hz, steady", s.Compact());
            Assert.Equal(string.Empty, s.Describe());
            // No display period at all: no VR session to sum up.
            Assert.Null(SessionSummary.FromLines(new[] { "[    1.000] [1] layer: loaded", "[    6.157] [4124] pace: off" }));
            Assert.Null(SessionSummary.FromLines(null));
            Assert.Contains("session refresh: 72 Hz", s.LogText());
        }

        [Fact]
        public void ItIsReadFromTheSessionFolderAndKeptInHeadsetTxt()
        {
            using (var dir = new TempDir())
            {
                // Two log files of one session, in time order by name.
                var first = Session(Vd, Quest3, 11.11, Repeat(2, 11.11, 89));
                File.WriteAllLines(Path.Combine(dir.Path, "eternalvr-20261001-091200-100.log"), first);
                File.WriteAllLines(Path.Combine(dir.Path, "eternalvr-20261001-091500-200.log"), Session(Vd, Quest3, 11.11, Repeat(2, 11.11, 89)).Skip(3));
                var s = SessionSummary.FromSessionDir(dir.Path);
                Assert.Equal(90, s.RefreshHz);
                Assert.Equal(4, s.Windows);
                Assert.Equal(first.Count + first.Count - 3, SessionRates.ReadLayerLogs(dir.Path).Count);
                Assert.Null(SessionSummary.FromSessionDir(Path.Combine(dir.Path, "missing")));

                var ended = new DateTime(2026, 10, 1, 9, 40, 0);
                var facts = LastHeadset.AfterSession(new HeadsetFacts { SystemName = Quest3 }, s, "20261001-091158", ended);
                var back = LastHeadset.Parse(LastHeadset.Serialize(facts));
                Assert.Equal("20261001-091158", back.Session);
                Assert.Equal(ended, back.SessionAt);
                Assert.Equal(Vd, back.SessionRuntime);
                Assert.Equal(90, back.SessionRefreshHz);
                Assert.Equal("90 Hz, steady", back.SessionRefresh);
                Assert.Equal("The game kept up with your headset: about 89 new frames a second at 90 Hz.", back.SessionText);
                Assert.Equal(Quest3, back.SystemName);
                // A session without a display period keeps the last one's.
                Assert.Equal("20261001-091158", LastHeadset.AfterSession(back, null, "20261001-100000", ended.AddHours(1)).Session);
            }
        }
    }
}
