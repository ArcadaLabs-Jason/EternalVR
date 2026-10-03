using System;
using System.ComponentModel;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using EternalVR.Launcher.Core.Launch;

namespace EternalVR.Launcher.Platform
{
    /// <summary>The game started inside its package (<see cref="PackageStart"/>): the launcher's side and the copy's.</summary>
    public static class PackageLaunch
    {
        private const int PowerShellTimeoutMs = 30000;

        /// <summary>
        /// Starts the game of <paramref name="plan"/> inside the package and returns its process, opened before it runs;
        /// throws (the reason in the message) when it could not, with nothing left running.
        /// </summary>
        public static Process Start(PackageStartPlan plan, string planFile, Log log)
        {
            var pidFile = planFile + PackageStart.PidSuffix;
            var goFile = planFile + PackageStart.GoSuffix;
            foreach (var f in new[] { pidFile, goFile }) File.Delete(f);
            File.WriteAllText(planFile, PackageStart.Write(plan), new UTF8Encoding(false));

            var exe = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), @"WindowsPowerShell\v1.0\powershell.exe");
            var command = PackageStart.Command(Process.GetCurrentProcess().MainModule.FileName, planFile);
            var psi = new ProcessStartInfo(exe, "-NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand "
                + Convert.ToBase64String(Encoding.Unicode.GetBytes(command)))
            {
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
            };
            using (var ps = Process.Start(psi) ?? throw new InvalidOperationException("Windows PowerShell did not start"))
            {
                var output = ps.StandardOutput.ReadToEndAsync();
                var errors = ps.StandardError.ReadToEndAsync();
                if (!ps.WaitForExit(PowerShellTimeoutMs))
                {
                    try { ps.Kill(); } catch (InvalidOperationException) { } catch (Win32Exception) { }
                    throw new InvalidOperationException("Invoke-CommandInDesktopPackage did not answer in " + PowerShellTimeoutMs / 1000 + " s");
                }
                if (ps.ExitCode != 0) throw new InvalidOperationException("Invoke-CommandInDesktopPackage failed: " + OneLine(errors.Result));
                log.Info("package start: " + OneLine(output.Result));
            }

            int pid = WaitForPid(pidFile, PackageStart.PidTimeoutMs);
            if (pid <= 0) throw new InvalidOperationException("the copy of the launcher inside the package did not start the game within " + PackageStart.PidTimeoutMs / 1000 + " s");
            Process game;
            try
            {
                game = Process.GetProcessById(pid);
                var unused = game.Handle; // held from now on, so the exit code can still be read after the game ends
            }
            catch (Exception e) when (e is ArgumentException || e is InvalidOperationException || e is Win32Exception)
            {
                throw new InvalidOperationException("the game (pid " + pid + ") could not be opened: " + e.Message);
            }
            File.WriteAllText(goFile, "go");
            return game;
        }

        /// <summary>
        /// The copy inside the package (<see cref="PackageStart.HelperSwitch"/>): starts the plan's game suspended, reports its
        /// process ID and resumes it on the launcher's go. Exit code 0 when the game was resumed.
        /// </summary>
        public static int RunHelper(string planFile)
        {
            PackageStartPlan plan;
            try { plan = PackageStart.Read(File.ReadAllText(planFile)); }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException || e is ArgumentException) { return 2; }
            if (plan == null) return 2;

            var si = new StartupInfo { cb = Marshal.SizeOf(typeof(StartupInfo)) };
            var commandLine = new StringBuilder(LaunchPlan.QuoteIfNeeded(plan.ExePath) + " " + (plan.CommandLine ?? string.Empty));
            var block = Marshal.StringToHGlobalUni(PackageStart.EnvironmentBlock(plan.Environment));
            try
            {
                if (!CreateProcess(plan.ExePath, commandLine, IntPtr.Zero, IntPtr.Zero, false, CreateSuspended | CreateUnicodeEnvironment,
                        block, plan.WorkingDirectory, ref si, out var pi))
                {
                    File.WriteAllText(planFile + PackageStart.PidSuffix, "error " + Marshal.GetLastWin32Error().ToString(CultureInfo.InvariantCulture));
                    return 3;
                }
                try
                {
                    File.WriteAllText(planFile + PackageStart.PidSuffix, pi.dwProcessId.ToString(CultureInfo.InvariantCulture));
                    var deadline = DateTime.UtcNow.AddMilliseconds(PackageStart.GoTimeoutMs);
                    while (!File.Exists(planFile + PackageStart.GoSuffix))
                    {
                        if (DateTime.UtcNow > deadline)
                        {
                            TerminateProcess(pi.hProcess, 1);
                            return 4;
                        }
                        Thread.Sleep(20);
                    }
                    ResumeThread(pi.hThread);
                    return 0;
                }
                finally
                {
                    CloseHandle(pi.hThread);
                    CloseHandle(pi.hProcess);
                }
            }
            finally { Marshal.FreeHGlobal(block); }
        }

        private static int WaitForPid(string pidFile, int timeoutMs)
        {
            var deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
            while (DateTime.UtcNow < deadline)
            {
                try
                {
                    if (File.Exists(pidFile))
                    {
                        var text = File.ReadAllText(pidFile).Trim();
                        if (text.StartsWith("error", StringComparison.Ordinal))
                            throw new InvalidOperationException("the copy of the launcher inside the package could not start the game: Windows error " + text.Substring(5).Trim());
                        if (int.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out var pid) && pid > 0) return pid;
                    }
                }
                catch (IOException) { } // still being written
                Thread.Sleep(50);
            }
            return 0;
        }

        private static string OneLine(string s) => string.Join(" ", (s ?? string.Empty).Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries)).Trim();

        private const uint CreateSuspended = 0x4, CreateUnicodeEnvironment = 0x400;

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct StartupInfo
        {
            public int cb;
            public string lpReserved, lpDesktop, lpTitle;
            public int dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
            public short wShowWindow, cbReserved2;
            public IntPtr lpReserved2, hStdInput, hStdOutput, hStdError;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct ProcessInformation
        {
            public IntPtr hProcess, hThread;
            public int dwProcessId, dwThreadId;
        }

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true, EntryPoint = "CreateProcessW")]
        private static extern bool CreateProcess(string application, StringBuilder commandLine, IntPtr processAttributes, IntPtr threadAttributes,
            bool inheritHandles, uint flags, IntPtr environment, string currentDirectory, ref StartupInfo startupInfo, out ProcessInformation info);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern uint ResumeThread(IntPtr thread);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool TerminateProcess(IntPtr process, uint exitCode);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool CloseHandle(IntPtr handle);
    }
}
