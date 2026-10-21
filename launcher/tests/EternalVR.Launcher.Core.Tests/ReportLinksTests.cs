using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using EternalVR.Launcher.Core.Report;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>
    /// Where the launcher sends a player to report a problem (<see cref="ReportLinks"/>): the two links beside Export
    /// report and in the dialog after a saved report, how to capture the headset's view first, and the brands' marks.
    /// </summary>
    public class ReportLinksTests
    {
        [Fact]
        public void TheLinksAreThePublicRepositorysIssuesAndTheBugReportChannel()
        {
            Assert.Equal("https://github.com/ArcadaLabs-Jason/EternalVR/issues", ReportLinks.GitHubIssues);
            Assert.Equal("https://discord.com/channels/747967102895390741/1554248586126954527", ReportLinks.Discord);
            foreach (var url in new[] { ReportLinks.GitHubIssues, ReportLinks.Discord })
            {
                var uri = new Uri(url);
                Assert.Equal(Uri.UriSchemeHttps, uri.Scheme);
                Assert.True(uri.IsAbsoluteUri);
            }
            // Never the private development repository.
            Assert.DoesNotContain("-dev", ReportLinks.GitHubIssues);
        }

        [Fact]
        public void TheirWordsSayWhatEachNeeds()
        {
            Assert.Equal("Report on GitHub", ReportLinks.GitHubText);
            Assert.Equal("EternalVR on Discord", ReportLinks.DiscordText);
            Assert.Contains("GitHub account", ReportLinks.GitHubTooltip);
            // Discord needs no GitHub account: the tooltip says so.
            Assert.Contains("no GitHub account", ReportLinks.DiscordTooltip);
            UiText.AssertNoAdvice(ReportLinks.GitHubTooltip, "GitHub tooltip");
            UiText.AssertNoAdvice(ReportLinks.DiscordTooltip, "Discord tooltip");
        }

        [Fact]
        public void TheCaptureHintNamesEachControllersPressAndTheReport()
        {
            var hint = ReportLinks.CaptureHint;
            Assert.Contains("hold the left Menu button and pull a trigger", hint);
            Assert.EndsWith("Then export again.", hint);
            Assert.DoesNotContain("¼", hint + ReportLinks.CaptureOtherControllers);
            Assert.DoesNotContain("Touch", ReportLinks.CaptureOtherControllers);
            // The SteamVR, Index and Steam Frame presses, as the player docs give them.
            Assert.Equal("SteamVR with Quest/Rift controllers: both sticks pressed + a trigger\n"
                + "Index: firm left trackpad press + a trigger · Steam Frame: View + a trigger", ReportLinks.CaptureOtherControllers);
            var controls = File.ReadAllText(RepoFile("docs", "release", "CONTROLS.md"));
            Assert.Contains("| Hold left Menu + pull a trigger | Save a screenshot of each eye", controls);
            Assert.Contains("| Hold both sticks pressed, then pull a trigger (under SteamVR, Touch controllers only) | The same screenshot", controls);
            Assert.Contains("Pull the trigger after ¼ s", controls);
            Assert.Contains("Index: the firm left trackpad press; Steam Frame: View", controls);
        }

        [Fact]
        public void TheSavedDialogSaysHowManyCapturesAreInOrOtherwiseHowToTakeOne()
        {
            Assert.Equal(ReportLinks.CaptureHint, ReportLinks.CaptureLine(0));
            Assert.Equal(ReportLinks.CaptureHint, ReportLinks.CaptureLine(-1));
            Assert.Contains("different between your eyes", ReportLinks.CaptureHint);
            Assert.Equal("1 capture from the headset is in this report, both eyes and the HUD.", ReportLinks.CaptureLine(1));
            Assert.StartsWith("3 captures from the headset are in this report", ReportLinks.CaptureLine(3));
            foreach (var n in new[] { 0, 1, 3 })
            {
                var line = ReportLinks.CaptureLine(n);
                Assert.DoesNotContain("we ", line + " ");
                Assert.DoesNotContain("stereo", line);
            }
        }

        [Fact]
        public void ALinkNothingOpensIsLoggedAndDoesNotThrow()
        {
            var logged = new List<string>();
            ProcessStartInfo asked = null;
            Assert.False(ReportLinks.Open(ReportLinks.Discord, logged.Add, psi =>
            {
                asked = psi;
                throw new Win32Exception(1155, "No application is associated with the specified file for this operation");
            }));
            // Opened by the shell (the default browser), and the log names the link.
            Assert.True(asked.UseShellExecute);
            Assert.Equal(ReportLinks.Discord, asked.FileName);
            Assert.Contains(ReportLinks.Discord, Assert.Single(logged));
            Assert.False(ReportLinks.Open("not a link", logged.Add, psi => throw new InvalidOperationException("no file name")));
            Assert.Equal(2, logged.Count);
            // Opened (the shell often hands back no process): true, nothing logged.
            Assert.True(ReportLinks.Open(ReportLinks.GitHubIssues, logged.Add, psi => null));
            Assert.Equal(2, logged.Count);
        }

        /// <summary>The marks beside the links are the brands' own files, unaltered (branding/README.md gives their source).</summary>
        [Theory]
        [InlineData("discord-symbol-blurple.png", "f6c1a66250d366d213789faefab59a9ec6ac6334e6c4be0af254680c148810dc")]
        [InlineData("github-invertocat-black.png", "2a2f5cbcc74c7fa83c40127dd8b0e42c23f1157131aebe28492aa1ac27bbdc6d")]
        public void TheMarksAreTheBrandsOwnFiles(string file, string sha256)
        {
            var path = RepoFile("branding", file);
            using (var sha = SHA256.Create())
            using (var stream = File.OpenRead(path))
                Assert.Equal(sha256, string.Concat(sha.ComputeHash(stream).Select(b => b.ToString("x2"))));
            Assert.Contains(file, File.ReadAllText(RepoFile("branding", "README.md")));
            Assert.Contains(file, File.ReadAllText(RepoFile("THIRD_PARTY_NOTICES.md")));
        }

        private static string RepoFile(params string[] parts)
        {
            for (var dir = new DirectoryInfo(AppContext.BaseDirectory); dir != null; dir = dir.Parent)
            {
                var path = Path.Combine(new[] { dir.FullName }.Concat(parts).ToArray());
                if (File.Exists(path)) return path;
            }
            throw new FileNotFoundException("not found above the test folder: " + string.Join("/", parts));
        }
    }
}
