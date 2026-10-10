using System;
using System.Collections.Generic;
using System.Text.RegularExpressions;

namespace EternalVR.Launcher.Core
{
    /// <summary>What became of Parallel Eye Rendering in a session.</summary>
    public enum ParallelEyesState
    {
        /// <summary>The launch did not ask for it: the layer logs nothing.</summary>
        NotAsked,
        /// <summary><c>parallel eyes: on: ...</c>: both eyes as two views of one render.</summary>
        On,
        /// <summary><c>parallel eyes: off: &lt;why&gt;; the standard renderer</c>.</summary>
        Off,
        /// <summary><c>parallel eyes: not available for this game version</c> (Game Pass, another Steam build).</summary>
        NotAvailable,
        /// <summary><c>parallel eyes: FAILED after the engine was changed</c>: both eyes show view 0's image.</summary>
        Failed,
    }

    /// <summary>
    /// Whether Parallel Eye Rendering ran in a session, or fell back and why, from the layer's start-up line
    /// (<c>src/vkcore/view_install.cpp</c>, docs/VR_STEREO.md "Logs"): <c>on: ...</c>, <c>off: &lt;why&gt;; ...</c>,
    /// <c>not available for this game version</c> or <c>FAILED ...</c>. A session that was on and later rendered view 0
    /// alone (one image in both eyes) says why: the multiplayer guard tripped, view 1's clones turned off, or the game's
    /// device still got an async compute queue (the safety net of src/vkcore/view_async.cpp: view 1 is never rendered).
    /// </summary>
    public sealed class ParallelEyesRun
    {
        private static readonly Regex OnLine = new Regex(@"\] parallel eyes: on: ", RegexOptions.CultureInvariant);
        private static readonly Regex OffLine = new Regex(@"\] parallel eyes: off: (?<why>[^;]*)", RegexOptions.CultureInvariant);
        private static readonly Regex NotAvailableLine = new Regex(@"\] parallel eyes: not available for this game version", RegexOptions.CultureInvariant);
        private static readonly Regex FailedLine = new Regex(@"\] parallel eyes: FAILED ", RegexOptions.CultureInvariant);
        private static readonly Regex TripLine = new Regex(@"\] parallel eyes: the multiplayer guard has tripped", RegexOptions.CultureInvariant);
        private static readonly Regex ClonesOffLine = new Regex(@"\] view-clones: .*clones off, view 0 alone", RegexOptions.CultureInvariant);
        /// <summary>Not its "kept for the test" form (ETERNALVR_TEST_PE_ASYNC=keep), which renders view 1 anyway.</summary>
        private static readonly Regex AsyncLine = new Regex(
            @"\] parallel eyes: the game's device has an async compute queue .*view 1 is not rendered", RegexOptions.CultureInvariant);
        /// <summary>The layer's own variable or setting named after a reason, as in <c>not with DLSS (ETERNALVR_STEREO_DLSS=1)</c>.</summary>
        private static readonly Regex Aside = new Regex(@"\s*\([^)]*\)\s*$", RegexOptions.CultureInvariant);

        public ParallelEyesState State { get; private set; }
        /// <summary>Why it was off, as the layer said it without its aside (<c>not with DLSS</c>); null unless <see cref="ParallelEyesState.Off"/>.</summary>
        public string Why { get; private set; }
        /// <summary>On, and later one image in both eyes: why (<c>multiplayer guard tripped</c>, <c>clones off</c>, <c>async
        /// compute</c>); null otherwise.</summary>
        public string Dropped { get; private set; }

        /// <summary>The run of a layer log's lines; <see cref="ParallelEyesState.NotAsked"/> when they hold none of its lines.</summary>
        public static ParallelEyesRun FromLines(IEnumerable<string> lines)
        {
            var run = new ParallelEyesRun();
            foreach (var line in lines ?? new string[0]) run.Read(line);
            return run;
        }

        /// <summary>Takes one layer log line (<see cref="SessionSummary"/> reads its lines once for both).</summary>
        internal void Read(string line)
        {
            if (line == null) return;
            if (line.IndexOf("] parallel eyes: ", StringComparison.Ordinal) >= 0)
            {
                if (OnLine.IsMatch(line)) State = ParallelEyesState.On;
                else if (NotAvailableLine.IsMatch(line)) State = ParallelEyesState.NotAvailable;
                else if (FailedLine.IsMatch(line)) State = ParallelEyesState.Failed;
                else if (TripLine.IsMatch(line)) { if (Dropped == null) Dropped = "multiplayer guard tripped"; }
                else if (AsyncLine.IsMatch(line)) { if (Dropped == null) Dropped = "async compute"; }
                else
                {
                    var off = OffLine.Match(line);
                    if (off.Success)
                    {
                        State = ParallelEyesState.Off;
                        Why = Aside.Replace(off.Groups["why"].Value.Trim(), string.Empty);
                    }
                }
                return;
            }
            if (Dropped == null && line.IndexOf("] view-clones: ", StringComparison.Ordinal) >= 0 && ClonesOffLine.IsMatch(line))
                Dropped = "clones off";
        }

        /// <summary>
        /// The report's and the log's words: "on", "on, then one image in both eyes (clones off)", "off: not with DLSS",
        /// "not available for this game version", "FAILED: both eyes showed the same image"; null when not asked.
        /// </summary>
        public string Text()
        {
            switch (State)
            {
                case ParallelEyesState.On: return Dropped == null ? "on" : "on, then one image in both eyes (" + Dropped + ")";
                case ParallelEyesState.Off: return "off: " + (string.IsNullOrEmpty(Why) ? "reason not logged" : Why);
                case ParallelEyesState.NotAvailable: return "not available for this game version";
                case ParallelEyesState.Failed: return "FAILED: both eyes showed the same image";
                default: return null;
            }
        }

        /// <summary>The status line's sentence after the session; empty when not asked.</summary>
        public string Sentence()
        {
            switch (State)
            {
                case ParallelEyesState.On:
                    return Dropped == null ? "Parallel Eye Rendering was on."
                        : "Parallel Eye Rendering was on, then showed one image in both eyes (" + Dropped + ").";
                case ParallelEyesState.Off:
                    return "Parallel Eye Rendering did not run (" + (string.IsNullOrEmpty(Why) ? "see the log" : Why) + "): the standard renderer ran.";
                case ParallelEyesState.NotAvailable:
                    return "Parallel Eye Rendering is not available for this game version: the standard renderer ran.";
                case ParallelEyesState.Failed:
                    return "Parallel Eye Rendering failed to start: both eyes showed the same image.";
                default: return string.Empty;
            }
        }

        /// <summary>It was asked for and did not run as asked (off, not available, failed, or one image later).</summary>
        public bool FellBack => State != ParallelEyesState.NotAsked && (State != ParallelEyesState.On || Dropped != null);
    }
}
