using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using EternalVR.Launcher.Core.Launch;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>The headset probe in a copy of the launcher (<see cref="ProbeChild"/>): its printed answer, and the copy's lifetime.</summary>
    public class ProbeChildTests
    {
        private static OpenXrProbeResult Answer() => new OpenXrProbeResult
        {
            RuntimeName = "VirtualDesktopXR",
            SystemName = "Oculus Quest 3",
            Limits = new ViewLimits
            {
                Recommended = new Extent(2064, 2208),
                MaxImageRect = new Extent(16384, 16384),
                MaxSwapchain = new Extent(0, 0),
            },
        };

        [Fact]
        public void AnAnswerReadsBackAsTheSameResult()
        {
            var back = ProbeChild.Parse(ProbeChild.Serialize(Answer()));
            Assert.True(back.Ok);
            Assert.Equal("VirtualDesktopXR", back.RuntimeName);
            Assert.Equal("Oculus Quest 3", back.SystemName);
            Assert.Equal(new Extent(2064, 2208), back.Limits.Recommended);
            Assert.Equal(new Extent(16384, 16384), back.Limits.MaxImageRect);
            Assert.Equal(new Extent(0, 0), back.Limits.MaxSwapchain);
            Assert.Equal(Answer().ToString(), back.ToString());
        }

        [Fact]
        public void AFailureKeepsItsReasonAndNoHeadset()
        {
            var back = ProbeChild.Parse(ProbeChild.Serialize(OpenXrProbeResult.Failed("xrGetSystem: XR_ERROR_FORM_FACTOR_UNAVAILABLE", true)));
            Assert.False(back.Ok);
            Assert.True(back.HeadsetUnavailable);
            Assert.Equal("xrGetSystem: XR_ERROR_FORM_FACTOR_UNAVAILABLE", back.Error);

            back = ProbeChild.Parse(ProbeChild.Serialize(OpenXrProbeResult.Failed("the OpenXR loader is missing: x")));
            Assert.False(back.Ok);
            Assert.False(back.HeadsetUnavailable);
            Assert.Equal("the OpenXR loader is missing: x", back.Error);
        }

        [Fact]
        public void ANameOnSeveralLinesStaysOnOne()
        {
            var r = Answer();
            r.SystemName = "Quest\r\n3";
            var text = ProbeChild.Serialize(r);
            Assert.Equal("Quest  3", ProbeChild.Parse(text).SystemName);
        }

        [Fact]
        public void OtherOutputAroundTheAnswerIsSkipped()
        {
            var text = "runtime log line\r\n" + ProbeChild.Serialize(Answer()).Replace("\n", "\r\n") + "trailing = noise\r\n";
            var back = ProbeChild.Parse(text);
            Assert.True(back.Ok);
            Assert.Equal(new Extent(2064, 2208), back.Limits.Recommended);
        }

        [Fact]
        public void AnAnswerAfterAnUnfinishedLineIsRead()
        {
            var back = ProbeChild.Parse("runtime output without a line break" + ProbeChild.Serialize(Answer()));
            Assert.True(back.Ok, back.Error);
            Assert.Equal("Oculus Quest 3", back.SystemName);
        }

        [Theory]
        [InlineData(null)]
        [InlineData("")]
        [InlineData("probe.ok=1\nprobe.recommended=2064x2208\n")]
        public void AnAnswerWithoutItsEndIsAFailure(string text)
        {
            var back = ProbeChild.Parse(text);
            Assert.False(back.Ok);
            Assert.Equal("the probe gave no answer", back.Error);
        }

        [Theory]
        [InlineData("probe.recommended=2064\n")]
        [InlineData("probe.recommended=x2208\n")]
        [InlineData("probe.recommended=-1x2208\n")]
        [InlineData("")]
        public void UnreadableSizesAreAFailure(string recommended)
        {
            var text = "probe.ok=1\n" + recommended + "probe.max_image=16384x16384\nprobe.max_swapchain=16384x16384\nprobe.end=1\n";
            var back = ProbeChild.Parse(text);
            Assert.False(back.Ok);
            Assert.Equal("the probe's answer is unreadable", back.Error);
        }

        // ---- The copy's lifetime, with a shell script standing in for the launcher copy.

        private static bool Windows => RuntimeInformation.IsOSPlatform(OSPlatform.Windows);

        /// <summary>A script for this OS; returns the program and arguments that run it.</summary>
        private static (string Exe, string Args) Script(TempDir dir, string cmd, string sh)
        {
            if (Windows)
            {
                var path = dir.Write("probe.cmd", "@echo off\r\n" + cmd.Replace("\n", "\r\n"));
                return ("cmd.exe", "/d /c \"" + path + "\"");
            }
            return ("/bin/sh", "\"" + dir.Write("probe.sh", sh) + "\"");
        }

        /// <summary>The same in cmd and sh.</summary>
        private const string AnswerLines =
            "echo probe.ok=1\necho probe.runtime=Fake\necho probe.system=Fake headset\necho probe.recommended=1832x1920\n"
            + "echo probe.max_image=4096x4096\necho probe.max_swapchain=4096x4096\necho probe.end=1\n";

        /// <summary>Waits <paramref name="seconds"/> in a child process of the script (ping on Windows: timeout refuses redirected input).</summary>
        private static string SleepCmd(int seconds) => $"ping -n {seconds + 1} 127.0.0.1 >nul\n";

        private static string SleepSh(int seconds) => $"sleep {seconds}\n";

        [Fact]
        public void ACopyThatAnswersAndExitsGivesItsAnswer()
        {
            using (var dir = new TempDir())
            {
                var (exe, args) = Script(dir, AnswerLines, AnswerLines);
                var r = ProbeChild.Run(exe, args, null, timeoutMs: 10000, graceMs: 2000);
                Assert.True(r.Ok, r.Error);
                Assert.Equal("Fake", r.RuntimeName);
                Assert.Equal("Fake headset", r.SystemName);
                Assert.Equal(new Extent(1832, 1920), r.Limits.Recommended);
            }
        }

        [Fact]
        public void ACopyThatNeverAnswersIsKilledAfterItsTime()
        {
            using (var dir = new TempDir())
            {
                var marker = dir.Combine("still-running.txt");
                var (exe, args) = Script(dir,
                    SleepCmd(4) + "echo late> \"" + marker + "\"\n" + AnswerLines,
                    SleepSh(4) + "echo late > \"" + marker + "\"\n" + AnswerLines);
                var watch = Stopwatch.StartNew();
                var r = ProbeChild.Run(exe, args, null, timeoutMs: 500, graceMs: 500);
                watch.Stop();
                Assert.False(r.Ok);
                Assert.Equal("the runtime did not answer within 0.5 s", r.Error);
                Assert.InRange(watch.ElapsedMilliseconds, 900, 3900);
                // Killed: the script never reaches the line after its wait.
                Thread.Sleep(5000);
                Assert.False(File.Exists(marker));
            }
        }

        [Fact]
        public void ACopyThatAnswersButDoesNotExitStillGivesItsAnswer()
        {
            using (var dir = new TempDir())
            {
                var marker = dir.Combine("still-running.txt");
                var (exe, args) = Script(dir,
                    AnswerLines + SleepCmd(6) + "echo late> \"" + marker + "\"\n",
                    AnswerLines + SleepSh(6) + "echo late > \"" + marker + "\"\n");
                var watch = Stopwatch.StartNew();
                var r = ProbeChild.Run(exe, args, null, timeoutMs: 10000, graceMs: 2000);
                watch.Stop();
                Assert.True(r.Ok, r.Error);
                Assert.Equal(new Extent(1832, 1920), r.Limits.Recommended);
                Assert.True(watch.ElapsedMilliseconds < 5000, watch.ElapsedMilliseconds + " ms");
                Thread.Sleep(7000);
                Assert.False(File.Exists(marker));
            }
        }

        [Fact]
        public void ACopyThatExitsWithoutAnAnswerSaysSo()
        {
            using (var dir = new TempDir())
            {
                var (exe, args) = Script(dir, "echo something else\nexit /b 7\n", "echo something else\nexit 7\n");
                var r = ProbeChild.Run(exe, args, null, timeoutMs: 10000, graceMs: 2000);
                Assert.False(r.Ok);
                Assert.Equal("the probe stopped without an answer (exit code 7)", r.Error);
            }
        }

        [Fact]
        public void TheCopyGetsTheGamesOpenXrEnvironment()
        {
            const string removed = "EVR_PROBE_CHILD_TEST_REMOVED";
            Environment.SetEnvironmentVariable(removed, "inherited");
            try
            {
                using (var dir = new TempDir())
                {
                    var (exe, args) = Script(dir,
                        "echo probe.ok=1\necho probe.runtime=%XR_RUNTIME_JSON%\n"
                        + "if defined " + removed + " (echo probe.system=kept) else (echo probe.system=removed)\n"
                        + "echo probe.recommended=1x1\necho probe.max_image=1x1\necho probe.max_swapchain=1x1\necho probe.end=1\n",
                        // printf, not echo: the echo of dash (/bin/sh) reads the path's "\v" as a vertical tab.
                        "echo probe.ok=1\nprintf '%s\\n' \"probe.runtime=$XR_RUNTIME_JSON\"\n"
                        + "if [ -n \"${" + removed + "+x}\" ]; then echo probe.system=kept; else echo probe.system=removed; fi\n"
                        + "echo probe.recommended=1x1\necho probe.max_image=1x1\necho probe.max_swapchain=1x1\necho probe.end=1\n");
                    var env = new[]
                    {
                        new KeyValuePair<string, string>("XR_RUNTIME_JSON", @"D:\Runtimes\VD\virtualdesktop-openxr.json"),
                        new KeyValuePair<string, string>(removed, null),
                    };
                    var r = ProbeChild.Run(exe, args, env, timeoutMs: 10000, graceMs: 2000);
                    Assert.True(r.Ok, r.Error);
                    Assert.Equal(@"D:\Runtimes\VD\virtualdesktop-openxr.json", r.RuntimeName);
                    Assert.Equal("removed", r.SystemName);
                    // This process's own environment is untouched.
                    Assert.Equal("inherited", Environment.GetEnvironmentVariable(removed));
                }
            }
            finally
            {
                Environment.SetEnvironmentVariable(removed, null);
            }
        }

        [Fact]
        public void AProgramThatCannotStartIsAFailure()
        {
            using (var dir = new TempDir())
            {
                var r = ProbeChild.Run(dir.Combine("no-such-launcher.exe"), ProbeChild.Switch + " x", null, timeoutMs: 1000, graceMs: 500);
                Assert.False(r.Ok);
                Assert.StartsWith("the probe could not be started", r.Error);
            }
        }
    }
}
