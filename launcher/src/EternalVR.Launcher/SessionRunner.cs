using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Threading;
using EternalVR.Launcher.Core;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Report;
using EternalVR.Launcher.Core.Safety;
using EternalVR.Launcher.Platform;

namespace EternalVR.Launcher
{
    /// <summary>
    /// One VR session end to end: preflight, marker, settings snapshot, save backup, start, hand-off
    /// watch, wait for exit, restore, marker removed. Also the crash recovery run at every start.
    /// </summary>
    public sealed class SessionRunner
    {
        private const int SnapshotsKept = 10;
        private readonly LauncherContext ctx;
        private readonly HkcuLayerRegistration registration;

        public SessionRunner(LauncherContext ctx)
        {
            this.ctx = ctx;
            registration = new HkcuLayerRegistration(ctx.Paths.RegistrationMarker, ctx.Log.Info);
        }

        private Log Log => ctx.Log;
        private SessionStatus lastStatus;
        private ProblemHold problemHold = new ProblemHold();
        /// <summary>The game's start with its marker and <see cref="GameStarted"/>; closed by <see cref="StopLaunching"/>.</summary>
        private readonly StartGate gate = new StartGate();
        private volatile bool gameLaunched;

        /// <summary>Why the last restore attempt failed; null when it succeeded or only waits for the game to exit.</summary>
        public string LastRestoreError { get; private set; }

        /// <summary>Called on the session's thread once the game process has started (the window starts the finisher here).</summary>
        public Action GameStarted { get; set; }

        /// <summary>True from the game's start to the end of the session; false while a launch is still being prepared.</summary>
        public bool GameLaunched => gameLaunched;

        /// <summary>
        /// The window is closing: no game is started any more. Returns once a start under way has also started the finisher,
        /// so the launcher's process can end without leaving a game that nothing restores after.
        /// </summary>
        public void StopLaunching() => gate.Close();

        /// <summary>
        /// Asked on the session's thread when the runtime probe found no headset (the problem's text): true launches
        /// anyway. Null (headless runs) launches with a warning.
        /// </summary>
        public Func<string, bool> ConfirmWithoutHeadset { get; set; }

        /// <summary>Session outcomes for the window (refusals, the game's start, the layer's state, the end); any thread.</summary>
        public event Action<SessionStatus> Status;

        private void Report(StatusKind kind, string text) => Report(new SessionStatus(kind, text));

        private void Report(SessionStatus s)
        {
            if (s == null || s.Equals(lastStatus)) return;
            lastStatus = s;
            if (s.Kind == StatusKind.Problem) Log.Error("status: " + s.Text);
            else if (s.Kind == StatusKind.Warning) Log.Warn("status: " + s.Text);
            else Log.Info("status: " + s.Text);
            Status?.Invoke(s);
        }

        /// <summary>
        /// Start-up clean-up (T-093): removes a stale layer registration, then completes or defers a
        /// pending settings restore. Returns true when nothing is left pending.
        /// </summary>
        public bool Recover()
        {
            LastRestoreError = null;
            try { registration.RemoveStale(); }
            catch (Exception e) { Log.Error("removing a stale layer registration failed: " + e.Message); }

            var marker = SessionMarker.Read(ctx.Paths.SessionMarker);
            var running = ctx.RunningGameProcesses();
            switch (SessionRecovery.Decide(marker, running.Count > 0))
            {
                case RecoveryAction.None:
                    return true;
                case RecoveryAction.Discard:
                    Log.Info($"session {marker.SessionId} never launched; discarding its marker");
                    if (!string.IsNullOrEmpty(marker.SnapshotDir) && Directory.Exists(marker.SnapshotDir) && !SettingsSnapshot.IsComplete(marker.SnapshotDir))
                        FileUtil.DeleteDirectory(marker.SnapshotDir);
                    SessionMarker.DeleteIfOwned(ctx.Paths.SessionMarker, marker.SessionId);
                    return true;
                case RecoveryAction.WaitForGame:
                    Log.Warn($"session {marker.SessionId} is still pending and {string.Join(", ", running)} is running; the settings are restored once it exits");
                    return false;
                default:
                    Log.Info($"completing the settings restore of session {marker.SessionId}");
                    return RestoreAndClear(marker);
            }
        }

        public bool RestoreAndClear(SessionMarker marker)
        {
            try
            {
                var snapshot = marker.ResolveSnapshotDir(ctx.Paths.Snapshots);
                if (snapshot == null)
                    throw new IOException("the session marker names no complete snapshot: " + marker.SnapshotDir);
                if (!string.Equals(snapshot, marker.SnapshotDir, StringComparison.OrdinalIgnoreCase))
                    Log.Warn("the session marker is damaged; restoring from " + snapshot);
                var report = SettingsSnapshot.Restore(snapshot, ctx.Data.RestoredKeys);
                foreach (var line in report.Lines) Log.Info("restore: " + line);
                // Steam Cloud files (profile.bin changes every session) are the game's by design: their lines above are enough.
                if (report.ChangedNotRestored.Count > 0)
                    Log.Warn($"local settings files changed during the session and are not restored key by key: {string.Join(", ", report.ChangedNotRestored)}; "
                        + $"their old copies are in {snapshot}, kept with the last {SnapshotsKept} sessions' snapshots");
                if (!SessionMarker.DeleteIfOwned(ctx.Paths.SessionMarker, marker.SessionId))
                    Log.Warn("the session marker now belongs to another session; it is left in place");
                Log.Info("settings restore complete");
                try { SettingsSnapshot.Prune(ctx.Paths.Snapshots, SnapshotsKept, snapshot); }
                catch (Exception e) when (e is IOException || e is UnauthorizedAccessException) { Log.Warn("pruning old snapshots failed: " + e.Message); }
                return true;
            }
            catch (Exception e)
            {
                Log.Error("settings restore failed, it will be retried: " + e.Message);
                LastRestoreError = e.Message;
                return false;
            }
        }

        /// <summary>Preflight and plan only; with <paramref name="takeCopies"/> also the snapshot and save backup.</summary>
        public LaunchPlan DryRun(bool takeCopies, out LauncherContext.Gathered gathered)
        {
            gathered = ctx.GatherPreflight();
            foreach (var c in gathered.Result.Checks) Log.Info("preflight " + c);
            if (gathered.Game == null) return null;
            var id = ctx.Paths.UniqueSessionId(DateTime.Now);
            var plan = ctx.BuildPlan(gathered, id);
            // Copies are taken only when a launch could: not while the game runs (a torn save backup
            // would count in the rotation) and not while an earlier session's restore is pending.
            bool quiet = gathered.Facts.GameProcessesRunning.Count == 0 && !gathered.Facts.SessionPending;
            if (takeCopies && !quiet) Log.Info("dry run: no snapshot or save backup while the game runs or a restore is pending");
            if (takeCopies && quiet && gathered.Locations.Count > 0)
            {
                var snap = SettingsSnapshot.Take(ctx.Paths.Snapshots, id, gathered.Locations, SessionKeys.FromArguments(ctx.Settings.ExtraArguments));
                File.WriteAllText(Path.Combine(snap, SettingsSnapshot.DryRunFile), "taken by --dry-run; no game was started");
                Log.Info("dry run: settings snapshot in " + snap);
                BackUpSaves(id, gathered);
            }
            return plan;
        }

        /// <summary>
        /// Runs a whole session and blocks until the game has exited and the restore is done. Returns
        /// false when preflight refused, the start failed or the restore did not complete.
        /// </summary>
        public bool Run(CancellationToken cancel)
        {
            lastStatus = null;
            problemHold = new ProblemHold();
            gameLaunched = false;
            if (!Recover())
            {
                Log.Error("a previous session is still pending; not launching");
                Report(StatusKind.Problem, "Not launched: the previous session's settings restore has not completed yet (it runs once the game has exited).");
                return false;
            }
            var g = ctx.GatherPreflight();
            foreach (var c in g.Result.Checks) Log.Info("preflight " + c);
            if (!g.Result.CanLaunch)
            {
                foreach (var c in g.Result.Failures) Log.Error("refused: " + c.Message);
                Report(StatusKind.Problem, "Not launched: " + string.Join(" ", g.Result.Failures.Select(c => c.Message)));
                return false;
            }
            Report(StatusKind.Info, "Preparing the launch (settings snapshot and save backup)...");

            var id = ctx.Paths.UniqueSessionId(DateTime.Now);
            if (LaunchPlanBuilder.WantsRenderSize(ctx.Settings))
                Report(StatusKind.Info, $"Waiting for the headset runtime (up to {OpenXrProbe.DefaultTimeoutMs / 1000} s; SteamVR and Windows Mixed Reality can take a while to start)...");
            var plan = ctx.BuildPlan(g, id);
            if (plan.HeadsetProblem != null)
            {
                Log.Warn("headset check: " + plan.HeadsetProblem);
                if (ConfirmWithoutHeadset != null && !ConfirmWithoutHeadset(plan.HeadsetProblem))
                {
                    Report(StatusKind.Problem, "Not launched: " + plan.HeadsetProblem + " Put the headset on, start its runtime (for a Quest, Virtual Desktop or Meta Horizon Link) and press Launch VR again.");
                    return false;
                }
                Log.Info("headset check: launching anyway");
            }
            var marker = new SessionMarker { State = SessionState.Preparing, SessionId = id, GameExe = plan.ExePath };
            Process game = null;
            try
            {
                // Closing the window stops the launch before the marker, or after the preparation and before the game's
                // start; the start, its marker and the finisher's start are one step the closing window waits for (StartGate).
                var outcome = gate.Launch(cancel, () =>
                {
                    marker.Write(ctx.Paths.SessionMarker);
                    // The cvars of Extra game arguments are put back with the forced ones (the game saves them on exit too).
                    var extraKeys = SessionKeys.FromArguments(ctx.Settings.ExtraArguments);
                    marker.SnapshotDir = SettingsSnapshot.Take(ctx.Paths.Snapshots, id, g.Locations, extraKeys);
                    marker.State = SessionState.Snapshotted;
                    marker.Write(ctx.Paths.SessionMarker);
                    Log.Info("settings snapshot: " + marker.SnapshotDir);
                    if (extraKeys.Count > 0) Log.Info("restored after the session as well (Extra game arguments): " + string.Join(", ", extraKeys));

                    BackUpSaves(id, g);

                    if (plan.Route == LayerRoute.HkcuRegistration)
                        registration.Register(Path.Combine(ctx.LayerDir, "VK_LAYER_ETERNALVR.json"));

                    Directory.CreateDirectory(ctx.Paths.SessionLogDir(id));
                    foreach (var old in SessionLogs.Prune(ctx.Paths.Logs, SessionLogs.Kept, ctx.Paths.SessionLogDir(id)))
                        Log.Info("removed an old session log folder: " + old);
                    Log.Info("launch plan:" + Environment.NewLine + plan.Describe());
                }, () =>
                {
                    game = Start(plan);
                    gameLaunched = true;
                    marker.State = SessionState.Running;
                    marker.GamePid = game.Id;
                    marker.GameStartUtc = game.StartTime.ToUniversalTime();
                    marker.Write(ctx.Paths.SessionMarker);
                    Log.Info($"game started, pid {game.Id}");
                    GameStarted?.Invoke();
                });
                if (outcome != LaunchOutcome.Started)
                {
                    Log.Warn("the launcher is closing; the game is not started");
                    if (outcome == LaunchOutcome.StoppedAfterPreparing) CleanUpAfterSession(marker);
                    return false;
                }
            }
            catch (Exception e)
            {
                Log.Error("launch failed: " + e.Message);
                if (game == null)
                {
                    Report(StatusKind.Problem, "The launch failed: " + e.Message);
                    CleanUpAfterSession(marker);
                    return false;
                }
                // The game did start: its settings are restored only after it has exited, never now.
                Log.Warn("the game was started; its settings are restored after it exits");
            }

            Report(StatusKind.Info, "The game is starting...");
            var logDir = ctx.Paths.SessionLogDir(id);
            var startedUtc = DateTime.UtcNow;
            int? exitCode = null;
            using (game)
            {
                WatchStart(game, cancel, logDir, startedUtc);
                WaitForAllGameProcesses(game, cancel, logDir, startedUtc);
                if (game.HasExited) exitCode = game.ExitCode;
            }
            if (cancel.IsCancellationRequested)
            {
                Log.Warn("stopped waiting for the game; the restore runs at the next launcher start");
                return false;
            }
            Log.Info("the game has exited" + (exitCode.HasValue ? ", " + GameExit.Describe(exitCode.Value) : string.Empty));
            // A crash, or exit code -1 (the game was ended, often after a freeze): a warning that asks for a report.
            string exitText = GameExit.StatusPrefix(exitCode);
            DropHeldProblem();
            bool restored = CleanUpAfterSession(marker);
            // How fast the game really drew, beside the headset's rate (SessionRates), and what the headset did: its refresh
            // rate, the time the runtime held the game to a part of it, refresh changes (SessionSummary, kept for the Play tab).
            var layerLines = SessionRates.ReadLayerLogs(logDir);
            var rates = SessionRates.FromLines(layerLines);
            if (rates != null) Log.Info(rates.LogText());
            var summary = SessionSummary.FromLines(layerLines);
            // No controller action bound: SteamVR's binding chosen for the game is named as the cause, any other gets the report.
            var unbound = UnboundControls.Decide(summary, () => SteamVrSummary.ReadCustomBindings(ctx.SteamRoot).Count > 0);
            if (summary != null)
            {
                Log.Info(summary.LogText());
                ctx.RememberSession(summary, id, DateTime.Now, unbound);
            }
            var controlsText = UnboundControls.StatusText(unbound);
            var summaryText = summary?.Describe();
            var ratesText = !string.IsNullOrEmpty(summaryText) ? " " + summaryText : rates != null ? " " + rates.Describe() : string.Empty;
            // Each eye below the planned size: the graphics driver held it at the window's size (RenderCap).
            var cap = ReadRenderCap(logDir);
            ctx.RememberRenderCap(cap);
            bool capped = cap != null && cap.Capped;
            if (capped)
            {
                Log.Warn("each eye rendered at " + cap.Real + ", " + cap.Percent + "% of the planned " + cap.Planned + " (the window's size)");
                ratesText += " " + cap.Warning();
            }
            // A problem shown during the session (a refusal, VR off) stays on screen; otherwise say how it ended.
            if (lastStatus == null || lastStatus.Kind != StatusKind.Problem)
                Report(restored && exitText.Length == 0 && !capped && controlsText.Length == 0 ? StatusKind.Good : StatusKind.Warning, exitText + (restored
                    ? "The game has exited and your settings were restored."
                    : "The game has exited, but the settings restore is not complete yet; it is retried (see the log).")
                    + (controlsText.Length == 0 ? string.Empty : " " + controlsText) + ratesText);
            return restored;
        }

        /// <summary>The session's eye size against the plan from the layer's status file; null when it is not there.</summary>
        private static RenderCap ReadRenderCap(string logDir)
        {
            var file = Path.Combine(logDir, LayerStatusFile.FileName);
            try { return File.Exists(file) ? RenderCap.FromStatus(File.ReadAllText(file)) : null; }
            catch (IOException) { return null; }
            catch (UnauthorizedAccessException) { return null; }
        }

        /// <summary>Reads the layer's loaded marker and status file and reports what changed.</summary>
        private void WatchLayer(string logDir, DateTime startedUtc)
        {
            bool loaded = File.Exists(Path.Combine(logDir, LayerStatusFile.LoadedMarker));
            LayerStatusFile status = null;
            var file = Path.Combine(logDir, LayerStatusFile.FileName);
            try { if (File.Exists(file)) status = LayerStatusFile.Parse(File.ReadAllText(file)); }
            catch (IOException) { } // mid-replace; read again next time
            catch (UnauthorizedAccessException) { }
            var seconds = (DateTime.UtcNow - startedUtc).TotalSeconds;
            Report(problemHold.Offer(LayerWatch.Decide(loaded, status, seconds), seconds));
        }

        /// <summary>The game has exited: a problem still held was the layer's own shutdown, not something to show.</summary>
        private void DropHeldProblem()
        {
            if (problemHold.Pending != null) Log.Info("the layer reported this as the game exited (not shown): " + problemHold.Pending.Text);
        }

        private void BackUpSaves(string id, LauncherContext.Gathered g)
        {
            var backup = SaveBackups.Create(ctx.Paths.SaveBackups, id, ctx.SaveLocations(g));
            Log.Info(backup == null ? "save backup: no save slot found, nothing to back up" : "save backup: " + backup);
            try { SaveBackups.Rotate(ctx.Paths.SaveBackups); }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException) { Log.Warn("removing old save backups failed: " + e.Message); }
        }

        private bool CleanUpAfterSession(SessionMarker marker)
        {
            try { registration.RemoveStale(); }
            catch (Exception e) { Log.Error("removing the layer registration failed: " + e.Message); }
            if (marker.State == SessionState.Preparing)
            {
                SessionMarker.DeleteIfOwned(ctx.Paths.SessionMarker, marker.SessionId);
                return false;
            }
            var running = ctx.RunningGameProcesses();
            if (SessionRecovery.Decide(marker, running.Count > 0) == RecoveryAction.WaitForGame)
            {
                // The game writes its config again on exit, so a restore now would be undone.
                Log.Warn($"{string.Join(", ", running)} is still running; the settings are restored once it exits");
                return false;
            }
            return RestoreAndClear(marker);
        }

        private static Process Start(LaunchPlan plan)
        {
            var psi = new ProcessStartInfo(plan.ExePath, plan.CommandLine)
            {
                UseShellExecute = false,
                WorkingDirectory = plan.WorkingDirectory,
            };
            ChildEnvironment.Apply(psi, plan.Environment);
            return Process.Start(psi) ?? throw new InvalidOperationException("the game process did not start");
        }

        private void WatchStart(Process game, CancellationToken cancel, string logDir, DateTime startedUtc)
        {
            var started = DateTime.UtcNow;
            DateTime? exitSeen = null;
            while (!cancel.IsCancellationRequested)
            {
                var now = DateTime.UtcNow;
                var elapsed = (now - started).TotalSeconds;
                bool exited = game.HasExited;
                if (exited && exitSeen == null) exitSeen = now;
                var sinceExit = exitSeen.HasValue ? (now - exitSeen.Value).TotalSeconds : 0;
                bool other = ctx.GameProcessNames.SelectMany(WindowsSystem.RunningProcessIds).Any(pid => pid != game.Id);
                var outcome = StartWatch.Decide(exited, elapsed, sinceExit, other);
                if (outcome == StartOutcome.HandOff)
                {
                    Log.Error(StartWatch.HandOffMessage);
                    Report(StatusKind.Problem, StartWatch.HandOffMessage);
                    return;
                }
                if (outcome == StartOutcome.ExitedEarly)
                {
                    Log.Error($"the game exited {elapsed - sinceExit:0.0} s after starting (exit code {game.ExitCode}); see the logs in {ctx.Paths.Logs}");
                    Report(StatusKind.Problem, $"The game closed {elapsed - sinceExit:0} s after it started (exit code {game.ExitCode}). "
                        + "Use Export report... and attach the zip to a GitHub issue if it keeps happening.");
                    return;
                }
                if (!exited) WatchLayer(logDir, startedUtc);
                if (outcome == StartOutcome.Exited) return;
                if (outcome == StartOutcome.Running && elapsed > StartWatch.WatchSeconds) return;
                cancel.WaitHandle.WaitOne(250);
            }
        }

        private void WaitForAllGameProcesses(Process game, CancellationToken cancel, string logDir, DateTime startedUtc)
        {
            while (!cancel.IsCancellationRequested)
            {
                if (game.HasExited && ctx.RunningGameProcesses().Count == 0) return;
                if (!game.HasExited) WatchLayer(logDir, startedUtc);
                cancel.WaitHandle.WaitOne(1000);
            }
        }
    }
}
