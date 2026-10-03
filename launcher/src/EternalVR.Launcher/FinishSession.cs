using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Threading;
using EternalVR.Launcher.Core.Launch;

namespace EternalVR.Launcher
{
    /// <summary>
    /// <c>--finish-session</c>: a windowless copy of the launcher started with each VR session. It waits for the game to exit
    /// and, when the launcher is no longer open (closed or killed mid-session), completes the settings restore itself, so the
    /// flat game never keeps the VR settings until the launcher is opened again. While the launcher is open, the launcher
    /// restores (it holds the instance lock) and this copy just exits.
    /// </summary>
    internal static class FinishSession
    {
        private const int PollMs = 2000;
        /// <summary>After the game exits: time for a launcher that is open to restore, or one that is closing to let go of its lock.</summary>
        private const int SettleMs = 3000;

        /// <summary>Starts the helper for the running session with the launcher's own path options.</summary>
        public static void Start(LauncherContext ctx)
        {
            var exe = Process.GetCurrentProcess().MainModule?.FileName;
            if (string.IsNullOrEmpty(exe)) return;
            // Quoted by the rules Windows splits a command line with: a folder ending in a backslash keeps its closing quote.
            var args = Environment.GetCommandLineArgs().Skip(1).Select(LaunchPlan.QuoteIfNeeded).Concat(new[] { "--finish-session" });
            try
            {
                using (Process.Start(new ProcessStartInfo(exe, string.Join(" ", args))
                {
                    UseShellExecute = false, CreateNoWindow = true, WorkingDirectory = ctx.ProgramDir,
                }))
                {
                }
                ctx.Log.Info("started the session finisher (it restores the settings if the launcher is closed before the game)");
            }
            catch (Exception e) when (e is InvalidOperationException || e is System.ComponentModel.Win32Exception)
            {
                ctx.Log.Warn("the session finisher could not be started: " + e.Message + "; keep the launcher open until the game exits");
            }
        }

        public static int Run(LauncherOptions options)
        {
            var log = new Log();
            LauncherContext ctx;
            try { ctx = LauncherContext.Create(options, log); }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException || e is FormatException || e is Core.Settings.SettingsException)
            {
                return 1;
            }
            // Wait for the session's game to exit, or for someone else to finish the session.
            while (File.Exists(ctx.Paths.SessionMarker) && ctx.RunningGameProcesses().Count > 0) Thread.Sleep(PollMs);
            if (!File.Exists(ctx.Paths.SessionMarker)) return 0;
            Thread.Sleep(SettleMs);

            if (!File.Exists(ctx.Paths.SessionMarker)) return 0;
            using (var instance = InstanceLock.TryAcquire(ctx.Paths.Root))
            {
                // The launcher is open: it restores (its session or its recovery timer).
                if (instance == null) return 0;
                if (ctx.RunningGameProcesses().Count > 0) return 0; // a new game started meanwhile: its launcher's business
                log.Info("finisher: the launcher is closed and the game has exited; completing the settings restore");
                bool clear = new SessionRunner(ctx).Recover();
                log.Info(clear ? "finisher: done" : "finisher: the restore did not complete; the launcher retries it when it opens");
                return clear ? 0 : 1;
            }
        }
    }
}
