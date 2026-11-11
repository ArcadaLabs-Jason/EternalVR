using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Threading;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Report;
using EternalVR.Launcher.Platform;

namespace EternalVR.Launcher
{
    /// <summary>
    /// The game's start for the session: directly or inside its package (with the DOOM Eternal Launcher's
    /// launch_target put back), and the watch of the start and of the game's processes until they have exited.
    /// </summary>
    public sealed partial class SessionRunner
    {
        // The DOOM Eternal Launcher's launch_target before the start (PackageStart.BethesdaSettings): the copy inside the
        // package puts it back once the game runs; the session puts it back again if anything left it changed.
        private string bethesdaTarget;

        private static string ReadBethesdaTarget(string gameExe)
        {
            try { return PackageStart.LaunchTarget(File.ReadAllText(PackageStart.BethesdaSettings(gameExe))); }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException || e is ArgumentException) { return null; }
        }

        private void RestoreBethesdaTarget(string gameExe) => RestoreBethesdaTarget(gameExe, bethesdaTarget);

        private void RestoreBethesdaTarget(string gameExe, string target)
        {
            if (target == null) return;
            var file = PackageStart.BethesdaSettings(gameExe);
            try
            {
                var bytes = File.ReadAllBytes(file);
                bool bom = bytes.Length >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF;
                var text = new System.Text.UTF8Encoding(false).GetString(bytes, bom ? 3 : 0, bytes.Length - (bom ? 3 : 0));
                var now = PackageStart.LaunchTarget(text);
                if (now == null || now == target) return;
                File.WriteAllText(file, PackageStart.WithLaunchTarget(text, target), new System.Text.UTF8Encoding(bom));
                Log.Info("the DOOM Eternal Launcher's launch_target put back to " + target + " (was " + now + ")");
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException || e is ArgumentException)
            {
                Log.Warn("the DOOM Eternal Launcher's launch_target could not be put back to " + target + ": " + e.Message);
            }
        }

        /// <summary>ETERNALVR_PACKAGE_START=0 in the launcher's environment starts a Game Pass game directly, as before 0.1.18;
        /// =game starts it inside the package without the DOOM Eternal Launcher, as 0.1.18 did.</summary>
        private static bool PackageStartOn() => Environment.GetEnvironmentVariable("ETERNALVR_PACKAGE_START") != "0";

        private static bool PackageStartViaLauncher() => Environment.GetEnvironmentVariable("ETERNALVR_PACKAGE_START") != "game";

        /// <summary>
        /// The Game Pass or Microsoft Store game started inside its package, as the Xbox app starts it (PackageStart), with the
        /// environment a direct start would give it; a direct start when that fails.
        /// </summary>
        private Process StartInPackage(LaunchPlan plan, string id)
        {
            var planFile = Path.Combine(ctx.Paths.SessionLogDir(id), "package-start.txt");
            // The DOOM Eternal Launcher can take a while to start the game: say what is happening meanwhile.
            if (PackageStartViaLauncher()) Report(StatusKind.Info, "Starting the game through the DOOM Eternal Launcher...");
            try
            {
                var env = ChildEnvironment.Merge(ChildEnvironment.Current(), plan.Environment, Environment.GetEnvironmentVariable);
                var game = PackageLaunch.Start(new PackageStartPlan
                {
                    ExePath = plan.ExePath,
                    WorkingDirectory = plan.WorkingDirectory,
                    CommandLine = plan.CommandLine,
                    Environment = env.ToList(),
                    ViaLauncher = PackageStartViaLauncher(),
                }, planFile, Log, out var viaLauncher);
                Log.Info("started inside the package " + PackageStart.PackageName
                    + (viaLauncher ? " through the DOOM Eternal Launcher" : " directly"));
                return game;
            }
            catch (Exception e) when (e is InvalidOperationException || e is IOException || e is UnauthorizedAccessException
                                      || e is System.ComponentModel.Win32Exception)
            {
                RestoreBethesdaTarget(plan.ExePath);
                Log.Warn("starting the game inside its package failed (" + e.Message + "); starting it directly");
                return Start(plan);
            }
            finally
            {
                // The plan holds the whole environment: it is not left in the session folder.
                try { File.Delete(planFile); } catch (IOException) { } catch (UnauthorizedAccessException) { }
            }
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

        private static DateTime GameStartUtc(Process game, DateTime fallbackUtc)
        {
            try { return game.StartTime.ToUniversalTime(); }
            catch (Exception e) when (e is InvalidOperationException || e is System.ComponentModel.Win32Exception || e is NotSupportedException)
            {
                return fallbackUtc.AddSeconds(-1);
            }
        }

        /// <summary>The game's own fatal error from this launch's console log, logged for the export; null when it wrote none.</summary>
        private string GameOwnError(bool layerLoaded, DateTime sinceUtc)
        {
            var line = GameFatalError.Lookup(layerLoaded, () => ctx.SavedGamesDir, ctx.Settings.ExtraArguments, sinceUtc);
            if (line != null) Log.Info("the game's own error (last FATAL ERROR line of its console log): " + line);
            return line;
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
                // A hand-off is Steam's (it restarts the game itself); a Store game's DOOM Eternal Launcher outlives it briefly.
                bool other = !storeGame && ctx.GameProcessNames.SelectMany(WindowsSystem.RunningProcessIds).Any(pid => pid != game.Id);
                var outcome = StartWatch.Decide(exited, elapsed, sinceExit, other);
                if (outcome == StartOutcome.HandOff)
                {
                    Log.Error(StartWatch.HandOffMessage);
                    Report(StatusKind.Problem, StartWatch.HandOffMessage);
                    return;
                }
                if (outcome == StartOutcome.ExitedEarly)
                {
                    bool layerLoaded = File.Exists(Path.Combine(logDir, LayerStatusFile.LoadedMarker));
                    Log.Error($"the game exited {elapsed - sinceExit:0.0} s after starting (exit code {game.ExitCode}), "
                        + (layerLoaded ? "after EternalVR loaded" : "before EternalVR loaded (no " + LayerStatusFile.LoadedMarker + ")")
                        + $"; see the logs in {ctx.Paths.Logs}");
                    if (storeGame) Log.Info(StoreProcesses.Describe("after the early exit", WindowsSystem.RunningProcesses(StoreProcesses.Names)));
                    Report(StatusKind.Problem, StartWatch.EarlyExitMessage(elapsed - sinceExit, game.ExitCode, layerLoaded,
                        GameOwnError(layerLoaded, GameStartUtc(game, startedUtc))));
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
