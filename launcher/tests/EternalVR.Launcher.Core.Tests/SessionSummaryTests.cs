using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
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
        public void AHeadsetThatTookFewerFramesThanItsRefreshRateIsNotSteady()
        {
            // A player's session: the display period said 90 Hz all along, the headset took 77 to 81 frames a second.
            var lines = Session(Vd, Quest3, 11.11);
            double t = 7.8;
            long frames = 1;
            foreach (var xr in new[] { 79.0, 81.0, 77.0, 80.0, 81.0 })
            {
                t += 10;
                frames += (long)(xr * 10);
                lines.Add(Xr(t, frames, 11.11));
                lines.Add(Rates(t, 70, xr));
            }
            var s = SessionSummary.FromLines(lines);
            Assert.Equal(90, s.RefreshHz);
            Assert.Equal(0.0, s.HeldShare);
            Assert.Equal(80.0, s.HeadsetRate.Value, 6);
            Assert.False(s.Steady);
            Assert.Equal("90 Hz, not steady: about 80 frames a second", s.Compact());
            Assert.Equal("The headset took about 80 frames a second at 90 Hz (not steady). The game drew about 70 new frames a second at 90 Hz.", s.Describe());
            Assert.Contains("headset frames/s at the refresh rate 80.0", s.LogText());

            // Within 5% of the refresh rate it is steady.
            var near = SessionSummary.FromLines(lines.Select(l => l.Contains("] rates: ") ? Regex.Replace(l, @"XR [0-9.]+ frame", "XR 86.0 frame") : l));
            Assert.Equal(86.0, near.HeadsetRate.Value, 6);
            Assert.True(near.Steady);
            Assert.Equal("90 Hz, steady", near.Compact());
            Assert.Equal("The game drew about 70 new frames a second at 90 Hz.", near.Describe());
            // An older layer's rates line without the XR rate: the display period alone, as before.
            var older = SessionSummary.FromLines(lines.Select(l => l.Contains("] rates: ") ? l.Substring(0, l.IndexOf("; XR", StringComparison.Ordinal)) : l));
            Assert.Null(older.HeadsetRate);
            Assert.Equal("90 Hz, steady", older.Compact());
            Assert.Contains("headset frames/s at the refresh rate unknown", older.LogText());
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
            Assert.Equal("144 Hz, throttled to 72 for 30% of a short session", s.Compact());
            Assert.Equal("SteamVR throttled the game to 72 of 144 Hz for 30% of a short session (Motion Smoothing or throttling). "
                + "The game drew about 110 new frames a second at 144 Hz.", s.Describe());
            // A third of the rate counts too.
            var third = SessionSummary.FromLines(Session(Steam, "SteamVR/OpenXR : cv", 6.94, Repeat(9, 6.94, 140).Concat(Repeat(1, 20.83, 45)).ToArray()));
            Assert.Equal(0.1, third.HeldShare, 6);
            Assert.Equal(48, third.HeldHz);
        }

        private static string Vram(double t, int over, int readings, int percent) => FormattableString.Invariant(
            $"[{t,9:0.000}] [28900] vram: the process uses {8878 * percent / 100} MB of local video memory, budget 8878 MB ({percent}%); last 10 s: peak 11433 MB, {over} of {readings} reading(s) over the budget");

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

        private static string VramUse(double t, int use, int budget) => FormattableString.Invariant(
            $"[{t,9:0.000}] [48148] vram: the process uses {use} MB of local video memory, budget {budget} MB ({use * 100 / budget}%); last 10 s: peak {use} MB, 0 of 10 reading(s) over the budget");

        [Fact]
        public void VideoMemoryNearTheBudgetWarnsAboutHeadsetMenus()
        {
            // Issue #19 (a player's 0.1.29 export, 2026-10-07): a 12 GB card at 98% of an 8.9 GB budget, never over it; the
            // SteamVR dashboard reset the driver, a lower texture pool fixed it.
            var lines = Session(Steam, "SteamVR/OpenXR : oculus", 11.11, Repeat(10, 11.11, 62));
            for (int i = 0; i < 10; i++) lines.Add(VramUse(20 + 10 * i, 8742, i < 9 ? 8926 : 9900));
            var s = SessionSummary.FromLines(lines);
            Assert.Equal(0.0, s.VramOverShare, 6);
            Assert.Equal(0.9, s.VramNearShare, 6);
            Assert.EndsWith("The game used nearly all the video memory Windows gives it for 90% of the session, so a headset menu "
                + "opening over it (like SteamVR's dashboard) can reset the graphics driver: a lower Texture Pool Size in the game's "
                + "video settings makes room.", s.Describe());
            Assert.Contains("video memory over the budget in 0 of 10 window(s), at 95% of it or more in 9", s.LogText());
            // At 87% of the budget (the rig's SteamVR runs): nothing said.
            var roomy = Session(Steam, "SteamVR/OpenXR : oculus", 11.11, Repeat(10, 11.11, 62));
            for (int i = 0; i < 10; i++) roomy.Add(VramUse(20 + 10 * i, 9131, 10504));
            Assert.DoesNotContain("video memory", SessionSummary.FromLines(roomy).Describe());
            // Near it for under half the session: nothing said.
            var some = Session(Steam, "SteamVR/OpenXR : oculus", 11.11, Repeat(10, 11.11, 62));
            for (int i = 0; i < 10; i++) some.Add(VramUse(20 + 10 * i, 8742, i < 4 ? 8926 : 10504));
            Assert.DoesNotContain("video memory", SessionSummary.FromLines(some).Describe());
        }

        [Fact]
        public void VirtualDesktopsSswAndMetasAswAreNamed()
        {
            var windows = Repeat(6, 11.11, 89).Concat(Repeat(4, 22.22, 44)).ToArray();
            var vd = SessionSummary.FromLines(Session(Vd, Quest3, 11.11, windows));
            Assert.Equal("90 Hz, held to 45 for 40% of a short session", vd.Compact());
            Assert.StartsWith("Virtual Desktop's SSW held the game to 45 of 90 Hz for 40% of a short session. The game kept up with your headset", vd.Describe());
            var link = SessionSummary.FromLines(Session("Oculus", Quest3, 11.11, windows));
            Assert.StartsWith("Meta's ASW held the game to 45 of 90 Hz for 40% of a short session.", link.Describe());
            var pimax = SessionSummary.FromLines(Session("Pimax OpenXR", "Pimax Crystal", 11.11, windows));
            Assert.StartsWith("Pimax's Smart Smoothing held the game to 45 of 90 Hz", pimax.Describe());
            var other = SessionSummary.FromLines(Session("Varjo OpenXR Runtime", "XR-4", 11.11, windows));
            Assert.StartsWith("The runtime held the game to 45 of 90 Hz for 40% of a short session (its reprojection or throttling).", other.Describe());
        }

        [Fact]
        public void ALongSessionSaysItsShareOfPlay()
        {
            // Ten minutes of windows and more: of play; fewer: of a short session.
            var windows = Repeat(48, 11.11, 89).Concat(Repeat(12, 22.22, 44)).ToArray();
            var s = SessionSummary.FromLines(Session(Vd, Quest3, 11.11, windows));
            Assert.Equal(SessionSummary.ShortSessionWindows, s.Windows);
            Assert.False(s.HeldByTime);
            Assert.Equal("90 Hz, held to 45 for 20% of play", s.Compact());
            Assert.Contains("held to 45 Hz in 20.0% of the windows", s.LogText());
            var shorter = SessionSummary.FromLines(Session(Vd, Quest3, 11.11, windows.Skip(1).ToArray()));
            Assert.EndsWith("of a short session", shorter.Compact());
        }

        private static string RefreshSummary(double t, string shares, bool end = false) => FormattableString.Invariant(
            $"[{t,9:0.000}] [ 9488] xr: refresh summary{(end ? " at session end" : string.Empty)}: base 11.11 ms (90 Hz); {shares}; 533 change(s) in {t - 12:0} s");

        [Fact]
        public void TheLayersTimeBasedSummaryGivesTheHeldShare()
        {
            // A player's 0.1.33 export (WMR, 90 Hz, 13 minutes): 12 of 75 windows (16%) caught at 45 Hz, 9.3% of the time.
            var lines = Session("Windows Mixed Reality Runtime", "Windows Mixed Reality", 11.11, Repeat(63, 11.11, 85).Concat(Repeat(12, 22.22, 44)).ToArray());
            lines.Add(RefreshSummary(732.141, "1x 90.3%, 2x 9.6%, 4x 0.1%"));
            lines.Add(RefreshSummary(786.844, "1x 90.6%, 2x 9.3%, 4x 0.1%", end: true));
            var s = SessionSummary.FromLines(lines);
            Assert.True(s.HeldByTime);
            Assert.Equal(0.094, s.HeldShare, 6);
            Assert.Equal(45, s.HeldHz);
            Assert.Equal("90 Hz, held to 45 for 9% of the session", s.Compact());
            Assert.StartsWith("The runtime held the game to 45 of 90 Hz for 9% of the session (its reprojection or throttling).", s.Describe());
            Assert.Contains("held to 45 Hz in 9.4% of the time (the layer's refresh summary)", s.LogText());
            // Next to no time held: steady, whatever the windows caught.
            var steady = Session(Vd, Quest3, 11.11, Repeat(8, 11.11, 89).Concat(Repeat(2, 22.22, 44)).ToArray());
            steady.Add(RefreshSummary(120, "1x 99.7%, 2x 0.3%", end: true));
            Assert.Equal("90 Hz, steady", SessionSummary.FromLines(steady).Compact());
            // A summary whose base is not the refresh rate found (a session that started throttled): the windows count.
            var other = Session(Vd, Quest3, 11.11, Repeat(6, 11.11, 89).Concat(Repeat(4, 22.22, 44)).ToArray());
            other.Add(RefreshSummary(120, "1x 100.0%", end: true).Replace("base 11.11 ms (90 Hz)", "base 22.22 ms (45 Hz)"));
            var windowsOnly = SessionSummary.FromLines(other);
            Assert.False(windowsOnly.HeldByTime);
            Assert.Equal("90 Hz, held to 45 for 40% of a short session", windowsOnly.Compact());
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
            Assert.StartsWith("The headset ran at 144 Hz, and at 90 Hz for 40 seconds. SteamVR throttled the game to 72 of 144 Hz for 17% of a short session", s.Describe());
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
            Assert.Equal("72 Hz, held to 36 for 100% of a short session", s.Compact());
            Assert.Equal("Virtual Desktop's SSW held the game to 36 of 72 Hz for 100% of a short session.", s.Describe());
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
