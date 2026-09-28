using System;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows.Forms;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Safety;
using EternalVR.Launcher.Core.Settings;

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
            CrashHandler.Install();
            return Run(args);
        }

        [MethodImpl(MethodImplOptions.NoInlining)]
        private static int Run(string[] args)
        {
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
            if (options.Headless)
            {
                AttachParentConsole();
                return RunHeadless(options);
            }

            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            using (var instance = InstanceLock.TryAcquire(LauncherContext.DataRootFor(options)))
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
                return 0;
            }
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

            using (var instance = InstanceLock.TryAcquire(LauncherContext.DataRootFor(options)))
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
    }
}
