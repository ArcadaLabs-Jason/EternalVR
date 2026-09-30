using System;
using System.IO;
using System.Linq;
using System.Text;
using EternalVR.Launcher.Core.Report;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>The game's own files in a report: its crash reports and console log (Saved Games\id Software\DOOMEternal\base).</summary>
    public class GameReportTests
    {
        private const string Computer = "TESTBOX-7";

        /// <summary>
        /// <see cref="ReportTests.Setup"/> plus the game's base folder: two crash reports in the window (the oldest session in
        /// the report started 2026-09-25 10:00, a week before now is 2026-09-20 12:00), one before it, the console log, a
        /// memory dump and a crash report inside crash-dumps, and a config file.
        /// </summary>
        private static ReportInputs SetupWithGame(TempDir t)
        {
            var inputs = ReportTests.Setup(t);
            inputs.ComputerName = Computer;
            inputs.GameSavedGamesDirs = new[] { t.Combine("saved") };
            Crash(t, "00011", new DateTime(2026, 9, 26, 10, 30, 0));
            Crash(t, "00012", new DateTime(2026, 9, 26, 11, 0, 0));
            Crash(t, "00003", new DateTime(2026, 9, 1, 9, 0, 0));
            t.Write(@"saved\base\qconsole.log", $"log file 'qconsole.log' opened\n\tHost Name: {Computer}\n"
                + "idSignInManager::TriggerLocalUserSignInEvent - User 'SlayerTester' signed in - 1234567890\n");
            t.Write(@"saved\base\crash-dumps\Ghost_20260811-165453-brown-sapphire\x64_build_0.dmp", "MDMP");
            t.Write(@"saved\base\crash-dumps\Ghost_20260811-165453-brown-sapphire\x64_build_0.txt", "inside crash-dumps");
            t.Write(@"saved\base\crash-dumps\x64_build_1.dmp", "MDMP");
            t.Write($@"saved\base\crash-dumps\Crash.{Computer}.00099.html", "inside crash-dumps");
            t.Write(@"saved\base\DOOMEternalConfig.cfg", "secret config");
            return inputs;
        }

        private static string Crash(TempDir t, string number, DateTime written, string folder = @"saved\base")
        {
            var path = t.Write($@"{folder}\Crash.{Computer}.{number}.html",
                $"<PRE>\nExpCode:          0xC0000374 (Undefined Exception)\nUser:             Tester        \n"
                + $"File Path:        C:\\Users\\Tester\\Games\\DOOMEternalx64vk.exe\nHost {Computer} crash {number}\n</PRE>\n");
            File.SetLastWriteTime(path, written);
            return path;
        }

        [Fact]
        public void TheGamesCrashReportsAndConsoleLogAreInTheZipRedacted()
        {
            using (var t = new TempDir())
            {
                var report = ReportBuilder.Build(SetupWithGame(t));
                var entries = ReportTests.Unzip(report.Zip);
                var names = entries.Keys.ToList();
                int events = names.IndexOf("windows-events.txt");
                Assert.Equal(new[] { "game-crashes/crash-00012.html", "game-crashes/crash-00011.html", "game/qconsole.log" }, names.Skip(events + 1).Take(3));
                Assert.DoesNotContain(names, n => n.Contains("00003") || n.Contains("00099") || n.Contains(Computer) || n.EndsWith(".dmp") || n.EndsWith(".cfg"));
                foreach (var kv in entries)
                {
                    Assert.DoesNotContain(Computer, kv.Value, StringComparison.OrdinalIgnoreCase);
                    Assert.DoesNotContain("Tester", kv.Value);
                    Assert.DoesNotContain("1234567890", kv.Value);
                    Assert.DoesNotContain("MDMP", kv.Value);
                    Assert.DoesNotContain("inside crash-dumps", kv.Value);
                    Assert.DoesNotContain("secret config", kv.Value);
                }
                var crash = entries["game-crashes/crash-00012.html"];
                Assert.Contains("ExpCode:          0xC0000374", crash);
                Assert.Contains("User:             <user>", crash);
                Assert.Contains(@"File Path:        %USERPROFILE%\Games\DOOMEternalx64vk.exe", crash);
                Assert.Contains("Host <computer> crash 00012", crash);
                Assert.Contains("Host Name: <computer>", entries["game/qconsole.log"]);
                Assert.Contains("User '<player>' signed in - <playerid>", entries["game/qconsole.log"]);

                var contents = entries["report-contents.txt"];
                Assert.Contains("  game-crashes/crash-00012.html  ", contents);
                Assert.Contains("  game/qconsole.log  ", contents);
                Assert.Contains("Left out: 1 older game crash report(s) (only those since 2026-09-20 12:00 are taken, at most 3)", contents);
                Assert.Contains(@"Left out: 2 memory dump(s) of the game (crash-dumps\*.dmp): never included", contents);
                Assert.Contains("your computer name <computer>", contents);
                Assert.DoesNotContain("Not found", contents);
            }
        }

        [Fact]
        public void CrashReportsAreNewestFirstInTheWindowAndAtMostThree()
        {
            using (var t = new TempDir())
            {
                var now = new DateTime(2026, 9, 27, 12, 0, 0);
                var since = now.AddDays(-7);
                var newest = Crash(t, "00020", now.AddHours(-1), "a");
                var second = Crash(t, "00007", now.AddHours(-2), "b"); // a lower number, written later than the next
                var third = Crash(t, "00019", now.AddHours(-3), "a");
                Crash(t, "00018", since, "a"); // in the window, but a fourth
                Crash(t, "00001", since.AddSeconds(-1), "a");
                t.Write(@"a\Crash.html", "not a crash report name");
                t.Write(@"a\Crash.notes.html", "not a crash report name");
                Crash(t, "00050", now, @"a\crash-dumps");

                var dirs = new[] { t.Combine("a"), t.Combine("b"), t.Combine("missing") };
                var taken = ReportBuilder.GameCrashReports(dirs, since, 3, out var notTaken);
                Assert.Equal(new[] { newest, second, third }, taken);
                Assert.Equal(2, notTaken);

                Assert.Equal(4, ReportBuilder.GameCrashReports(dirs, since, 10, out notTaken).Count); // one written exactly at the start counts
                Assert.Equal(1, notTaken);
                Assert.Empty(ReportBuilder.GameCrashReports(new[] { t.Combine("missing") }, since, 3, out notTaken));
                Assert.Equal(0, notTaken);
                Assert.Empty(ReportBuilder.GameCrashReports(null, since, 3, out _));
            }
        }

        [Fact]
        public void TheCrashWindowIsTheOldestSessionInTheReportOrAWeek()
        {
            var now = new DateTime(2026, 9, 27, 12, 0, 0);
            var week = new DateTime(2026, 9, 20, 12, 0, 0);
            Assert.Equal(week, ReportBuilder.CrashWindowStart(new string[0], now));
            Assert.Equal(week, ReportBuilder.CrashWindowStart(null, now));
            // Newer sessions than a week: the week.
            Assert.Equal(week, ReportBuilder.CrashWindowStart(new[] { "20260926-100000-2", "20260926-100000", "20260925-100000" }, now));
            // An older session in the report reaches back further.
            Assert.Equal(new DateTime(2026, 9, 10, 8, 0, 0), ReportBuilder.CrashWindowStart(new[] { "20260926-100000", "20260910-080000-2" }, now));
            // Only the sessions in the report count (the newest 3).
            Assert.Equal(new DateTime(2026, 9, 15, 9, 30, 0),
                ReportBuilder.CrashWindowStart(new[] { "20260927-100000", "20260926-100000", "20260915-093000", "20260801-100000" }, now));
        }

        [Theory]
        [InlineData("Crash.DESKTOP-7Q2XK9M.00014.html", "game-crashes/crash-00014.html")]
        [InlineData("crash.DESKTOP-AB12CD.7.HTML", "game-crashes/crash-7.html")]
        [InlineData("Crash.html", null)]
        [InlineData("Crash.PC.html", null)]
        [InlineData("Crash.PC.00001.html.bak", null)]
        [InlineData("x64_build_0.dmp", null)]
        public void TheCrashReportsZipNameDropsTheComputerName(string fileName, string expected) =>
            Assert.Equal(expected, ReportBuilder.GameCrashZipPath(fileName));

        [Fact]
        public void NoGameFolderOrNoCrashReportIsNotFoundNotAnError()
        {
            using (var t = new TempDir())
            {
                var inputs = ReportTests.Setup(t);
                foreach (var dirs in new[] { null, new string[0], new[] { t.Combine("nothing"), "", null } })
                {
                    inputs.GameSavedGamesDirs = dirs;
                    var report = ReportBuilder.Build(inputs);
                    Assert.Contains("Not found: game-crashes/crash-*.html, game/qconsole.log\n", report.Files[0].Text);
                    Assert.DoesNotContain(report.Files, f => f.ZipPath.StartsWith("game"));
                }

                t.Write(@"saved\base\qconsole.log", "console\n");
                inputs.GameSavedGamesDirs = new[] { t.Combine("saved"), t.Combine("saved") };
                Assert.Equal(new[] { t.Combine("saved", "base") }, ReportBuilder.GameBaseFolders(inputs.GameSavedGamesDirs));
                var withLog = ReportBuilder.Build(inputs);
                Assert.Contains("Not found: game-crashes/crash-*.html\n", withLog.Files[0].Text);
                Assert.Single(withLog.Files, f => f.ZipPath == "game/qconsole.log");
            }
        }

        [Fact]
        public void TheGamesFilesCountTowardTheTextCap()
        {
            using (var t = new TempDir())
            {
                var inputs = SetupWithGame(t);
                t.Write(@"data\launcher.ini", new string('a', (int)ReportManifest.TotalCapBytes - 4096));
                t.Write(@"saved\base\qconsole.log", new string('q', 8191) + "\n");
                var report = ReportBuilder.Build(inputs);
                Assert.Contains(report.Files, f => f.ZipPath == "game-crashes/crash-00012.html");
                Assert.DoesNotContain(report.Files, f => f.ZipPath == "game/qconsole.log");
                Assert.Contains(report.Dropped, d => d.StartsWith("game/qconsole.log (8 KB): the report would pass 20.0 MB"));
                Assert.True(report.UncompressedBytes <= ReportManifest.TotalCapBytes + 4096);
            }
        }

        [Fact]
        public void ALongConsoleLogKeepsItsHeadAndTail()
        {
            using (var t = new TempDir())
            {
                var inputs = SetupWithGame(t);
                var sb = new StringBuilder("first line\n");
                var line = new string('x', 99) + "\n";
                while (sb.Length < 5 * 1024 * 1024) sb.Append(line);
                sb.Append("last line\n");
                t.Write(@"saved\base\qconsole.log", sb.ToString());
                var log = ReportBuilder.Build(inputs).Files.Single(f => f.ZipPath == "game/qconsole.log");
                Assert.True(log.Truncated);
                Assert.True(log.Bytes <= 4 * 1024 * 1024 + 200);
                Assert.StartsWith("first line\n", log.Text);
                Assert.EndsWith("last line\n", log.Text);
            }
        }

        [Fact]
        public void TheWindowsEventsFileIsRedactedToo()
        {
            using (var t = new TempDir())
            {
                var inputs = SetupWithGame(t);
                inputs.WindowsEvents = new[]
                {
                    new WindowsEventLogRead(WindowsEvents.ApplicationLog, new[]
                    {
                        new WindowsEvent(new DateTime(2026, 9, 26, 11, 0, 5), WindowsEvents.ApplicationLog, "Windows Error Reporting", 1001, 4,
                            $"P1: DOOMEternalx64vk.exe\r\nAttached files:\r\n\\\\?\\C:\\Users\\Tester\\AppData\\Local\\Temp\\WER.xml\r\nComputer {Computer}"),
                    }, null),
                    new WindowsEventLogRead(WindowsEvents.SystemLog, new WindowsEvent[0], "UnauthorizedAccessException: Attempted to perform an unauthorized operation."),
                };
                var text = ReportTests.Unzip(ReportBuilder.Build(inputs).Zip)["windows-events.txt"];
                Assert.Contains("2026-09-26 11:00:05  Application  Windows Error Reporting  1001  Information\n    P1: DOOMEternalx64vk.exe\n", text);
                Assert.Contains(@"    \\?\%USERPROFILE%\AppData\Local\Temp\WER.xml", text);
                Assert.Contains("    Computer <computer>\n", text);
                Assert.Contains("System: none\n  could not be read: UnauthorizedAccessException", text);
            }
        }
    }
}
