using System.Collections.Generic;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Report;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    public class StartupWatchdogTests
    {
        private static LayerStatusFile File(string text) => LayerStatusFile.Parse(text);

        // The status file at each step, as the layer writes it (status_file.hpp).
        private const string Loading = "state=starting";
        private const string Presenting = "state=starting\nfirst_present=10.1";
        private const string Created = "state=starting\nfirst_present=10.1\nruntime=VirtualDesktopXR\nsystem=Meta Quest 3";
        private const string Running = "state=vr\nfirst_present=10.1\nruntime=VirtualDesktopXR\nsystem=Meta Quest 3";
        private const string InHeadset = Running + "\nstereo=on\nfirst_headset_frame=14.3";

        [Fact]
        public void TheStatusFileNamesTheStartsSteps()
        {
            var s = File(InHeadset);
            Assert.True(s.FirstPresent);
            Assert.True(s.FirstHeadsetFrame);
            Assert.Equal("VirtualDesktopXR", s.Runtime);
            var none = File(Loading);
            Assert.False(none.FirstPresent);
            Assert.False(none.FirstHeadsetFrame);
            Assert.Equal(string.Empty, none.Runtime);
        }

        [Fact]
        public void AStartAsFastAsTheRigsSaysNothingAndWalksEveryStep()
        {
            // The rig's usual start (eternalvr-20261011-161851): first present at 10 s, VR and the first frame at 14 s.
            var w = new StartupWatchdog();
            var steps = new List<StartStep>();
            w.StepChanged += (step, at) => steps.Add(step);
            Assert.Null(w.Observe(false, null, 0.5));
            Assert.Equal(StartStep.Launched, w.Step);
            Assert.Null(w.Observe(true, File(Loading), 1));
            Assert.Null(w.Observe(true, File(Presenting), 10.1));
            Assert.Null(w.Observe(true, File(Created), 14.0));
            Assert.Null(w.Observe(true, File(Running), 14.3));
            Assert.Null(w.Observe(true, File(InHeadset), 14.4));
            Assert.Equal(new[] { StartStep.Loading, StartStep.Presenting, StartStep.SessionCreated, StartStep.SessionRunning, StartStep.InHeadset }, steps);
            // Once in the headset, nothing more from the watchdog however long the session lasts.
            Assert.Null(w.Observe(true, File(InHeadset), 3600));
        }

        [Fact]
        public void ALoadThatTakesLongIsSlowFirstThenStuck()
        {
            var w = new StartupWatchdog();
            Assert.Null(w.Observe(true, File(Loading), StartupWatchdog.SlowLoadSeconds - 1));
            var slow = w.Observe(true, File(Loading), StartupWatchdog.SlowLoadSeconds);
            Assert.Equal(new SessionStatus(StatusKind.Info, StartupWatchdog.SlowLoadMessage), slow);
            Assert.Contains("driver update", slow.Text);
            Assert.False(ReportHint.In(slow.Text));
            Assert.Equal(StatusKind.Info, w.Observe(true, File(Loading), StartupWatchdog.StuckLoadSeconds - 1).Kind);
            var stuck = w.Observe(true, File(Loading), StartupWatchdog.StuckLoadSeconds);
            Assert.Equal(new SessionStatus(StatusKind.Warning, StartupWatchdog.StuckLoadMessage), stuck);
            Assert.True(ReportHint.In(stuck.Text));
            Assert.False(LayerWatch.NeedsDialog(stuck));
            // The marker alone (no status file yet) is loading too.
            Assert.Equal(StatusKind.Warning, new StartupWatchdog().Observe(true, null, StartupWatchdog.StuckLoadSeconds).Kind);
        }

        [Fact]
        public void ASlowButFineFirstStartNeverWarns()
        {
            // A first start that loads for four minutes, then comes up as usual: Info only, and VR is running at the end.
            var m = new LayerMonitor();
            var shown = new List<SessionStatus>();
            void Poll(string text, double at)
            {
                var s = m.Poll(true, text == null ? null : File(text), at);
                if (s != null) shown.Add(s);
            }
            for (double t = 1; t < 240; t += 1) Poll(Loading, t);
            Poll(Presenting, 240);
            for (double t = 241; t < 248; t += 1) Poll(Presenting, t);
            Poll(Created, 248);
            Poll(Running, 248.5);
            Poll(InHeadset, 249);
            Assert.DoesNotContain(shown, s => s.Kind == StatusKind.Warning || s.Kind == StatusKind.Problem);
            Assert.Contains(shown, s => s.Text == StartupWatchdog.SlowLoadMessage);
            Assert.Equal(StatusKind.Good, shown[shown.Count - 1].Kind);
            Assert.Equal(StartStep.InHeadset, m.Watchdog.Step);
        }

        [Fact]
        public void ARuntimeThatDoesNotAnswerIsNamedAfterItsLimit()
        {
            var w = new StartupWatchdog();
            w.Observe(true, File(Loading), 1);
            Assert.Null(w.Observe(true, File(Presenting), 20));
            Assert.Null(w.Observe(true, File(Presenting), 20 + StartupWatchdog.RuntimeSeconds - 1));
            var s = w.Observe(true, File(Presenting), 20 + StartupWatchdog.RuntimeSeconds);
            Assert.Equal(new SessionStatus(StatusKind.Warning, StartupWatchdog.NoRuntimeMessage), s);
            Assert.False(LayerWatch.NeedsDialog(s));
            // The session comes: the warning goes.
            Assert.Null(w.Observe(true, File(Created), 60));
        }

        [Fact]
        public void ASessionTheRuntimeDoesNotStartNamesTheRuntimeAndTheHeadset()
        {
            var w = new StartupWatchdog();
            w.Observe(true, File(Presenting), 10);
            Assert.Null(w.Observe(true, File(Created), 14));
            Assert.Null(w.Observe(true, File(Created), 14 + StartupWatchdog.SessionSeconds - 1));
            var s = w.Observe(true, File(Created), 14 + StartupWatchdog.SessionSeconds);
            Assert.Equal(StatusKind.Warning, s.Kind);
            Assert.Equal(StartupWatchdog.SessionIdleMessage("VirtualDesktopXR"), s.Text);
            Assert.Contains("(VirtualDesktopXR)", s.Text);
            Assert.Contains("Put the headset on", s.Text);
            Assert.Null(w.Observe(true, File(Running), 40));
            Assert.StartsWith("The headset runtime has not started VR", StartupWatchdog.SessionIdleMessage(" "));
        }

        [Fact]
        public void ARunningSessionWithoutAHeadsetFrameIsAStall()
        {
            var w = new StartupWatchdog();
            w.Observe(true, File(Created), 14);
            Assert.Null(w.Observe(true, File(Running), 15));
            Assert.Null(w.Observe(true, File(Running), 15 + StartupWatchdog.FirstFrameSeconds - 1));
            var s = w.Observe(true, File(Running), 15 + StartupWatchdog.FirstFrameSeconds);
            Assert.Equal(new SessionStatus(StatusKind.Warning, StartupWatchdog.NoFrameMessage), s);
            Assert.True(ReportHint.In(s.Text));
            Assert.Null(w.Observe(true, File(InHeadset), 40));
        }

        [Fact]
        public void AnOlderLayerIsNotJudgedByTheHeadsetFrameButStillByTheLoad()
        {
            // No first_present: a running session is VR as before, never a missing headset frame.
            var w = new StartupWatchdog();
            Assert.Null(w.Observe(true, File("state=vr\nruntime=SteamVR/OpenXR"), 10));
            Assert.Null(w.Observe(true, File("state=vr\nruntime=SteamVR/OpenXR"), 600));
            Assert.Equal(StartStep.SessionRunning, w.Step);
            // Its session idle in "starting" is still told by runtime=.
            var idle = new StartupWatchdog();
            idle.Observe(true, File("state=starting\nruntime=SteamVR/OpenXR"), 10);
            Assert.Equal(StatusKind.Warning, idle.Observe(true, File("state=starting\nruntime=SteamVR/OpenXR"), 10 + StartupWatchdog.SessionSeconds).Kind);
            // Until its session exists it is loading: a runtime slow to answer reads as a slow load (it has no first_present).
            var load = new StartupWatchdog();
            Assert.Equal(StatusKind.Info, load.Observe(true, File("state=starting"), StartupWatchdog.SlowLoadSeconds).Kind);
            Assert.Equal(StartStep.Loading, load.Step);
        }

        [Fact]
        public void VrBeforeTheFirstPresentCountsTheHeadsetFrameFromThePresent()
        {
            // The runtime can start VR before the game presents (the presenter is up from the swapchain on); no game image can
            // reach the headset before the game's first present, so the limit counts from it.
            var w = new StartupWatchdog();
            w.Observe(true, File(Loading), 1);
            Assert.Null(w.Observe(true, File("state=vr\nruntime=VirtualDesktopXR"), 10));
            Assert.Equal(StartStep.SessionRunning, w.Step);
            Assert.Null(w.Observe(true, File("state=vr\nruntime=VirtualDesktopXR"), 39));
            Assert.Null(w.Observe(true, File(Running), 40));
            Assert.Null(w.Observe(true, File(Running), 40 + StartupWatchdog.FirstFrameSeconds - 1));
            Assert.Equal(new SessionStatus(StatusKind.Warning, StartupWatchdog.NoFrameMessage), w.Observe(true, File(Running), 40 + StartupWatchdog.FirstFrameSeconds));
        }

        [Fact]
        public void WaitingWithASessionLeavesTheLayersReason()
        {
            // The layer waits with runtime= set (a session the runtime would not start, tried again): the step is the session's,
            // but the watchdog says nothing and the layer's own reason stands, however long it lasts.
            const string waiting = "state=waiting\nreason=the headset runtime did not start VR for the game; trying again with a new session"
                + "\nfirst_present=10.1\nruntime=VirtualDesktopXR";
            var m = new LayerMonitor();
            m.Poll(true, File(Presenting), 10);
            SessionStatus last = null;
            for (double t = 14; t <= 120; t += 1) last = m.Poll(true, File(waiting), t) ?? last;
            Assert.Equal(StartStep.SessionCreated, m.Watchdog.Step);
            Assert.Null(m.Watchdog.Observe(true, File(waiting), 121));
            Assert.Equal(StatusKind.Warning, last.Kind);
            Assert.StartsWith("Waiting for the headset: the headset runtime did not start VR", last.Text);
            // Once the layer has the session it says so with "starting" (presenter_frame.cpp): the idle warning comes.
            const string idle = "state=starting\nreason=the headset runtime has the game's VR session; waiting for it to start VR"
                + "\nfirst_present=10.1\nruntime=VirtualDesktopXR";
            var w = new StartupWatchdog();
            w.Observe(true, File(Presenting), 10);
            w.Observe(true, File(idle), 14);
            Assert.Equal(StartupWatchdog.SessionIdleMessage("VirtualDesktopXR"), w.Observe(true, File(idle), 14 + StartupWatchdog.SessionSeconds).Text);
        }

        [Fact]
        public void AGameDeviceWithoutThePresenterIsFlatNotStuck()
        {
            // The layer could not set up the game's device for the headset (layer_entry.cpp): no presenter, so no first_present
            // ever. It says flat, and the game is not told it is stuck however long it runs.
            const string off = "state=flat\nreason=the game's graphics device could not be set up for the headset (details in the log)";
            var m = new LayerMonitor();
            SessionStatus last = null;
            m.Poll(true, File(Loading), 1);
            for (double t = 2; t <= StartupWatchdog.StuckLoadSeconds + 60; t += 1) last = m.Poll(true, File(off), t) ?? last;
            Assert.Equal(StatusKind.Problem, last.Kind);
            Assert.StartsWith("VR is off for this session: the game's graphics device", last.Text);
            Assert.Equal(StartStep.Loading, m.Watchdog.Step);
            Assert.Null(m.Watchdog.Observe(true, File(off), StartupWatchdog.StuckLoadSeconds + 61));
        }

        [Fact]
        public void ASessionLostBeforeItsFirstFrameStepsBackAndLogsTheStepsAgain()
        {
            // VR, then the headset lost (waiting, runtime= still set): the step goes back to the session's and is logged again,
            // then forward again when the runtime starts the new session.
            var w = new StartupWatchdog();
            var steps = new List<(StartStep Step, double At)>();
            w.StepChanged += (step, at) => steps.Add((step, at));
            w.Observe(true, File(Created), 14);
            w.Observe(true, File(Running), 15);
            const string lost = "state=waiting\nreason=the headset disconnected\nfirst_present=10.1\nruntime=VirtualDesktopXR";
            Assert.Null(w.Observe(true, File(lost), 20));
            Assert.Null(w.Observe(true, File(lost), 200));
            Assert.Null(w.Observe(true, File(Running), 210));
            Assert.Equal(new[] { (StartStep.SessionCreated, 14.0), (StartStep.SessionRunning, 15.0), (StartStep.SessionCreated, 20.0), (StartStep.SessionRunning, 210.0) }, steps);
        }

        [Theory]
        [InlineData("state=flat\nreason=not supported")]
        [InlineData("state=waiting\nreason=no headset found yet")]
        [InlineData("state=vr\nfirst_present=1\npresents=stopped_at_start")]
        public void TheLayersOwnOutcomesComeFirst(string text)
        {
            var w = new StartupWatchdog();
            w.Observe(true, File(text), 1);
            Assert.Null(w.Observe(true, File(text), 1000));
        }

        [Fact]
        public void ASessionThatComesBackStartsItsLimitAgain()
        {
            // VR, then the headset lost before any frame (waiting), then VR again: the limit counts from the new start.
            var w = new StartupWatchdog();
            w.Observe(true, File(Running), 15);
            w.Observe(true, File("state=waiting\nfirst_present=10.1\nruntime=VirtualDesktopXR"), 20);
            Assert.Null(w.Observe(true, File(Running), 100));
            Assert.Equal(100, w.StepSince);
            Assert.Null(w.Observe(true, File(Running), 100 + StartupWatchdog.FirstFrameSeconds - 1));
            Assert.Equal(StatusKind.Warning, w.Observe(true, File(Running), 100 + StartupWatchdog.FirstFrameSeconds).Kind);
        }

        [Fact]
        public void TheMonitorKeepsTheLastFileThroughAnUnreadablePoll()
        {
            var m = new LayerMonitor();
            m.Poll(true, File(Running), 15);
            Assert.Equal(StatusKind.Good, m.Poll(true, File(InHeadset), 16).Kind);
            // Mid-replace: the read failed this time; the start does not go back a step.
            Assert.Equal(StatusKind.Good, m.Poll(true, null, 17).Kind);
            Assert.Equal(StartStep.InHeadset, m.Watchdog.Step);
        }

        [Fact]
        public void AGameThatStopsPresentingAtTheStartStillAsksForARestart()
        {
            // The frames-stopped step is the layer's (presents=stopped_at_start), through the monitor's hold on problems.
            var m = new LayerMonitor();
            m.Poll(true, File(InHeadset), 15);
            var stopped = InHeadset + "\npresents=stopped_at_start";
            Assert.Null(m.Poll(true, File(stopped), 30));
            Assert.Equal(new SessionStatus(StatusKind.Problem, LayerWatch.StoppedMessage), m.Poll(true, File(stopped), 30 + ProblemHold.HoldSeconds));
        }

        [Fact]
        public void PositiveControlTheSessionsMonitorRunsTheWatchdog()
        {
            // The monitor is what the session runner polls. A session the runtime never starts must reach the window as the
            // watchdog's warning: without the watchdog it would stay "VR is starting" for good.
            var m = new LayerMonitor();
            SessionStatus last = null;
            for (double t = 1; t <= 60; t += 1)
                last = m.Poll(true, File(t < 10 ? Loading : t < 14 ? Presenting : Created), t) ?? last;
            Assert.Equal(StatusKind.Warning, last.Kind);
            Assert.Equal(StartupWatchdog.SessionIdleMessage("VirtualDesktopXR"), last.Text);
            Assert.NotEqual(LayerWatch.StartingMessage, last.Text);
        }
    }
}
