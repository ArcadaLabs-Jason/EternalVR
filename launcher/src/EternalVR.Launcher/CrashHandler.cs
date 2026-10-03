using System;
using System.IO;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace EternalVR.Launcher
{
    /// <summary>
    /// Last-resort handling of exceptions nothing else caught: a log line (the launcher log once it is open, else
    /// <c>logs\launcher.log</c> in the default data folder) and a plain dialog instead of the .NET crash box.
    /// </summary>
    internal static class CrashHandler
    {
        private static Log log;

        public static void Install()
        {
            Application.SetUnhandledExceptionMode(UnhandledExceptionMode.CatchException);
            Application.ThreadException += (s, e) => Report(e.Exception, fatal: false);
            AppDomain.CurrentDomain.UnhandledException += (s, e) => Report(e.ExceptionObject as Exception, fatal: true);
            TaskScheduler.UnobservedTaskException += (s, e) =>
            {
                Write("unobserved task error: " + e.Exception);
                e.SetObserved();
            };
        }

        /// <summary>From now on crashes are logged to the launcher's own log.</summary>
        public static void Attach(Log launcherLog) => log = launcherLog;

        private static void Report(Exception e, bool fatal)
        {
            var where = Write((fatal ? "fatal error: " : "unexpected error: ") + e);
            var text = "EternalVR hit an unexpected error" + (fatal ? " and has to close" : string.Empty) + ":\n\n"
                + (e?.Message ?? "unknown error") + "\n\nDetails are in " + where
                + ". If it happens again: " + Core.Report.ReportHint.Ask;
            try { MessageBox.Show(text, "EternalVR", MessageBoxButtons.OK, MessageBoxIcon.Error); }
            catch (InvalidOperationException) { }
        }

        /// <summary>Writes the line and returns the file it went to.</summary>
        private static string Write(string line)
        {
            if (log != null)
            {
                log.Error(line);
                return "the launcher log (logs\\launcher.log in the data folder)";
            }
            var file = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "EternalVR", "logs", "launcher.log");
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(file));
                File.AppendAllText(file, $"{DateTime.Now:yyyy-MM-dd HH:mm:ss} ERROR {line}{Environment.NewLine}");
            }
            catch (Exception io) when (io is IOException || io is UnauthorizedAccessException) { }
            return file;
        }
    }
}
