using System;
using System.IO;
using System.Security;
using System.Text;
using EternalVR.Launcher.Core.Preflight;
using Xunit;
using static EternalVR.Launcher.Core.Tests.RtssProfilesTests;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>
    /// The edges of reading RTSS's profiles: whatever cannot be read for certain is unreadable (the launcher warns), never
    /// "no limit", and nothing throws.
    /// </summary>
    public class RtssProfilesReadTests
    {
        private const string Manifest = "RivaTuner Statistics Server/Vulkan/RTSSVkLayer64.json";

        private static string Limited(string limit = "60") => Global().Replace("Limit\t\t\t\t\t= 0", "Limit\t\t\t\t\t= " + limit);

        /// <summary>Reads a Global profile written with <paramref name="encoding"/>.</summary>
        private static RtssLimit FromEncoded(string global, Encoding encoding)
        {
            using (var t = new TempDir())
            {
                var path = t.Write(Manifest, "{}");
                var file = t.Combine("RivaTuner Statistics Server", "Profiles", "Global");
                Directory.CreateDirectory(Path.GetDirectoryName(file));
                File.WriteAllText(file, global, encoding);
                return RtssProfiles.Read(path);
            }
        }

        private static void AssertUnreadable(RtssLimit limit, string problem)
        {
            Assert.False(limit.Readable);
            Assert.False(limit.LimitsTheGame);
            Assert.Contains(problem, limit.Problem);
            Assert.Equal(LayerAction.Warn, Decide(limit).Action);
        }

        [Fact]
        public void NoFailureEscapes()
        {
            AssertUnreadable(RtssProfiles.Read("C:\\R\0TSS|<>\\Vulkan\\RTSSVkLayer64.json"), "");
            using (var t = new TempDir())
                AssertUnreadable(RtssProfiles.Read(t.Combine("not here", "Vulkan", "RTSSVkLayer64.json")), "there is no ");
            foreach (var e in new Exception[]
                     {
                         new IOException("in use"), new UnauthorizedAccessException("denied"), new SecurityException("not allowed"),
                         new NotSupportedException("odd path"), new ArgumentException("bad path"),
                     })
            {
                AssertUnreadable(RtssProfiles.Read(Path.Combine("x", "Vulkan", "RTSSVkLayer64.json"), _ => true, _ => throw e), e.Message);
                AssertUnreadable(RtssProfiles.Read(Path.Combine("x", "Vulkan", "RTSSVkLayer64.json"), _ => throw e, _ => ""), e.Message);
            }
            // Only the game's profile failing still fails the read.
            AssertUnreadable(RtssProfiles.Read(Path.Combine("x", "Vulkan", "RTSSVkLayer64.json"), _ => true,
                p => p.EndsWith(RtssProfiles.GameProfile, StringComparison.Ordinal) ? throw new IOException("in use") : Global()), "in use");
        }

        [Fact]
        public void AHeaderThatIsNotACleanNameIsUnreadable()
        {
            AssertUnreadable(FromFiles(Limited().Replace("[Framerate]", "[Framerate] ; caps")), "section header it cannot read: [Framerate] ; caps");
            AssertUnreadable(FromFiles(Global().Replace("[Framerate]", "[Framerate")), "section header it cannot read");
            AssertUnreadable(FromFiles(Global(), "[Framerate]x\r\nLimit = 60\r\n"), "DOOMEternalx64vk.exe.cfg profile has a section header");
            Assert.True(FromFiles(Limited().Replace("[Framerate]", "[Framerate]  \t")).LimitsTheGame);
        }

        [Fact]
        public void ConflictingDuplicatesAreUnreadable()
        {
            AssertUnreadable(FromFiles(Global().Replace("[Framerate]\r\n", "[Framerate]\r\nLimit = 60\r\n")),
                "Global profile has conflicting duplicate values for Limit (60 and 0)");
            AssertUnreadable(FromFiles(Global() + "[Framerate]\r\nLimit = 60\r\n"), "conflicting duplicate values for Limit");
            AssertUnreadable(FromFiles(Global(), "[Framerate]\r\nlimit = 60\r\nLIMIT = 0\r\n"), "conflicting duplicate values for LIMIT");
            // The same value twice is no conflict.
            Assert.True(FromFiles(Global() + "[framerate]\r\nLimit = 0\r\n").Readable);
        }

        [Fact]
        public void CommentsAreSkippedAndTrailingTextIsNotANumber()
        {
            Assert.False(FromFiles(Global().Replace("[Framerate]\r\n", "[Framerate]\r\n; Limit = 60\r\n# LimitTime = 16667\r\n")).LimitsTheGame);
            Assert.True(FromFiles(Global().Replace("[Framerate]\r\n", "[Framerate]\r\n; Limit = 60\r\n")).Readable);
            AssertUnreadable(FromFiles(Limited("60 ; cap")), "Limit = 60 ; cap in its Global profile is not a number");
            AssertUnreadable(FromFiles(Global(), "[Framerate]\r\nLimit = 60fps\r\n"), "Limit = 60fps in its DOOMEternalx64vk.exe.cfg profile");
        }

        [Fact]
        public void TheEncodingsAndLineEndingsRtssMayWrite()
        {
            const string sixty = "a limit of 60 frames per second in its Global profile";
            Assert.Equal(new[] { sixty }, FromEncoded(Limited(), new UTF8Encoding(true)).Limits);
            Assert.Equal(new[] { sixty }, FromEncoded(Limited(), new UnicodeEncoding(false, true)).Limits);
            Assert.Equal(new[] { sixty }, FromEncoded(Limited().Replace("\r\n", "\n"), new UTF8Encoding(false)).Limits);
            // UTF-16 without its byte order mark cannot be told apart: unreadable, never "no limit".
            AssertUnreadable(FromEncoded(Limited(), new UnicodeEncoding(false, false)), "");
            AssertUnreadable(FromEncoded(Global(), new UnicodeEncoding(false, false)), "");
            Assert.Equal(new[] { "a limit of 30 frames per second in its Global profile" },
                RtssProfiles.Evaluate("\uFEFF" + Limited("30"), null).Limits);
        }

        [Fact]
        public void NamesAreCaseInsensitive()
        {
            Assert.Equal(new[] { "a limit of 45 frames per second in its DOOMEternalx64vk.exe.cfg profile" },
                FromFiles(Global().ToLowerInvariant(), "[FRAMERATE]\r\nlimit=45\r\nlimitdenominator=1\r\n").Limits);
        }

        [Fact]
        public void ValuesAtTheirEdges()
        {
            // A negative limit is no limit; a zero denominator counts as 1.
            Assert.False(FromFiles(Limited("-60")).LimitsTheGame);
            Assert.True(FromFiles(Limited("-60")).Readable);
            Assert.Equal(new[] { "a limit of 60 frames per second in its Global profile" },
                FromFiles(Limited().Replace("LimitDenominator\t\t\t= 1", "LimitDenominator\t\t\t= 0")).Limits);
            Assert.Equal(new[] { "a frame time limit of 16666.7 microseconds (about 60 frames per second) in its DOOMEternalx64vk.exe.cfg profile" },
                FromFiles(Global(), "[Framerate]\r\nLimitTime = 166667\r\nLimitTimeDenominator = 10\r\n").Limits);
            Assert.Equal(new[] { "scanline sync, which ties frames to the display's refresh, in its Global profile" },
                FromFiles(Global().Replace("SyncScanline1\t\t\t\t= 0", "SyncScanline1\t\t\t\t= 100")).Limits);
        }

        [Fact]
        public void AGameProfileWithoutAFramerateSectionLeavesItToGlobal()
        {
            Assert.Equal(new[] { "a limit of 60 frames per second in its Global profile" },
                FromFiles(Limited(), "[OSD]\r\nEnableOSD = 0\r\n").Limits);
            Assert.False(FromFiles(Global(), "[OSD]\r\nEnableOSD = 0\r\n").LimitsTheGame);
            Assert.True(FromFiles(Global(), "").Readable);
        }
    }
}
