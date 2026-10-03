using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Text;
using EternalVR.Launcher.Core.Report;

namespace EternalVR.Launcher.Platform
{
    /// <summary>
    /// Asks Windows for the Store packages of a report (<see cref="StorePackages"/>) through Windows PowerShell's
    /// <c>Get-AppxPackage</c>, which reads only. A query that fails or takes too long is reported, never an error.
    /// </summary>
    public static class StorePackageQuery
    {
        private const int TimeoutMs = 20000;

        public static IReadOnlyList<KeyValuePair<string, string>> Read()
        {
            var exe = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), @"WindowsPowerShell\v1.0\powershell.exe");
            var psi = new ProcessStartInfo(exe, "-NoProfile -NonInteractive -EncodedCommand " + Convert.ToBase64String(Encoding.Unicode.GetBytes(StorePackages.Query())))
            {
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
            };
            try
            {
                using (var p = Process.Start(psi))
                {
                    if (p == null) return StorePackages.Describe(null, "PowerShell did not start");
                    var output = p.StandardOutput.ReadToEndAsync();
                    p.StandardError.ReadToEndAsync();
                    if (!p.WaitForExit(TimeoutMs))
                    {
                        try { p.Kill(); } catch (InvalidOperationException) { } catch (Win32Exception) { }
                        return StorePackages.Describe(null, "no answer in " + TimeoutMs / 1000 + " s");
                    }
                    return StorePackages.Describe(output.Result, null);
                }
            }
            catch (Exception e) when (e is Win32Exception || e is InvalidOperationException || e is IOException)
            {
                return StorePackages.Describe(null, e.Message);
            }
        }
    }
}
