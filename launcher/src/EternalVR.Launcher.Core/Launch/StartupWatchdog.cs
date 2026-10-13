using System;

namespace EternalVR.Launcher.Core.Launch
{
    /// <summary>The steps from the game's start to its first frame in the headset, as the layer's status file shows them.</summary>
    public enum StartStep
    {
        /// <summary>The game process runs; the layer has not loaded yet (<see cref="LayerWatch.LoadTimeoutSeconds"/>).</summary>
        Launched,
        /// <summary>The layer loaded; the game has not presented a frame yet (it is loading).</summary>
        Loading,
        /// <summary>The game presents; the layer has no OpenXR session yet (the runtime is starting, or does not answer).</summary>
        Presenting,
        /// <summary>The OpenXR session exists (<c>runtime=</c>); the runtime has not started it yet.</summary>
        SessionCreated,
        /// <summary>The runtime started the session (<c>state=vr</c>); no game image has reached the headset yet.</summary>
        SessionRunning,
        /// <summary>A game image reached the headset (<c>first_headset_frame=</c>): the start is over.</summary>
        InHeadset,
    }

    /// <summary>
    /// Tells a slow start from one that stalls (M8): fed the layer's status at every poll, it keeps the step the start has
    /// reached and since when, and says what is missing once a step lasts past its limit, with one thing to try. A slow
    /// load is told as such (Info); a stall is a Warning, replaced as soon as the start moves on. Never a dialog: the
    /// game may still get there. The layer's own outcomes (flat, waiting for the headset, the game stopped presenting)
    /// are <see cref="LayerWatch"/>'s.
    /// </summary>
    public sealed class StartupWatchdog
    {
        /// <summary>
        /// No picture from the game this long after its start: the status line says it is still loading. The game's first
        /// present came 5 to 59 s after the layer loaded in 680 rig and player logs (median 10 s); the game compiles its
        /// pipelines in the background once its renderer is up (its console: "Starting background pipeline compilation").
        /// </summary>
        public const double SlowLoadSeconds = 60;

        /// <summary>No picture from the game this long after its start: it is probably stuck.</summary>
        public const double StuckLoadSeconds = 300;

        /// <summary>
        /// The game presents but the layer has no OpenXR session this long after: the runtime does not answer. 7 s at most
        /// in the same logs (the layer's hooks, then the runtime's instance); SteamVR started by the game takes longer.
        /// </summary>
        public const double RuntimeSeconds = 30;

        /// <summary>The session exists but the runtime has not started it this long after (1.5 s at most in the same logs).</summary>
        public const double SessionSeconds = 15;

        /// <summary>The session runs but no game image reached the headset this long after (1 s at most in the same logs).</summary>
        public const double FirstFrameSeconds = 15;

        public const string SlowLoadMessage =
            "Still loading (no picture after 1 minute). A first start or one after a driver update can take a few minutes.";

        public const string StuckLoadMessage =
            "No picture from the game in 5 minutes. If its window is black or frozen, close the game (Task Manager if needed) and "
            + "launch it again. " + Report.ReportHint.Ask;

        public const string NoRuntimeMessage =
            "The game runs, but the headset runtime has not answered in 30 s. Check the headset and its software (SteamVR can "
            + "take a minute).";

        public const string NoFrameMessage =
            "VR started, but no game picture reached the headset in 15 s. If the game window is frozen, close it and launch "
            + "it again. " + Report.ReportHint.Ask;

        /// <summary>The runtime (named when the status file names it) has the session but has not started it.</summary>
        public static string SessionIdleMessage(string runtime) =>
            "The headset runtime" + (string.IsNullOrWhiteSpace(runtime) ? string.Empty : " (" + runtime.Trim() + ")")
            + " has not started VR in 15 s. Put the headset on and leave its home screen.";

        private bool presented;
        private bool created;
        private bool shown;
        /// <summary>
        /// The layer writes the start's steps (<c>first_present=</c>); an older layer does not, so it is not judged by the
        /// headset's first frame. It is still judged by the load limits until its session exists or it waits for the headset
        /// (<c>runtime=</c>, <c>state=waiting</c>): a runtime that takes a minute to answer then reads as a slow load.
        /// </summary>
        private bool writesSteps;
        /// <summary>When <see cref="writesSteps"/> was first seen: the headset's first frame cannot come before it.</summary>
        private double stepsSince;

        /// <summary>The step the start has reached at the last <see cref="Observe"/>.</summary>
        public StartStep Step { get; private set; } = StartStep.Launched;

        /// <summary>When <see cref="Step"/> began, in seconds since the game started.</summary>
        public double StepSince { get; private set; }

        /// <summary>Called with the new step and its time whenever the step changes (the launcher logs it).</summary>
        public event Action<StartStep, double> StepChanged;

        /// <summary>
        /// One poll. <paramref name="loaded"/>: the layer's loaded marker exists; <paramref name="status"/>: the parsed status
        /// file or null; <paramref name="secondsSinceStart"/>: since the game process started. Returns the stall or slow-start
        /// status to show, or null when the watchdog has nothing to say (the layer's own status then stands).
        /// </summary>
        public SessionStatus Observe(bool loaded, LayerStatusFile status, double secondsSinceStart)
        {
            var state = status?.State ?? LayerState.Unknown;
            // What the file says stays true for the session: a runtime is named once its session exists, VR and waiting for
            // the headset come only after the game presented.
            presented |= status != null && (status.FirstPresent || status.Runtime.Length > 0 || state == LayerState.Vr || state == LayerState.Waiting);
            created |= status != null && (status.Runtime.Length > 0 || state == LayerState.Vr);
            shown |= status != null && status.FirstHeadsetFrame;
            if (!writesSteps && status != null && status.FirstPresent)
            {
                writesSteps = true;
                stepsSince = secondsSinceStart;
            }
            var step = shown ? StartStep.InHeadset
                : state == LayerState.Vr ? StartStep.SessionRunning
                : created ? StartStep.SessionCreated
                : presented ? StartStep.Presenting
                : loaded || status != null ? StartStep.Loading
                : StartStep.Launched;
            if (step != Step)
            {
                Step = step;
                StepSince = secondsSinceStart;
                StepChanged?.Invoke(step, secondsSinceStart);
            }
            // The layer's own outcomes come first: VR off, waiting for the headset, a game that stopped presenting.
            if (status != null && (state == LayerState.Flat || state == LayerState.Waiting || status.PresentsStopped)) return null;
            double inStep = secondsSinceStart - StepSince;
            switch (step)
            {
                case StartStep.Loading:
                    if (secondsSinceStart >= StuckLoadSeconds) return new SessionStatus(StatusKind.Warning, StuckLoadMessage);
                    if (secondsSinceStart >= SlowLoadSeconds) return new SessionStatus(StatusKind.Info, SlowLoadMessage);
                    return null;
                case StartStep.Presenting:
                    return inStep >= RuntimeSeconds ? new SessionStatus(StatusKind.Warning, NoRuntimeMessage) : null;
                case StartStep.SessionCreated:
                    return inStep >= SessionSeconds ? new SessionStatus(StatusKind.Warning, SessionIdleMessage(status?.Runtime)) : null;
                case StartStep.SessionRunning:
                    // A layer that writes first_present also writes first_headset_frame; an older one is not judged by it. VR
                    // can start before the game's first present: the limit counts from the later of the two.
                    double running = secondsSinceStart - Math.Max(StepSince, stepsSince);
                    return writesSteps && running >= FirstFrameSeconds ? new SessionStatus(StatusKind.Warning, NoFrameMessage) : null;
                default:
                    return null;
            }
        }

        /// <summary>The step in a few words, for the launcher's log.</summary>
        public static string Describe(StartStep step)
        {
            switch (step)
            {
                case StartStep.Loading: return "the mod loaded; the game is loading";
                case StartStep.Presenting: return "the game shows its first frame; VR connects to the headset runtime";
                case StartStep.SessionCreated: return "the OpenXR session exists; the runtime has not started it yet";
                case StartStep.SessionRunning: return "the runtime started VR; no game picture in the headset yet";
                case StartStep.InHeadset: return "the game's first picture reached the headset";
                default: return "the game started";
            }
        }
    }

    /// <summary>
    /// What the window shows about the layer while the game runs, poll by poll (SessionRunner): the start's watchdog, then the
    /// layer's own status (<see cref="LayerWatch"/>), through the hold on problems (<see cref="ProblemHold"/>).
    /// </summary>
    public sealed class LayerMonitor
    {
        private readonly ProblemHold hold = new ProblemHold();
        private LayerStatusFile last;

        public StartupWatchdog Watchdog { get; } = new StartupWatchdog();

        /// <summary>A problem still waiting out its hold; null when none.</summary>
        public SessionStatus Held => hold.Pending;

        /// <param name="loaded">The layer's loaded marker exists.</param>
        /// <param name="status">The parsed status file; null when there is none or it could not be read this time (the last one
        /// read then stands: the layer replaces it whole, and never removes it).</param>
        /// <param name="secondsSinceStart">Since the game process started.</param>
        /// <returns>The status to show now, or null while there is nothing to say (or a problem is held).</returns>
        public SessionStatus Poll(bool loaded, LayerStatusFile status, double secondsSinceStart)
        {
            if (status != null) last = status;
            var stall = Watchdog.Observe(loaded, last, secondsSinceStart);
            return hold.Offer(stall ?? LayerWatch.Decide(loaded, last, secondsSinceStart), secondsSinceStart);
        }
    }
}
