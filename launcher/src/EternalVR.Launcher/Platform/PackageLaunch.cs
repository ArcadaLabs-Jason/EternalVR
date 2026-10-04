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
        /// <summary>The game the DOOM Eternal Launcher started had ended before the launcher could open it.</summary>
        public sealed class GameGoneException : Exception
        {
            public GameGoneException(string message) : base(message) { }
        }

        private const int PowerShellTimeoutMs = 30000;

        /// <summary>
        /// Starts the game of <paramref name="plan"/> inside the package and returns its process, opened before it runs;
        /// throws (the reason in the message) when it could not, with nothing left running.
        /// </summary>
        public static Process Start(PackageStartPlan plan, string planFile, Log log, out bool viaLauncher)
        {
            viaLauncher = false;
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

            int pid = WaitForPid(pidFile, PackageStart.PidTimeoutMs, out viaLauncher);
            if (pid <= 0) throw new InvalidOperationException("the copy of the launcher inside the package did not start the game within " + PackageStart.PidTimeoutMs / 1000 + " s");
            Process game;
            try
            {
                game = Process.GetProcessById(pid);
                var unused = game.Handle; // held from now on, so the exit code can still be read after the game ends
            }
            catch (Exception e) when (e is ArgumentException || e is InvalidOperationException || e is Win32Exception)
            {
                // Started by the DOOM Eternal Launcher, the game runs before it is opened: gone already means it ended at
                // once, and starting it again directly would only hide that.
                if (viaLauncher) throw new GameGoneException("the game (pid " + pid + ") ended right after the DOOM Eternal Launcher started it");
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
            if (plan.ViaLauncher && RunViaLauncher(plan, planFile)) return 0;

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
                var resumed = false;
                try
                {
                    File.WriteAllText(planFile + PackageStart.PidSuffix, pi.dwProcessId.ToString(CultureInfo.InvariantCulture));
                    var deadline = DateTime.UtcNow.AddMilliseconds(PackageStart.GoTimeoutMs);
                    while (!File.Exists(planFile + PackageStart.GoSuffix))
                    {
                        if (DateTime.UtcNow > deadline) return 4;
                        Thread.Sleep(20);
                    }
                    resumed = ResumeThread(pi.hThread) != uint.MaxValue;
                    return resumed ? 0 : 5;
                }
                catch (Exception e) when (e is IOException || e is UnauthorizedAccessException)
                {
                    return 6;
                }
                finally
                {
                    // Never leave a suspended game behind: it would count as running until it is ended by hand.
                    if (!resumed) TerminateProcess(pi.hProcess, 1);
                    CloseHandle(pi.hThread);
                    CloseHandle(pi.hProcess);
                }
            }
            finally { Marshal.FreeHGlobal(block); }
        }

        /// <summary>
        /// Starts the plan's game through the DOOM Eternal Launcher (<see cref="PackageStart.BethesdaLauncher"/>) with its window
        /// skipped for this start (<c>launch_target</c> <see cref="PackageStart.SkipLauncherTarget"/>, the player's own value put
        /// back once the game runs), the plan's command line and environment, which the launcher hands to the game. Reports the
        /// game's process ID as "<c>pid launcher</c>"; false, with nothing left running, when the launcher or its settings are
        /// missing or no game started in time (then the game is started directly).
        /// </summary>
        private static bool RunViaLauncher(PackageStartPlan plan, string planFile)
        {
            var launcher = PackageStart.BethesdaLauncher(plan.ExePath);
            var settingsFile = PackageStart.BethesdaSettings(plan.ExePath);
            if (!File.Exists(launcher) || !File.Exists(settingsFile)) return false;
            string settings;
            bool bom;
            try
            {
                var bytes = File.ReadAllBytes(settingsFile);
                bom = bytes.Length >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF;
                settings = new UTF8Encoding(false).GetString(bytes, bom ? 3 : 0, bytes.Length - (bom ? 3 : 0));
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException) { return false; }
            var target = PackageStart.LaunchTarget(settings);
            var skipped = PackageStart.WithLaunchTarget(settings, PackageStart.SkipLauncherTarget);
            if (target == null || skipped == null) return false;
            var encoding = new UTF8Encoding(bom);
            bool changed = target != PackageStart.SkipLauncherTarget;
            var gameName = Path.GetFileNameWithoutExtension(plan.ExePath);
            var launcherName = Path.GetFileNameWithoutExtension(launcher);
            var gamesBefore = ProcessIds(gameName);
            var launchersBefore = ProcessIds(launcherName);
            bool started = false;
            var si = new StartupInfo { cb = Marshal.SizeOf(typeof(StartupInfo)) };
            var commandLine = new StringBuilder(LaunchPlan.QuoteIfNeeded(launcher) + " " + (plan.CommandLine ?? string.Empty));
            var block = Marshal.StringToHGlobalUni(PackageStart.EnvironmentBlock(plan.Environment));
            try
            {
                if (changed) File.WriteAllText(settingsFile, skipped, encoding);
                if (!CreateProcess(launcher, commandLine, IntPtr.Zero, IntPtr.Zero, false, CreateUnicodeEnvironment,
                        block, plan.WorkingDirectory, ref si, out var pi))
                    return false;
                CloseHandle(pi.hThread);
                int game = 0;
                try
                {
                    var deadline = DateTime.UtcNow.AddMilliseconds(PackageStart.LauncherTimeoutMs);
                    DateTime? launcherGone = null;
                    while (DateTime.UtcNow < deadline && game == 0)
                    {
                        game = NewProcess(gameName, gamesBefore);
                        if (game != 0) break;
                        // The launcher closed without starting the game: a short grace for a child it handed over, then no wait.
                        if (launcherGone == null && WaitForSingleObject(pi.hProcess, 0) == 0) launcherGone = DateTime.UtcNow;
                        if (launcherGone.HasValue && (DateTime.UtcNow - launcherGone.Value).TotalSeconds > 3) break;
                        Thread.Sleep(50);
                    }
                }
                finally { CloseHandle(pi.hProcess); }
                if (game == 0) game = NewProcess(gameName, gamesBefore); // a game that appeared at the deadline is still the one
                if (game != 0)
                {
                    File.WriteAllText(planFile + PackageStart.PidSuffix, game.ToString(CultureInfo.InvariantCulture) + " launcher");
                    started = true;
                    return true;
                }
                return false;
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException) { return false; }
            finally
            {
                // A failed start leaves nothing running: the launcher it started (it would count as the game running) and any
                // game it started late, before the game is started directly. The launchers first, so that none can start a
                // game after the games were ended.
                if (!started)
                {
                    EndNew(launcherName, launchersBefore);
                    EndNew(gameName, gamesBefore);
                }
                Marshal.FreeHGlobal(block);
                if (changed)
                {
                    // The player's own value back, in the file as it is now (the launcher may have written it meanwhile).
                    try
                    {
                        var now = File.ReadAllText(settingsFile);
                        var restored = PackageStart.WithLaunchTarget(now, target);
                        if (restored != null) File.WriteAllText(settingsFile, restored, encoding);
                    }
                    catch (Exception e) when (e is IOException || e is UnauthorizedAccessException) { }
                }
            }
        }

        private static System.Collections.Generic.HashSet<int> ProcessIds(string name)
        {
            var ids = new System.Collections.Generic.HashSet<int>();
            foreach (var p in Process.GetProcessesByName(name)) { ids.Add(p.Id); p.Dispose(); }
            return ids;
        }

        /// <summary>The ID of a process called <paramref name="name"/> that was not running before; 0 when none.</summary>
        private static int NewProcess(string name, System.Collections.Generic.HashSet<int> before)
        {
            int found = 0;
            foreach (var p in Process.GetProcessesByName(name))
            {
                if (found == 0 && !before.Contains(p.Id)) found = p.Id;
                p.Dispose();
            }
            return found;
        }

        private static void EndNew(string name, System.Collections.Generic.HashSet<int> before)
        {
            foreach (var p in Process.GetProcessesByName(name))
            {
                try { if (!before.Contains(p.Id)) p.Kill(); }
                catch (Exception e) when (e is InvalidOperationException || e is Win32Exception) { }
                finally { p.Dispose(); }
            }
        }

        private static int WaitForPid(string pidFile, int timeoutMs, out bool viaLauncher)
        {
            viaLauncher = false;
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
                        // "<pid>", or "<pid> launcher" when the DOOM Eternal Launcher started the game.
                        var parts = text.Split(' ');
                        if (int.TryParse(parts[0], NumberStyles.Integer, CultureInfo.InvariantCulture, out var pid) && pid > 0)
                        {
                            viaLauncher = parts.Length > 1 && parts[1] == "launcher";
                            return pid;
                        }
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

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);
    }
}
