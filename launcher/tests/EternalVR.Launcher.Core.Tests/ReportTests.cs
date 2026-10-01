using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text;
using EternalVR.Launcher.Core.Preflight;
using EternalVR.Launcher.Core.Report;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    public class ReportTests
    {
        private const string Profile = @"C:\Users\Tester";
        private const string Account = "12345678";

        /// <summary>A data folder with six sessions, a dump, and personal data in every file.</summary>
        internal static ReportInputs Setup(TempDir t)
        {
            t.Write(@"data\logs\launcher.log",
                $"2026-09-26 02:14:00 INFO  settings snapshot: {Profile}\\AppData\\Local\\EternalVR\\snapshots\\20260926-021400\n"
                + $"2026-09-26 05:19:58 INFO  restore: unchanged steam-{Account}/PROFILE/profile.bin\n");
            t.Write(@"data\launcher.ini", "schema_version = 2\ngame_dir = C:\\Users\\Tester\\Games\\DOOMEternal\n");
            t.Write(@"data\snapshots\20260926-021400\DOOMEternalConfig.cfg", "secret config");
            t.Write(@"data\save-backups\20260926-021400\profile.bin", "save");
            foreach (var s in new[] { "20260921-100000", "20260922-100000", "20260924-100000", "20260925-100000", "20260926-100000", "20260926-100000-2" })
            {
                t.Write($@"data\logs\{s}\LAYER_LOADED", $"log {Profile}\\AppData\\Local\\EternalVR\\logs\\{s}\\eternalvr.log\n");
                t.Write($@"data\logs\{s}\eternalvr-{s}-100.log", "[0.000] vulkan-1.dll version 1.4.357.0, user Tester\n");
                t.Write($@"data\logs\{s}\eternalvr-frames-100.csv", "frame,ms\n1,11.1\n");
            }
            t.Write(@"data\logs\20260926-100000\crash.dmp", "MDMP");
            t.Write(@"data\logs\notes\eternalvr-x.log", "not a session folder");
            t.Write(@"program\BUILD-INFO.txt", "EternalVR alpha 0.1.0\n");
            t.Write(@"layer\VK_LAYER_ETERNALVR.json", "{\"layer\": {\"name\": \"VK_LAYER_ETERNALVR\"}}");
            return new ReportInputs
            {
                DataRoot = t.Combine("data"),
                ProgramDir = t.Combine("program"),
                LayerDir = t.Combine("layer"),
                System = new[]
                {
                    new KeyValuePair<string, string>("launcher version", "0.1.0+abc"),
                    new KeyValuePair<string, string>("hardware-accelerated GPU scheduling", "on (HwSchMode 2)"),
                    new KeyValuePair<string, string>("data folder", Profile + @"\AppData\Local\EternalVR"),
                    new KeyValuePair<string, string>("gpu", null),
                },
                Preflight = new[] { new Check("steam", Severity.Pass, "Steam is running and logged in") },
                UserProfile = Profile,
                UserName = "Tester",
                SteamAccountIds = new string[0],
                Now = new DateTime(2026, 9, 27, 12, 0, 0),
            };
        }

        internal static Dictionary<string, string> Unzip(byte[] zip)
        {
            using (var a = new ZipArchive(new MemoryStream(zip), ZipArchiveMode.Read))
                return a.Entries.ToDictionary(e => e.FullName, e => { using (var r = new StreamReader(e.Open())) return r.ReadToEnd(); });
        }

        [Fact]
        public void ZipMatchesTheManifest()
        {
            using (var t = new TempDir())
            {
                var report = ReportBuilder.Build(Setup(t));
                var entries = Unzip(report.Zip);
                var expected = new[]
                {
                    "report-contents.txt", "system.txt", "preflight.txt", "launcher.log", "launcher.ini", "BUILD-INFO.txt", "layer/VK_LAYER_ETERNALVR.json",
                    "windows-events.txt", "sessions/20260926-100000-2/LAYER_LOADED", "sessions/20260926-100000/LAYER_LOADED", "sessions/20260925-100000/LAYER_LOADED",
                    "sessions/20260924-100000/LAYER_LOADED", "sessions/20260922-100000/LAYER_LOADED",
                    "sessions/20260926-100000-2/eternalvr-20260926-100000-2-100.log", "sessions/20260926-100000/eternalvr-20260926-100000-100.log",
                    "sessions/20260925-100000/eternalvr-20260925-100000-100.log", "sessions/20260924-100000/eternalvr-20260924-100000-100.log",
                    "sessions/20260922-100000/eternalvr-20260922-100000-100.log",
                    "sessions/20260926-100000-2/eternalvr-frames-100.csv",
                };
                Assert.Equal(expected, entries.Keys.ToArray());
                Assert.Equal(expected, report.Files.Select(f => f.ZipPath).ToArray());
                Assert.Contains("hardware-accelerated GPU scheduling: on (HwSchMode 2)", entries["system.txt"]);
                Assert.Contains("gpu: unknown", entries["system.txt"]);
                Assert.Contains("[PASS] steam: Steam is running and logged in", entries["preflight.txt"]);
                Assert.Contains(report.Dropped, d => d.Contains("1 older session folder(s) (only the newest 5 are taken)"));
                Assert.DoesNotContain(entries.Keys, k => k.Contains("20260921"));
                Assert.Contains(report.Dropped, d => d.Contains("1 memory dump"));
                Assert.Contains("Left out: 1 memory dump(s)", entries["report-contents.txt"]);
                Assert.Contains("Never included", entries["report-contents.txt"]);
            }
        }

        [Fact]
        public void NothingPersonalOrPrivateIsInTheZip()
        {
            using (var t = new TempDir())
            {
                var entries = Unzip(ReportBuilder.Build(Setup(t)).Zip);
                foreach (var kv in entries)
                {
                    Assert.DoesNotContain("Tester", kv.Value);
                    Assert.DoesNotContain(Account, kv.Value);
                    Assert.DoesNotContain("secret config", kv.Value);
                    Assert.DoesNotContain("MDMP", kv.Value);
                }
                Assert.DoesNotContain(entries.Keys, k => k.EndsWith(".dmp") || k.Contains("snapshots") || k.Contains("save-backups") || k.EndsWith(".cfg"));
                Assert.Contains(@"%USERPROFILE%\AppData\Local\EternalVR\snapshots", entries["launcher.log"]);
                Assert.Contains("steam-<steamid>/PROFILE", entries["launcher.log"]);
                Assert.Contains(@"game_dir = %USERPROFILE%\Games", entries["launcher.ini"]);
                Assert.Contains("user <user>", entries["sessions/20260925-100000/eternalvr-20260925-100000-100.log"]);
                Assert.Contains("1.4.357.0", entries["sessions/20260925-100000/eternalvr-20260925-100000-100.log"]);
            }
        }

        [Fact]
        public void AnAccountIdFoundInOneFileIsRemovedFromAll()
        {
            using (var t = new TempDir())
            {
                var inputs = Setup(t);
                t.Write(@"data\logs\20260926-100000-2\eternalvr-20260926-100000-2-100.log", $"steam user {Account} signed in\n");
                var entries = Unzip(ReportBuilder.Build(inputs).Zip);
                Assert.Contains("steam user <steamid> signed in", entries["sessions/20260926-100000-2/eternalvr-20260926-100000-2-100.log"]);
            }
        }

        [Fact]
        public void MissingFilesAreListedNotFatal()
        {
            using (var t = new TempDir())
            {
                var inputs = new ReportInputs { DataRoot = t.Combine("nothing"), ProgramDir = t.Combine("p"), LayerDir = null, Now = new DateTime(2026, 1, 2) };
                var report = ReportBuilder.Build(inputs);
                Assert.Equal(new[] { "report-contents.txt", "system.txt", "preflight.txt", "windows-events.txt" }, report.Files.Select(f => f.ZipPath).ToArray());
                Assert.Contains("Not found: launcher.log, launcher.ini, BUILD-INFO.txt, layer/VK_LAYER_ETERNALVR.json, game-crashes/crash-*.html, game/qconsole.log, game/DOOMEternalConfig.cfg, game/DOOMEternalConfig.local\n", report.Files[0].Text);
                Assert.Contains("no checks were run", report.Files[2].Text);
                Assert.Contains("The event logs were not read.", report.Files[3].Text);
            }
        }

        [Fact]
        public void LongLogsKeepTheirHeadAndTail()
        {
            using (var t = new TempDir())
            {
                var inputs = Setup(t);
                var sb = new StringBuilder("first line\n");
                var line = new string('x', 99) + "\n";
                while (sb.Length < 5 * 1024 * 1024) sb.Append(line);
                sb.Append("last line\n");
                t.Write(@"data\logs\20260926-100000-2\eternalvr-20260926-100000-2-100.log", sb.ToString());
                var report = ReportBuilder.Build(inputs);
                var log = report.Files.Single(f => f.ZipPath.EndsWith("-2-100.log"));
                Assert.True(log.Truncated);
                Assert.True(log.Bytes <= 4 * 1024 * 1024 + 200);
                Assert.StartsWith("first line\n", log.Text);
                Assert.EndsWith("last line\n", log.Text);
                Assert.Contains("bytes left out of the report here ...]\nxxx", log.Text);
                Assert.DoesNotContain(log.Text.Split('\n'), l => l.Length != 0 && l.Length != 99 && !l.Contains("line") && !l.StartsWith("[..."));
                Assert.Contains("(shortened from 5.0 MB)", report.Files[0].Text);
                Assert.Contains("shortened", report.Describe());
            }
        }

        [Fact]
        public void TailOnlySliceStartsAtAWholeLine()
        {
            using (var t = new TempDir())
            {
                var path = t.Write("a.log", "aaaa\nbbbb\ncccc\n");
                Assert.Equal("[... 10 bytes left out of the report here ...]\ncccc\n", ReportBuilder.ReadSlice(path, 0, 7, out var length, out var truncated));
                Assert.Equal(15, length);
                Assert.True(truncated);
                Assert.Equal("aaaa\nbbbb\ncccc\n", ReportBuilder.ReadSlice(path, 0, 0, out _, out truncated));
                Assert.False(truncated);
                Assert.Equal("aaaa\nbbbb\ncccc\n", ReportBuilder.ReadSlice(path, 10, 10, out _, out truncated));
                Assert.False(truncated);
            }
        }

        [Fact]
        public void AFileThatWouldPassTheCapIsDroppedAndSaid()
        {
            using (var t = new TempDir())
            {
                var inputs = Setup(t);
                t.Write(@"data\launcher.ini", new string('a', (int)ReportManifest.TotalCapBytes + 10));
                var report = ReportBuilder.Build(inputs);
                Assert.DoesNotContain(report.Files, f => f.ZipPath == "launcher.ini");
                Assert.Contains(report.Dropped, d => d.StartsWith("launcher.ini (20.0 MB): the report would pass 20.0 MB"));
                Assert.Contains(report.Files, f => f.ZipPath == "BUILD-INFO.txt");
                Assert.True(report.UncompressedBytes <= ReportManifest.TotalCapBytes + 4096);
            }
        }

        [Fact]
        public void SessionsAreNewestFirst()
        {
            using (var t = new TempDir())
            {
                foreach (var s in new[] { "20260926-100000", "20260926-100000-2", "20260926-100000-10", "20251231-235959", "latest", "20260926" })
                    Directory.CreateDirectory(t.Combine("logs", s));
                Assert.Equal(new[] { "20260926-100000-10", "20260926-100000-2", "20260926-100000", "20251231-235959" }, ReportBuilder.NewestSessions(t.Combine("logs"), 10));
                Assert.Empty(ReportBuilder.NewestSessions(t.Combine("none"), 3));
            }
        }

        [Fact]
        public void ManifestIsTextOnlyAndSmall()
        {
            Assert.Equal("EternalVR-report-2026-09-27.zip", ReportBuilder.DefaultFileName(new DateTime(2026, 9, 27, 23, 59, 0)));
            foreach (var item in ReportManifest.Items)
            {
                Assert.DoesNotMatch(@"\.(dmp|bin|exe|dll)$", item.Pattern);
                Assert.DoesNotContain("structured", item.Pattern, StringComparison.OrdinalIgnoreCase); // the game's structured.log holds account IDs
                if (item.Source == ReportSource.SessionFolder) Assert.InRange(item.Sessions, 1, ReportManifest.SessionsKept);
            }
            long Worst(IEnumerable<ReportItem> items) => items.Sum(i => (i.HeadBytes + i.TailBytes) * Math.Max(1, i.Sessions));
            // With every file at its longest, the cap leaves out the older sessions' logs (the newest sessions' come first), never
            // the launcher's log, the game's files, the frame table or the newest two sessions' logs.
            var order = ReportManifest.Items.ToList();
            Assert.True(order.FindIndex(i => i.Source == ReportSource.GameFolder) < order.FindIndex(i => i.Pattern == "eternalvr-*.log"));
            var sessionLog = order.Single(i => i.Pattern == "eternalvr-*.log");
            Assert.Equal(ReportManifest.SessionsKept, sessionLog.Sessions);
            Assert.True(Worst(ReportManifest.Items) - (ReportManifest.SessionsKept - 2) * (sessionLog.HeadBytes + sessionLog.TailBytes) < ReportManifest.TotalCapBytes);
            Assert.True(ReportManifest.TotalCapBytes < 25L * 1024 * 1024, "GitHub's attachment limit");
        }

        [Fact]
        public void CapturesAreTakenWholeNewestFirstUpToTheirCap()
        {
            using (var t = new TempDir())
            {
                var inputs = Setup(t);
                var png = new byte[] { 0x89, (byte)'P', (byte)'N', (byte)'G', 0, 0xFF, 13, 10 };
                // Three captures of about half the cap each (the eye images): the newest two fit, the oldest is left out.
                long half = ReportManifest.CapturesCapBytes / 2 - 1024;
                var dir = t.Combine("data", "logs", "20260926-100000-2", "captures");
                Directory.CreateDirectory(dir);
                foreach (var stem in new[] { "capture-20260926-100100-p000010-t100", "capture-20260926-100200-p000020-t200", "capture-20260926-100300-p000030-t300" })
                {
                    File.WriteAllBytes(Path.Combine(dir, stem + "-L.png"), png.Concat(new byte[half / 2]).ToArray());
                    File.WriteAllBytes(Path.Combine(dir, stem + "-R.png"), png.Concat(new byte[half / 2]).ToArray());
                    File.WriteAllBytes(Path.Combine(dir, stem + "-UI.png"), png);
                    File.WriteAllText(Path.Combine(dir, stem + ".txt"), "head pose: 1 2 3\nlog C:\\Users\\Tester\\x\n");
                }
                var report = ReportBuilder.Build(inputs);
                string Zip(string stem, string suffix) => "sessions/20260926-100000-2/captures/" + stem + suffix;
                var names = report.Files.Select(f => f.ZipPath).ToList();
                foreach (var stem in new[] { "capture-20260926-100300-p000030-t300", "capture-20260926-100200-p000020-t200" })
                    foreach (var suffix in new[] { "-L.png", "-R.png", "-UI.png", ".txt" })
                        Assert.Contains(Zip(stem, suffix), names);
                Assert.DoesNotContain(names, n => n.Contains("p000010"));
                Assert.Contains(report.Dropped, d => d.Contains("1 older capture(s)"));

                using (var a = new ZipArchive(new MemoryStream(report.Zip), ZipArchiveMode.Read))
                {
                    var ui = a.GetEntry(Zip("capture-20260926-100300-p000030-t300", "-UI.png"));
                    using (var s = ui.Open())
                    using (var ms = new MemoryStream())
                    {
                        s.CopyTo(ms);
                        Assert.Equal(png, ms.ToArray()); // images go in byte for byte
                    }
                    using (var r = new StreamReader(a.GetEntry(Zip("capture-20260926-100300-p000030-t300", ".txt")).Open()))
                        Assert.DoesNotContain("Tester", r.ReadToEnd()); // the text files are redacted
                }
                // The text cap still holds for the logs; the captures do not count toward it.
                Assert.DoesNotContain(report.Dropped, d => d.Contains("the report would pass"));
            }
        }

        [Fact]
        public void CaptureStemsGroupACapturesFiles()
        {
            Assert.Equal("capture-1-p2-t3", ReportBuilder.CaptureStem("capture-1-p2-t3-L.png"));
            Assert.Equal("capture-1-p2-t3", ReportBuilder.CaptureStem("capture-1-p2-t3-UI.png"));
            Assert.Equal("capture-1-p2-t3", ReportBuilder.CaptureStem("capture-1-p2-t3-mono.png"));
            Assert.Equal("capture-1-p2-t3", ReportBuilder.CaptureStem("capture-1-p2-t3.txt"));
        }
    }
}
