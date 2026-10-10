using System;
using System.Diagnostics;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows.Forms;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Safety;
using EternalVR.Launcher.Core.Settings;
using EternalVR.Launcher.Platform;

namespace EternalVR.Launcher
{
    internal static class Program
    {
        /// <summary>Exit code when another launcher already uses the same data folder.</summary>
        private const int AlreadyRunning = 3;
        private const string AlreadyRunningMessage = "Another EternalVR launcher is already open with this data folder. Use that one, or close it first.";

        [STAThread]
        private static int Main(string[] args)
        {
            // Before any Core type is touched: a launcher started from inside the zip cannot load its Core assembly.
            var problem = InstallCheck.Problem(args);
            if (problem != null)
            {
                if (args.Length == 0) MessageBox.Show(problem, "EternalVR", MessageBoxButtons.OK, MessageBoxIcon.Error);
                else
                {
                    AttachParentConsole();
                    Console.Error.WriteLine(problem);
                }
                return 1;
            }
            // The headset probe's copy has no window: an error there is its answer, not a dialog nobody sees.
            if (args.Length > 0 && string.Equals(args[0], ProbeChild.Switch, StringComparison.OrdinalIgnoreCase))
                AppDomain.CurrentDomain.UnhandledException += (s, e) =>
                {
                    var x = e.ExceptionObject as Exception;
                    var why = x == null ? "unknown error" : x.GetType().Name + ": " + x.Message;
                    Answer(OpenXrProbeResult.Failed("the probe stopped: " + why), 1);
                };
            else
                CrashHandler.Install();
            return Run(args);
        }

        [MethodImpl(MethodImplOptions.NoInlining)]
        private static int Run(string[] args)
        {
            // The copy started inside the game's package for a Game Pass launch: it starts the game and ends (PackageStart).
            if (args.Length == 2 && string.Equals(args[0], PackageStart.HelperSwitch, StringComparison.OrdinalIgnoreCase))
                return PackageLaunch.RunHelper(args[1]);
            LauncherOptions options;
            try { options = LauncherOptions.Parse(args); }
            catch (ArgumentException e)
            {
                AttachParentConsole();
                Console.Error.WriteLine(e.Message);
                Console.Error.WriteLine(LauncherOptions.Usage);
                return 64;
            }

            if (options.FinishSession) return FinishSession.Run(options);
            if (options.ProbeHeadset != null) return ProbeHeadset(options.ProbeHeadset);
            if (options.Headless)
            {
                AttachParentConsole();
                return RunHeadless(options);
            }

            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            using (var instance = AcquireInstance(options))
            {
                if (instance == null)
                {
                    MessageBox.Show(AlreadyRunningMessage, "EternalVR", MessageBoxButtons.OK, MessageBoxIcon.Information);
                    return AlreadyRunning;
                }
                var log = new Log();
                CrashHandler.Attach(log);
                LauncherContext ctx;
                try { ctx = LauncherContext.Create(options, log); }
                catch (Exception e) when (IsStartupError(e))
                {
                    MessageBox.Show(e.Message, "EternalVR", MessageBoxButtons.OK, MessageBoxIcon.Error);
                    return 1;
                }
                Application.Run(new MainForm(ctx));
            }
            // After an update: the new launcher, started once this one's lock is released, with the same options.
            if (MainForm.RestartExe != null)
                Process.Start(new ProcessStartInfo(MainForm.RestartExe, string.Join(" ", args.Select(LaunchPlan.QuoteIfNeeded))) { UseShellExecute = false });
            return 0;
        }

        /// <summary>
        /// <c>--probe-headset</c>: the runtime probe in this short-lived copy (<see cref="ProbeChild"/>); the answer goes to
        /// standard output, which the launcher reads. Exit 0 when the runtime answered, 2 when it did not, 1 on an error.
        /// </summary>
        private static int ProbeHeadset(string loader)
        {
            // A runtime that starts its server (SteamVR) from here must not hand it this copy's output pipe: the launcher
            // would then wait on a pipe that stays open as long as SteamVR runs.
            foreach (var std in new[] { StdInputHandle, StdOutputHandle, StdErrorHandle })
                SetHandleInformation(GetStdHandle(std), HandleFlagInherit, 0);
            var result = OpenXrProbe.Run(loader, null);
            Answer(result, result.Ok ? 0u : 2u);
            return 1;
        }

        /// <summary>
        /// The probe copy's answer on standard output, then the copy ends at once: a runtime that keeps a thread or the
        /// loader lock cannot hold it open (the normal exit unloads the runtime's DLLs, which may wait on them).
        /// </summary>
        private static void Answer(OpenXrProbeResult result, uint exitCode)
        {
            var bytes = new System.Text.UTF8Encoding(false).GetBytes(ProbeChild.Serialize(result));
            try
            {
                using (var stdout = Console.OpenStandardOutput())
                {
                    stdout.Write(bytes, 0, bytes.Length);
                    stdout.Flush();
                }
            }
            catch (System.IO.IOException) { exitCode = 1; }
            TerminateProcess(GetCurrentProcess(), exitCode);
        }

        /// <summary>
        /// The data folder's lock; while the session finisher holds it to complete a restore, it is waited for
        /// (<see cref="InstanceLock.TryAcquireAfterFinisher"/>) instead of saying another launcher is open.
        /// </summary>
        private static InstanceLock AcquireInstance(LauncherOptions options)
        {
            var root = LauncherContext.DataRootFor(options);
            var names = options.TestExe != null ? new[] { System.IO.Path.GetFileNameWithoutExtension(options.TestExe) } : Core.Game.GameLayout.GameProcessNames;
            return InstanceLock.TryAcquireAfterFinisher(root, new Core.DataPaths(root).SessionMarker,
                () => names.Any(Platform.WindowsSystem.IsProcessRunning));
        }

        private static bool IsStartupError(Exception e) =>
            e is SettingsException || e is System.IO.IOException || e is FormatException || e is UnauthorizedAccessException;

        private static int RunHeadless(LauncherOptions options)
        {
            if (options.Help)
            {
                Console.WriteLine(LauncherOptions.Usage);
                return 0;
            }

            using (var instance = AcquireInstance(options))
            {
                if (instance == null)
                {
                    Console.Error.WriteLine(AlreadyRunningMessage);
                    return AlreadyRunning;
                }
                return RunHeadlessLocked(options);
            }
        }

        private static int RunHeadlessLocked(LauncherOptions options)
        {
            var log = new Log();
            log.Line += Console.WriteLine;
            LauncherContext ctx;
            try { ctx = LauncherContext.Create(options, log); }
            catch (Exception e) when (IsStartupError(e))
            {
                Console.Error.WriteLine(e.Message);
                return 1;
            }

            log.Info($"EternalVR launcher {typeof(Program).Assembly.GetName().Version}, data folder {ctx.Paths.Root}");
            var runner = new SessionRunner(ctx);
            try
            {
                if (options.ExportReport != null) return ReportExport.RunHeadless(ctx, options.ExportReport);
                if (options.RestoreSaves) return RestoreSaves(ctx, runner);
                if (options.Launch) return runner.Run(CancellationToken.None) ? 0 : 1;

                bool clear = runner.Recover();
                var plan = runner.DryRun(takeCopies: true, out var gathered);
                Console.WriteLine();
                Console.WriteLine("== Preflight");
                foreach (var c in gathered.Result.Checks) Console.WriteLine(c);
                Console.WriteLine();
                Console.WriteLine("== Launch plan (dry run, not started)");
                Console.WriteLine(plan == null ? "no plan: the game was not found" : plan.Describe());
                Console.WriteLine(gathered.Result.CanLaunch && clear ? "RESULT: would launch" : "RESULT: would refuse");
                return gathered.Result.CanLaunch && clear ? 0 : 2;
            }
            catch (Exception e)
            {
                log.Error(e.ToString());
                return 1;
            }
        }

        /// <summary>Exit code of <c>--restore-saves</c> when the saves were restored but Steam's record is still stale.</summary>
        private const int RestoredRecordStale = 4;

        private static int RestoreSaves(LauncherContext ctx, SessionRunner runner)
        {
            var newest = SaveBackups.List(ctx.Paths.SaveBackups).FirstOrDefault();
            if (newest == null)
            {
                ctx.Log.Error("no save backup found in " + ctx.Paths.SaveBackups);
                return 1;
            }
            var r = SaveRestore.Run(ctx.Paths.SaveBackups, newest, DateTime.Now, ctx.SettingsLocations(),
                                    ctx.ResyncHost(), ctx.ResyncOptions(), ctx.Log.Info);
            switch (r.Outcome)
            {
                case SaveRestoreOutcome.Refused: return 2;
                case SaveRestoreOutcome.RestoredRecordStale: return RestoredRecordStale;
                default: return 0;
            }
        }

        private static void AttachParentConsole()
        {
            // A WinForms exe has no console; reuse the caller's when there is one. Redirected output
            // (pipes, files) already works without it.
            AttachConsole(-1);
        }

        [DllImport("kernel32.dll")]
        private static extern bool AttachConsole(int processId);

        private const int StdInputHandle = -10;
        private const int StdOutputHandle = -11;
        private const int StdErrorHandle = -12;
        private const uint HandleFlagInherit = 0x00000001;

        [DllImport("kernel32.dll")]
        private static extern IntPtr GetStdHandle(int which);

        [DllImport("kernel32.dll")]
        private static extern bool SetHandleInformation(IntPtr handle, uint mask, uint flags);

        [DllImport("kernel32.dll")]
        private static extern IntPtr GetCurrentProcess();

        [DllImport("kernel32.dll")]
        private static extern bool TerminateProcess(IntPtr process, uint exitCode);
    }
}
