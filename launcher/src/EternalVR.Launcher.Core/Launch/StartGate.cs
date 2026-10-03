using System;
using System.Threading;

namespace EternalVR.Launcher.Core.Launch
{
    /// <summary>How a launch through <see cref="StartGate.Launch"/> ended.</summary>
    public enum LaunchOutcome
    {
        /// <summary>The game was started (its start step ran).</summary>
        Started,
        /// <summary>Stopped before anything was prepared: no marker, no snapshot.</summary>
        StoppedBeforePreparing,
        /// <summary>Stopped after the preparation (marker, snapshot, backup), before the game's start: the caller undoes it.</summary>
        StoppedAfterPreparing,
    }

    /// <summary>
    /// Keeps the game's start and the steps that must follow it (the marker, the session finisher) together against the
    /// window closing: once <see cref="Close"/> has returned, no game is started, and a start already under way has
    /// finished all its steps. The window closes it before the launcher's process ends, so the process never ends between
    /// the game's start and the finisher's.
    /// </summary>
    public sealed class StartGate
    {
        private readonly object sync = new object();
        private bool closed;

        /// <summary>True once <see cref="Close"/> was called.</summary>
        public bool IsClosed
        {
            get { lock (sync) return closed; }
        }

        /// <summary>Runs <paramref name="start"/> unless the gate is closed; false when it was (nothing ran).</summary>
        public bool Run(Action start) => Run(CancellationToken.None, start);

        /// <summary>Runs <paramref name="start"/> unless the gate is closed or <paramref name="cancel"/> is set; false when nothing ran.</summary>
        public bool Run(CancellationToken cancel, Action start)
        {
            lock (sync)
            {
                if (closed || cancel.IsCancellationRequested) return false;
                start();
                return true;
            }
        }

        /// <summary>
        /// A session's launch: <paramref name="prepare"/> (the marker, the snapshot, the save backup) and then
        /// <paramref name="start"/> (the game, its marker, the finisher), each only while neither the gate is closed nor
        /// <paramref name="cancel"/> set. A stop after the preparation is the caller's to undo. Exceptions pass through.
        /// </summary>
        public LaunchOutcome Launch(CancellationToken cancel, Action prepare, Action start)
        {
            if (cancel.IsCancellationRequested || IsClosed) return LaunchOutcome.StoppedBeforePreparing;
            prepare();
            return Run(cancel, start) ? LaunchOutcome.Started : LaunchOutcome.StoppedAfterPreparing;
        }

        /// <summary>Closes the gate, waiting for a start under way to finish.</summary>
        public void Close()
        {
            lock (sync) closed = true;
        }
    }
}
