using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;

namespace EternalVR.Launcher.Core.Launch
{
    /// <summary>What the copy of the launcher started inside the game's package runs: the game, its folder, its command line and its whole environment.</summary>
    public sealed class PackageStartPlan
    {
        public string ExePath { get; set; }
        public string WorkingDirectory { get; set; }
        public string CommandLine { get; set; }
        public IReadOnlyList<KeyValuePair<string, string>> Environment { get; set; } = new KeyValuePair<string, string>[0];

        /// <summary>Start the game through the DOOM Eternal Launcher (<see cref="PackageStart.BethesdaLauncher"/>), as the Xbox app does; else directly.</summary>
        public bool ViaLauncher { get; set; }
    }

    /// <summary>
    /// Starting the Game Pass or Microsoft Store game inside its package, as the Xbox app does. The Xbox app starts the
    /// package's DOOM Eternal Launcher, which starts the game as its child, so the game runs with the package's identity.
    /// A game started from outside runs without it; a Game Pass player's game then crashed a second after starting, before
    /// EternalVR loaded, whenever he had played flat from the Xbox app shortly before (2026-10-03).
    /// <para>
    /// The launcher writes the plan to a file and starts a copy of itself inside the package with Windows PowerShell's
    /// <c>Invoke-CommandInDesktopPackage</c> (a process started that way gets the package's identity, not the caller's
    /// environment). The copy (<see cref="HelperSwitch"/>) first starts the game through the DOOM Eternal Launcher, as the
    /// Xbox app does (<see cref="BethesdaLauncher"/>, its window skipped for that start), and reports its process ID; when
    /// that fails it starts the game suspended with the plan's environment, writes the
    /// game's process ID next to the plan, waits for the launcher to open the process and write the go file, and resumes it:
    /// the launcher holds the game's process from its first instruction, as with a direct start.
    /// </para>
    /// </summary>
    public static class PackageStart
    {
        public const string HelperSwitch = "--start-in-package";
        public const string PackageName = Game.GamePassInstall.PackageName;

        public const string PidSuffix = ".pid";
        public const string GoSuffix = ".go";

        /// <summary>How long the launcher waits for the copy to report the game's process ID (the DOOM Eternal Launcher's
        /// route and, when it fails, the direct one).</summary>
        public const int PidTimeoutMs = 75000;

        /// <summary>How long the copy waits for the DOOM Eternal Launcher to start the game before it starts it directly.</summary>
        public const int LauncherTimeoutMs = 40000;

        /// <summary>The DOOM Eternal Launcher's setting that skips its window and starts the game at once.</summary>
        public const string SkipLauncherTarget = "retail";

        /// <summary>
        /// The DOOM Eternal Launcher the Xbox app starts (Bethesda's, in the game folder's <c>launcher</c> folder). On the
        /// Store build it signs in the Xbox user before it starts the game as its child, with its own command line and
        /// environment passed on unchanged; a game started without it crashed for a Microsoft Store player about a second
        /// after starting, now and then, also inside the package (2026-10-03).
        /// </summary>
        public static string BethesdaLauncher(string gameExe) => Path.Combine(Path.GetDirectoryName(gameExe) ?? string.Empty, "launcher", "idTechLauncher.exe");

        /// <summary>The DOOM Eternal Launcher's settings file, in the game folder; its <c>launch_target</c> is <c>portal</c> (its
        /// window) or <c>retail</c> (straight to the game, as its own option does).</summary>
        public static string BethesdaSettings(string gameExe) => Path.Combine(Path.GetDirectoryName(gameExe) ?? string.Empty, "doom-launcher-settings.json");

        private static readonly Regex LaunchTargetPattern = new Regex(@"(""launch_target""\s*:\s*"")([a-z]*)("")", RegexOptions.CultureInvariant);

        /// <summary>The settings file's <c>launch_target</c>; null when it has none.</summary>
        public static string LaunchTarget(string settings)
        {
            var m = LaunchTargetPattern.Match(settings ?? string.Empty);
            return m.Success ? m.Groups[2].Value : null;
        }

        /// <summary>The settings file with <c>launch_target</c> set to <paramref name="target"/>, the rest unchanged; null when it has none.</summary>
        public static string WithLaunchTarget(string settings, string target)
        {
            if (LaunchTarget(settings) == null) return null;
            return LaunchTargetPattern.Replace(settings, m => m.Groups[1].Value + target + m.Groups[3].Value, 1);
        }

        /// <summary>How long the copy waits for the go file before it ends the suspended game.</summary>
        public const int GoTimeoutMs = 30000;

        /// <summary>
        /// The plan file: <c>exe</c>, <c>cwd</c>, <c>args</c> and one <c>env</c> line per variable (<c>NAME=VALUE</c>), each
        /// <c>key&lt;TAB&gt;value</c>. A variable whose name or value holds a line break or a tab is left out.
        /// </summary>
        public static string Write(PackageStartPlan plan)
        {
            var sb = new StringBuilder();
            void Line(string key, string value) => sb.Append(key).Append('\t').Append(value ?? string.Empty).Append('\n');
            Line("exe", plan.ExePath);
            Line("cwd", plan.WorkingDirectory);
            Line("args", plan.CommandLine);
            if (plan.ViaLauncher) Line("via", "launcher");
            foreach (var kv in plan.Environment ?? new KeyValuePair<string, string>[0])
            {
                if (string.IsNullOrEmpty(kv.Key) || kv.Value == null || Unsafe(kv.Key) || Unsafe(kv.Value) || kv.Key.IndexOf('=') > 0) continue;
                Line("env", kv.Key + "=" + kv.Value);
            }
            return sb.ToString();
        }

        /// <summary>The plan in a file's text; null when it names no game.</summary>
        public static PackageStartPlan Read(string text)
        {
            var plan = new PackageStartPlan();
            var env = new List<KeyValuePair<string, string>>();
            foreach (var raw in (text ?? string.Empty).Split('\n'))
            {
                var line = raw.TrimEnd('\r');
                int tab = line.IndexOf('\t');
                if (tab <= 0) continue;
                var key = line.Substring(0, tab);
                var value = line.Substring(tab + 1);
                switch (key)
                {
                    case "exe": plan.ExePath = value; break;
                    case "cwd": plan.WorkingDirectory = value; break;
                    case "args": plan.CommandLine = value; break;
                    case "via": plan.ViaLauncher = value == "launcher"; break;
                    case "env":
                        int eq = value.IndexOf('=', 1); // a name may start with '=' (=C:)
                        if (eq > 0) env.Add(new KeyValuePair<string, string>(value.Substring(0, eq), value.Substring(eq + 1)));
                        break;
                }
            }
            plan.Environment = env;
            return string.IsNullOrEmpty(plan.ExePath) ? null : plan;
        }

        /// <summary>
        /// The environment block CreateProcess takes (<c>NAME=VALUE\0</c>... <c>\0</c>), sorted by name without case as Windows
        /// wants it; names that differ only in case keep the first.
        /// </summary>
        public static string EnvironmentBlock(IEnumerable<KeyValuePair<string, string>> env)
        {
            var sb = new StringBuilder();
            foreach (var kv in (env ?? Enumerable.Empty<KeyValuePair<string, string>>())
                         .GroupBy(kv => kv.Key, StringComparer.OrdinalIgnoreCase).Select(g => g.First())
                         .OrderBy(kv => kv.Key, StringComparer.OrdinalIgnoreCase))
                sb.Append(kv.Key).Append('=').Append(kv.Value).Append('\0');
            if (sb.Length == 0) sb.Append('\0');
            return sb.Append('\0').ToString();
        }

        /// <summary>
        /// The PowerShell command that starts <paramref name="helperExe"/> <see cref="HelperSwitch"/> <paramref name="planFile"/>
        /// inside the package (its first application, the one the Xbox app starts), and prints <c>started</c>.
        /// </summary>
        public static string Command(string helperExe, string planFile)
        {
            string Quote(string s) => "'" + (s ?? string.Empty).Replace("'", "''") + "'";
            var args = HelperSwitch + " \"" + planFile + "\"";
            return "$ErrorActionPreference = 'Stop'; $ProgressPreference = 'SilentlyContinue'; "
                + "$p = Get-AppxPackage -Name " + Quote(PackageName) + " | Select-Object -First 1; "
                + "if (-not $p) { throw 'the package ' + " + Quote(PackageName) + " + ' is not installed for this user' }; "
                + "$app = @((Get-AppxPackageManifest $p).Package.Applications.Application)[0].Id; "
                + "Invoke-CommandInDesktopPackage -PackageFamilyName $p.PackageFamilyName -AppId $app -Command " + Quote(helperExe)
                + " -Args " + Quote(args) + "; "
                + "'started ' + $p.PackageFamilyName + '!' + $app";
        }

        private static bool Unsafe(string s) => s.IndexOf('\n') >= 0 || s.IndexOf('\r') >= 0 || s.IndexOf('\t') >= 0;
    }
}
