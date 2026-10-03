using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.Text;
using System.Threading;

namespace EternalVR.Launcher.Core.Launch
{
    /// <summary>
    /// The runtime probe in a short-lived copy of the launcher (<c>--probe-headset &lt;loader&gt;</c>), so the runtime's DLLs
    /// (Virtual Desktop's loads a closed LibOVRRT, SteamVR its client) and any threads they leave behind go away with that
    /// process instead of staying in the launcher. The copy runs <see cref="OpenXrProbe.Run"/> with the game's OpenXR
    /// environment and prints its answer as <c>probe.&lt;key&gt;=&lt;value&gt;</c> lines; the launcher reads them, and kills
    /// a copy that has not answered in time.
    /// </summary>
    public static class ProbeChild
    {
        /// <summary>The launcher's switch for the copy; its value is the OpenXR loader's full path.</summary>
        public const string Switch = "--probe-headset";

        /// <summary>After the copy's own probe deadline: time for it to print its answer and exit before it is killed.</summary>
        public const int GraceMs = 5000;

        /// <summary>For the rest of the copy's output after it exits, and for its exit after it answered.</summary>
        private const int DrainMs = 2000;

        private const string Prefix = "probe.";
        private const string EndKey = "end";

        /// <summary>The copy's answer, one <c>probe.key=value</c> line per fact, ending with <c>probe.end=1</c>.</summary>
        public static string Serialize(OpenXrProbeResult r)
        {
            // A line break first: the answer must start a line even after output that did not end one.
            var sb = new StringBuilder("\n");
            void Line(string key, string value)
            {
                if (value != null) sb.Append(Prefix).Append(key).Append('=').Append(OneLine(value)).Append('\n');
            }

            Line("ok", r.Ok ? "1" : "0");
            Line("runtime", r.RuntimeName);
            Line("system", r.SystemName);
            Line("error", r.Error);
            Line("unavailable", r.HeadsetUnavailable ? "1" : "0");
            if (r.Ok)
            {
                Line("recommended", Size(r.Limits.Recommended));
                Line("max_image", Size(r.Limits.MaxImageRect));
                Line("max_swapchain", Size(r.Limits.MaxSwapchain));
            }
            Line(EndKey, "1");
            return sb.ToString();
        }

        /// <summary>
        /// The copy's printed answer back as a probe result; an answer that is missing, cut short or unreadable is a failed probe
        /// that says so. Lines that do not start with <c>probe.</c> (a runtime's own output) are skipped.
        /// </summary>
        public static OpenXrProbeResult Parse(string output)
        {
            var values = new Dictionary<string, string>(StringComparer.Ordinal);
            foreach (var raw in (output ?? string.Empty).Split('\n'))
            {
                var line = raw.TrimEnd('\r');
                if (!line.StartsWith(Prefix, StringComparison.Ordinal)) continue;
                int eq = line.IndexOf('=');
                if (eq < 0) continue;
                values[line.Substring(Prefix.Length, eq - Prefix.Length)] = line.Substring(eq + 1);
            }
            if (!values.ContainsKey(EndKey)) return OpenXrProbeResult.Failed("the probe gave no answer");

            string Get(string key) => values.TryGetValue(key, out var v) ? v : null;
            if (Get("ok") != "1")
                return OpenXrProbeResult.Failed(Get("error") ?? "the probe failed without a reason", Get("unavailable") == "1");

            if (!TryReadSize(Get("recommended"), out var recommended) || !TryReadSize(Get("max_image"), out var maxImage)
                || !TryReadSize(Get("max_swapchain"), out var maxSwapchain))
                return OpenXrProbeResult.Failed("the probe's answer is unreadable");
            return new OpenXrProbeResult
            {
                RuntimeName = Get("runtime"),
                SystemName = Get("system"),
                Limits = new ViewLimits { Recommended = recommended, MaxImageRect = maxImage, MaxSwapchain = maxSwapchain },
            };
        }

        /// <summary>
        /// Starts <paramref name="exe"/> with <paramref name="arguments"/> (no window, output read), with <paramref name="environment"/>
        /// added to this process's environment (a null value removes the variable), and returns its answer. A copy that has not
        /// answered after <paramref name="timeoutMs"/> plus <see cref="GraceMs"/> is killed; one that answered but does not exit
        /// is killed too. Never throws.
        /// </summary>
        public static OpenXrProbeResult Run(string exe, string arguments, IEnumerable<KeyValuePair<string, string>> environment,
                                            int timeoutMs = OpenXrProbe.DefaultTimeoutMs, int graceMs = GraceMs)
        {
            var output = new StringBuilder();
            var answered = new ManualResetEventSlim(false);
            Process p;
            try
            {
                var psi = new ProcessStartInfo(exe, arguments ?? string.Empty)
                {
                    UseShellExecute = false,
                    CreateNoWindow = true,
                    RedirectStandardOutput = true,
                    RedirectStandardError = true,
                    RedirectStandardInput = true,
                    StandardOutputEncoding = Encoding.UTF8,
                };
                ChildEnvironment.Apply(psi, environment);
                p = new Process { StartInfo = psi };
                p.OutputDataReceived += (s, e) =>
                {
                    if (e.Data == null) return;
                    lock (output) output.Append(e.Data).Append('\n');
                    if (e.Data.StartsWith(Prefix + EndKey + "=", StringComparison.Ordinal)) answered.Set();
                };
                // Read and dropped, so a runtime that writes a lot to stderr cannot fill the pipe and stall the copy.
                p.ErrorDataReceived += (s, e) => { };
                if (!p.Start()) return OpenXrProbeResult.Failed("the probe could not be started");
            }
            // ArgumentException: an environment .NET cannot copy (ChildEnvironment.Open already copes with names that differ only in case).
            catch (Exception e) when (e is InvalidOperationException || e is System.ComponentModel.Win32Exception
                                      || e is System.IO.IOException || e is PlatformNotSupportedException || e is ArgumentException)
            {
                return OpenXrProbeResult.Failed("the probe could not be started: " + e.Message);
            }

            // The event is not disposed: a late line from a process still holding the pipe may set it after this returns.
            using (p)
            {
                try { p.StandardInput.Close(); }
                catch (Exception e) when (e is InvalidOperationException || e is System.IO.IOException) { }
                p.BeginOutputReadLine();
                p.BeginErrorReadLine();

                // Wait for the answer, or for the copy to exit without one. Never for the end of its output: a process the
                // runtime started from the copy may hold the pipe open.
                int deadline = timeoutMs + graceMs;
                var watch = Stopwatch.StartNew();
                bool exited = false;
                while (!answered.Wait(100) && watch.ElapsedMilliseconds < deadline)
                {
                    if (!HasExited(p)) continue;
                    exited = true;
                    answered.Wait(DrainMs);
                    break;
                }
                bool hasAnswer = answered.IsSet;
                // The answer is printed last; a copy still running after it (a runtime thread holding the exit) gets a moment.
                if (!exited && !(hasAnswer && p.WaitForExit(DrainMs))) Kill(p);
                if (!hasAnswer && !exited)
                    return OpenXrProbeResult.Failed($"the runtime did not answer within {timeoutMs / 1000.0:0.#} s");
                if (!hasAnswer)
                    return OpenXrProbeResult.Failed($"the probe stopped without an answer (exit code {SafeExitCode(p)})");

                string text;
                lock (output) text = output.ToString();
                return Parse(text);
            }
        }

        private static bool HasExited(Process p)
        {
            try { return p.HasExited; }
            catch (Exception e) when (e is InvalidOperationException || e is System.ComponentModel.Win32Exception) { return true; }
        }

        private static void Kill(Process p)
        {
            try
            {
                if (!p.HasExited) p.Kill();
                p.WaitForExit(2000);
            }
            catch (Exception e) when (e is InvalidOperationException || e is System.ComponentModel.Win32Exception
                                      || e is NotSupportedException)
            {
            }
        }

        private static string SafeExitCode(Process p)
        {
            try { return p.ExitCode.ToString(CultureInfo.InvariantCulture); }
            catch (InvalidOperationException) { return "unknown"; }
        }

        private static string OneLine(string s) => s.Replace('\r', ' ').Replace('\n', ' ');

        private static string Size(Extent e) =>
            e.Width.ToString(CultureInfo.InvariantCulture) + "x" + e.Height.ToString(CultureInfo.InvariantCulture);

        private static bool TryReadSize(string s, out Extent e)
        {
            e = default(Extent);
            if (s == null) return false;
            int x = s.IndexOf('x');
            if (x <= 0) return false;
            if (!uint.TryParse(s.Substring(0, x), NumberStyles.None, CultureInfo.InvariantCulture, out uint w)
                || !uint.TryParse(s.Substring(x + 1), NumberStyles.None, CultureInfo.InvariantCulture, out uint h))
                return false;
            e = new Extent(w, h);
            return true;
        }
    }
}
