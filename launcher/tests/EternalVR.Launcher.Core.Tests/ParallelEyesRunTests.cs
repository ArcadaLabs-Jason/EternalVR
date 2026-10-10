using System;
using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Headsets;
using EternalVR.Launcher.Core.Launch;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>Whether Parallel Eye Rendering ran, or fell back and why, from the layer's lines (<see cref="ParallelEyesRun"/>), in the
    /// session summary, the last session's facts and the report. The lines are in the layer's own formats (view_install.cpp).</summary>
    public class ParallelEyesRunTests
    {
        private const string On =
            "[    0.412] [10176] parallel eyes: on: both eyes as two views of one render (29 slot sites, 8 occlusion sites); async compute off; "
            + "view 1's clones on; eye copy each view's screen pass; parts off (ETERNALVR_TEST_VIEW_OFF): none; a present carries the view of "
            + "the frame it shows; a present whose eye 1 would repeat its last copy is not shown (the last pair stays)";
        private const string OffDlss = "[    0.401] [10176] parallel eyes: off: not with DLSS (ETERNALVR_STEREO_DLSS=1); the standard renderer";
        private const string OffCheck = "[    0.405] [10176] parallel eyes: off: a check failed (above); the game is unchanged, the standard renderer";
        private const string OffGuard = "[    0.403] [10176] parallel eyes: off: the multiplayer guard is not armed; the standard renderer";
        private const string NotAvailable =
            "[    0.402] [10176] parallel eyes: not available for this game version (Game Pass 1.0.56.0, timestamp 0x69BC663D); the standard renderer";
        private const string Failed =
            "[    0.420] [10176] parallel eyes: FAILED after the engine was changed (above): not on; both eyes show the same image (view 0's) "
            + "for this session, and the standard renderer does not run on the changed engine";
        private const string Trip = "[  812.004] [24384] parallel eyes: the multiplayer guard has tripped: view 0 alone from now on";
        private const string ClonesOff =
            "[  301.250] [24384] view-clones: 8 builds and the engine's targets still change; clones off, view 0 alone from now on";
        private const string Async =
            "[    2.118] [10176] parallel eyes: the game's device has an async compute queue (family 2) although the layer turned async "
            + "compute off (read seen); view 1 is not rendered, one eye";
        private const string AsyncKept = "[    2.118] [10176] parallel eyes: the game's device has an async compute queue (family 2), kept for the test";
        private const string Other = "[    0.430] [10176] parallel eyes: async compute off (the device setup read r_enableAsyncCompute 1 as 0): both eyes";

        private static ParallelEyesRun Run(params string[] lines) => ParallelEyesRun.FromLines(lines);

        /// <summary>A short session the summary counts: the runtime, the first frame, ten steady 90 Hz windows, and the given lines.</summary>
        private static List<string> Session(params string[] extra)
        {
            var lines = new List<string>(extra)
            {
                "[    7.657] [10176] xr: runtime 'VirtualDesktopXR' 1.0.10, api 1.0",
                "[    7.672] [10176] xr: system 'Meta Quest 3', max swapchain 16384x16384",
                "[    7.800] [24384] xr: 1 frame(s), 0 new image(s), 1 repeat(s); 0 head-tracked, 1 on the screen; pose age average 0.0 ms, max 0.0 ms; display period 11.11 ms, pose lead 0.0 ms",
            };
            long frames = 1;
            for (int i = 1; i <= 10; i++)
            {
                frames += 900;
                lines.Add(FormattableString.Invariant(
                    $"[{7.8 + 10 * i,9:0.000}] [24384] xr: {frames} frame(s), {frames - 5} new image(s), 5 repeat(s); 0 head-tracked, {frames} on the screen; pose age average 0.0 ms, max 0.0 ms; display period 11.11 ms, pose lead 0.0 ms"));
                lines.Add(FormattableString.Invariant(
                    $"[{7.8 + 10 * i,9:0.000}] [24384] rates: game 179.2 present(s)/s, 89.6 tick(s)/s, 89.6 stereo pair(s)/s shown; XR 90.0 frame(s)/s, 89.6 new image(s)/s"));
            }
            return lines;
        }

        [Fact]
        public void NotAskedWithoutItsLines()
        {
            var run = Run("[    0.100] [10176] xr: runtime 'VirtualDesktopXR' 1.0.10, api 1.0", Other);
            Assert.Equal(ParallelEyesState.NotAsked, run.State);
            Assert.Null(run.Text());
            Assert.Equal(string.Empty, run.Sentence());
            Assert.False(run.FellBack);
            Assert.Equal(ParallelEyesState.NotAsked, Run().State);
            Assert.Equal(ParallelEyesState.NotAsked, ParallelEyesRun.FromLines(null).State);
        }

        [Fact]
        public void OnSaysSo()
        {
            var run = Run(On, Other);
            Assert.Equal(ParallelEyesState.On, run.State);
            Assert.Equal("on", run.Text());
            Assert.Equal("Parallel Eye Rendering was on.", run.Sentence());
            Assert.False(run.FellBack);
        }

        [Fact]
        public void OffGivesTheLayersReasonWithoutItsVariable()
        {
            Assert.Equal("off: not with DLSS", Run(OffDlss).Text());
            Assert.Equal("Parallel Eye Rendering did not run (not with DLSS): the standard renderer ran.", Run(OffDlss).Sentence());
            Assert.Equal("off: a check failed", Run(OffCheck).Text());
            Assert.Equal("off: the multiplayer guard is not armed", Run(OffGuard).Text());
            Assert.Equal("off: stereo only", Run("[    0.4] [1] parallel eyes: off: stereo only (ETERNALVR_MODE is not stereo); the standard renderer").Text());
            Assert.Equal("off: the UI layer is off",
                Run("[    0.4] [1] parallel eyes: off: the UI layer is off (ETERNALVR_UI_LAYER=0); its image hooks give each eye its view's picture; the standard renderer").Text());
            Assert.True(Run(OffDlss).FellBack);
        }

        [Fact]
        public void AnotherGameVersionAndAFailure()
        {
            var other = Run(NotAvailable);
            Assert.Equal(ParallelEyesState.NotAvailable, other.State);
            Assert.Equal("not available for this game version", other.Text());
            Assert.Equal("Parallel Eye Rendering is not available for this game version: the standard renderer ran.", other.Sentence());
            Assert.True(other.FellBack);
            var failed = Run(Failed);
            Assert.Equal(ParallelEyesState.Failed, failed.State);
            Assert.Equal("FAILED: both eyes showed the same image", failed.Text());
            Assert.Equal("Parallel Eye Rendering failed to start: both eyes showed the same image.", failed.Sentence());
            Assert.True(failed.FellBack);
        }

        [Fact]
        public void OnThenOneImageSaysWhy()
        {
            var clones = Run(On, ClonesOff);
            Assert.Equal(ParallelEyesState.On, clones.State);
            Assert.Equal("on, then one image in both eyes (clones off)", clones.Text());
            Assert.Equal("Parallel Eye Rendering was on, then showed one image in both eyes (clones off).", clones.Sentence());
            Assert.True(clones.FellBack);
            // The first cause counts.
            Assert.Equal("on, then one image in both eyes (multiplayer guard tripped)", Run(On, Trip, ClonesOff).Text());
            Assert.Equal("on, then one image in both eyes (clones off)",
                Run(On, "[  9.0] [1] view-clones: 4 remakes; clones off, view 0 alone from now on").Text());
        }

        [Fact]
        public void TheAsyncComputeSafetyNetIsOneImage()
        {
            // The device still got an async compute queue: the layer never renders view 1, both eyes show view 0's image.
            var run = Run(On, Async);
            Assert.Equal("on, then one image in both eyes (async compute)", run.Text());
            Assert.Equal("Parallel Eye Rendering was on, then showed one image in both eyes (async compute).", run.Sentence());
            Assert.True(run.FellBack);
            // Its test form keeps the queue and renders view 1 anyway.
            var kept = Run(On, AsyncKept);
            Assert.Equal("on", kept.Text());
            Assert.False(kept.FellBack);
            // In the summary too.
            Assert.StartsWith("Parallel Eye Rendering was on, then showed one image in both eyes (async compute).",
                SessionSummary.FromLines(Session(On, Async)).Describe());
        }

        [Fact]
        public void TheSummaryStartsWithIt()
        {
            var s = SessionSummary.FromLines(Session(On));
            Assert.Equal(ParallelEyesState.On, s.ParallelEyes.State);
            Assert.StartsWith("Parallel Eye Rendering was on. The game kept up with your headset", s.Describe());
            Assert.Contains("; parallel eyes on; ", s.LogText());
            var off = SessionSummary.FromLines(Session(NotAvailable));
            Assert.StartsWith("Parallel Eye Rendering is not available for this game version: the standard renderer ran. ", off.Describe());
            // Without its lines the summary is as before.
            var none = SessionSummary.FromLines(Session());
            Assert.Equal("The game kept up with your headset: about 90 new frames a second at 90 Hz.", none.Describe());
            Assert.Contains("; parallel eyes not asked; ", none.LogText());
        }

        [Fact]
        public void TheLastSessionAndTheReportKeepIt()
        {
            var ended = new DateTime(2026, 10, 9, 20, 15, 0);
            var facts = LastHeadset.AfterSession(new HeadsetFacts(), SessionSummary.FromLines(Session(On, ClonesOff)), "20261009-200000", ended);
            Assert.Equal("on, then one image in both eyes (clones off)", facts.SessionParallelEyes);
            var text = LastHeadset.Serialize(facts);
            Assert.Contains("session_parallel_eyes = on, then one image in both eyes (clones off)\n", text);
            var back = LastHeadset.Parse(text);
            Assert.Equal(facts.SessionParallelEyes, back.SessionParallelEyes);
            var lines = HeadsetView.ReportLines(back, null).ToDictionary(kv => kv.Key, kv => kv.Value);
            Assert.Equal("on, then one image in both eyes (clones off)", lines["last session parallel eyes"]);
            Assert.StartsWith("Parallel Eye Rendering was on, then showed one image in both eyes (clones off).", lines["last session summary"]);
            // A later session that did not ask for it clears it, and the report leaves the line out.
            var later = LastHeadset.AfterSession(back, SessionSummary.FromLines(Session()), "20261009-210000", ended.AddHours(1));
            Assert.Null(later.SessionParallelEyes);
            Assert.DoesNotContain("session_parallel_eyes", LastHeadset.Serialize(later));
            Assert.DoesNotContain(HeadsetView.ReportLines(later, null), kv => kv.Key == "last session parallel eyes");
        }

        [Fact]
        public void ItsWordsArePlain()
        {
            foreach (var run in new[] { Run(On), Run(OffDlss), Run(NotAvailable), Run(Failed), Run(On, Trip) })
            {
                Assert.DoesNotContain("—", run.Sentence());
                Assert.DoesNotContain("–", run.Sentence());
                Assert.DoesNotContain("ETERNALVR_", run.Sentence());
                Assert.EndsWith(".", run.Sentence());
            }
        }
    }
}
