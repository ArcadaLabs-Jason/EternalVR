using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;

namespace EternalVR.Launcher.Core.Report
{
    /// <summary>
    /// Where a player reports a problem (Jason 2026-10-13): the public repository's GitHub issues and the bug report
    /// channel of the EternalVR Discord server. The window has a link to each next to Export report, and the dialog after a
    /// saved report offers the same two; both open in the default browser.
    /// </summary>
    public static class ReportLinks
    {
        public const string GitHubIssues = "https://github.com/ArcadaLabs-Jason/EternalVR/issues";
        public const string Discord = "https://discord.com/channels/747967102895390741/1554248586126954527";

        public const string GitHubText = "Report on GitHub";
        public const string DiscordText = "EternalVR on Discord";

        public const string GitHubTooltip =
            "Opens EternalVR's GitHub issues in your browser: open a new issue there and attach the report zip. Needs a GitHub account.";
        public const string DiscordTooltip =
            "Opens the EternalVR Discord server's bug report channel in your browser or the Discord app: post your report and "
            + "the zip there. Needs a Discord account, no GitHub account.";

        /// <summary>
        /// How to capture what the player sees before exporting (docs/release/CONTROLS.md, "In the game"): the mod saves each
        /// eye and the HUD, and the next report takes the newest captures. Said in Export report's tooltip and the saved dialog.
        /// </summary>
        public const string CaptureHint =
            "Bug in one eye, or different between your eyes? Take a capture where it happens: in the headset, hold the left Menu "
            + "button and pull a trigger. Then export again.";

        /// <summary>The same press on the controllers whose left Menu button is not the one to use (see docs/release/CONTROLS.md).</summary>
        public const string CaptureOtherControllers =
            "SteamVR with Quest/Rift controllers: both sticks pressed + a trigger" + "\n"
            + "Index: firm left trackpad press + a trigger · Steam Frame: View + a trigger";

        /// <summary>
        /// What the saved dialog says about captures: with none in the report, <see cref="CaptureHint"/>; with some, a quiet line
        /// that they are in, so the player knows the report shows both eyes.
        /// </summary>
        public static string CaptureLine(int captures) =>
            captures <= 0 ? CaptureHint
            : captures == 1 ? "1 capture from the headset is in this report, both eyes and the HUD."
            : captures + " captures from the headset are in this report, both eyes and the HUD each.";

        /// <summary>
        /// Opens <paramref name="url"/> with the program Windows has for it (the default browser, or the Discord app), through
        /// <paramref name="start"/> (<see cref="Process.Start(ProcessStartInfo)"/> with UseShellExecute unless a test gives
        /// another); false, with the reason given to <paramref name="logError"/>, when nothing could open it. Never throws for
        /// that: the caller tells the player.
        /// </summary>
        public static bool Open(string url, Action<string> logError, Func<ProcessStartInfo, Process> start = null)
        {
            try
            {
                var open = start ?? (Func<ProcessStartInfo, Process>)(psi => Process.Start(psi));
                var process = open(new ProcessStartInfo(url) { UseShellExecute = true });
                process?.Dispose();
                return true;
            }
            catch (Exception e) when (e is Win32Exception || e is FileNotFoundException || e is InvalidOperationException || e is PlatformNotSupportedException)
            {
                logError?.Invoke($"no browser opened {url}: {e.Message}");
                return false;
            }
        }
    }
}
