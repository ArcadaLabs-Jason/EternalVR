using System;
using System.IO;
using System.Text;
using EternalVR.Launcher.Core.Launch;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    public class GameFatalErrorTests : IDisposable
    {
        // The last line of a real console log of a game that found no working Vulkan driver.
        private const string Real = "FATAL ERROR: Please update your driver: VK_KHR_get_surface_capabilities2 is required.";
        private const string Reason = "Please update your driver: VK_KHR_get_surface_capabilities2 is required.";
        private const int NotACrashCode = 1;
        private const int Breakpoint = unchecked((int)0x80000003);

        private readonly string dir = Path.Combine(Path.GetTempPath(), "evr-fatal-" + Guid.NewGuid().ToString("N"));
        private readonly DateTime start = new DateTime(2026, 10, 18, 12, 0, 0, DateTimeKind.Utc);

        public GameFatalErrorTests() => Directory.CreateDirectory(dir);

        public void Dispose()
        {
            try { Directory.Delete(dir, true); } catch (IOException) { }
        }

        private string Log(string text, DateTime writtenUtc, Encoding enc = null)
        {
            var path = Path.Combine(dir, "qconsole.log");
            File.WriteAllText(path, text, enc ?? new UTF8Encoding(false));
            File.SetLastWriteTimeUtc(path, writtenUtc);
            return path;
        }

        [Fact]
        public void TheLastFatalLineOfThisLaunchIsTheGameOwnError()
        {
            var path = Log("idLib::Init\r\nVulkan: loading\r\n" + Real + "\r\n", start.AddSeconds(1));
            Assert.Equal(Reason, GameFatalError.Read(path, start));
        }

        [Fact]
        public void ALogWithoutAFatalLineGivesNothing()
        {
            var path = Log("idLib::Init\r\nVulkan: loading\r\nsome FATAL ERROR: in the middle of a line\r\n", start.AddSeconds(1));
            Assert.Null(GameFatalError.Read(path, start));
        }

        [Fact]
        public void ALogFromAnEarlierLaunchIsNotRead()
        {
            var path = Log(Real + "\r\n", start.AddMinutes(-30));
            Assert.Null(GameFatalError.Read(path, start));
        }

        [Fact]
        public void AMissingLogOrFolderGivesNothing()
        {
            Assert.Null(GameFatalError.Read(Path.Combine(dir, "none", "qconsole.log"), start));
            Assert.Null(GameFatalError.Read(null, start));
            Assert.Null(GameFatalError.Read(string.Empty, start));
            Assert.Null(GameFatalError.Read(dir, start)); // a folder, not a file
        }

        [Fact]
        public void TheLastOfSeveralFatalLinesWins()
        {
            var path = Log("FATAL ERROR: first\r\nmore\r\n" + Real + "\r\nshutdown\r\n", start.AddSeconds(1));
            Assert.Equal(Reason, GameFatalError.Read(path, start));
        }

        [Fact]
        public void ALogThatTheGameStillHoldsOpenIsRead()
        {
            var path = Path.Combine(dir, "qconsole.log");
            using (var writer = new FileStream(path, FileMode.Create, FileAccess.Write, FileShare.Read))
            {
                var bytes = Encoding.ASCII.GetBytes(Real + "\r\n");
                writer.Write(bytes, 0, bytes.Length);
                writer.Flush();
                Assert.Equal(Reason, GameFatalError.Read(path, DateTime.UtcNow.AddMinutes(-1)));
            }
        }

        [Fact]
        public void OnlyTheTailOfAHugeLogIsRead()
        {
            var filler = new string('x', 100) + "\r\n";
            var sb = new StringBuilder("FATAL ERROR: in the part that is not read\r\n");
            for (int i = 0; i < 3000; i++) sb.Append(filler); // ~300 KB
            sb.Append(Real).Append("\r\n");
            var path = Log(sb.ToString(), start.AddSeconds(1));
            Assert.Equal(Reason, GameFatalError.Read(path, start));
            // The line is cut off by the tail's start: still no failure, and no half line taken for a whole one.
            var onlyOld = Log("FATAL ERROR: in the part that is not read\r\n" + new string('y', 200 * 1024) + "\r\n", start.AddSeconds(1));
            Assert.Null(GameFatalError.Read(onlyOld, start));
        }

        [Fact]
        public void OtherEncodingsAreReadOrGiveNothing()
        {
            Assert.Equal(Reason, GameFatalError.Read(Log("x\r\n" + Real + "\r\n", start.AddSeconds(1), Encoding.Unicode), start));
            // The FATAL line as the very first line of a UTF-16 log (its BOM is read as replacement characters).
            Assert.Equal(Reason, GameFatalError.Read(Log(Real + "\r\n", start.AddSeconds(1), Encoding.Unicode), start));
            Assert.Equal(Reason, GameFatalError.Read(Log(Real + "\r\n", start.AddSeconds(1), new UTF8Encoding(true)), start));
            var path = Path.Combine(dir, "qconsole.log");
            File.WriteAllBytes(path, new byte[] { 0xFF, 0xFE, 0x80, 0x81, 0xC0, 0x0A });
            File.SetLastWriteTimeUtc(path, start.AddSeconds(1));
            Assert.Null(GameFatalError.Read(path, start));
        }

        [Fact]
        public void ALongLineIsKeptToASaneLengthAndAnEmptyOneIsNothing()
        {
            var path = Log("FATAL ERROR: " + new string('z', 5000) + "\r\n", start.AddSeconds(1));
            var line = GameFatalError.Read(path, start);
            Assert.True(line.Length <= GameFatalError.MaxLength);
            Assert.EndsWith("...", line);
            Assert.Null(GameFatalError.Read(Log("FATAL ERROR:   \r\n", start.AddSeconds(1)), start));
        }

        [Fact]
        public void TheStatusLineShowsTheGamesOwnErrorWhenEternalVrNeverLoaded()
        {
            foreach (var code in new[] { Breakpoint, NotACrashCode })
            {
                var message = StartWatch.EarlyExitMessage(1.2, code, layerLoaded: false, gameError: Reason);
                Assert.Equal("The game stopped before EternalVR loaded, with its own error: " + Reason + " Still stopping? " + Report.ReportHint.Ask, message);
                Assert.DoesNotContain("Xbox app", message);
            }
            // The layer had loaded: the game's line is not the reason shown.
            Assert.DoesNotContain("own error", StartWatch.EarlyExitMessage(3.0, Breakpoint, layerLoaded: true, gameError: Reason));
        }

        [Fact]
        public void WithoutAFatalLineTheCurrentMessagesStay()
        {
            Assert.StartsWith("The game crashed before EternalVR loaded. Start it once from the Xbox app or Steam",
                StartWatch.EarlyExitMessage(1.2, Breakpoint, layerLoaded: false, gameError: null));
            Assert.StartsWith("The game closed 2 s after it started (exit code 1)", StartWatch.EarlyExitMessage(2.0, 1, layerLoaded: false));
            Assert.StartsWith("The game closed right after it started, before EternalVR loaded. Start it once",
                StartWatch.GoneMessage(layerLoaded: false, gameError: null));
        }

        [Fact]
        public void AGameThatWasGoneBeforeItWasOpenedShowsItsOwnErrorToo()
        {
            var message = StartWatch.GoneMessage(layerLoaded: false, gameError: "Out of memory");
            Assert.Contains("with its own error: Out of memory. Still closing?", message);
            Assert.True(Report.ReportHint.In(message));
            Assert.DoesNotContain("own error", StartWatch.GoneMessage(layerLoaded: true, gameError: "Out of memory"));
        }

        [Fact]
        public void TheLookupNeverThrowsOnAnIllegalFolder()
        {
            Assert.Null(GameFatalError.Lookup(false, () => "C:\\bad\0folder", null, start));
            Assert.Null(GameFatalError.Lookup(false, () => throw new ArgumentException("no folder"), null, start));
        }

        [Fact]
        public void TheLookupSkipsAnAppendingLogAndALoadedLayer()
        {
            var sg = Path.Combine(dir, "sg");
            Directory.CreateDirectory(Path.Combine(sg, "base"));
            File.WriteAllText(Path.Combine(sg, "base", "qconsole.log"), Real + "\r\n");
            var now = DateTime.UtcNow;
            Assert.Equal(Reason, GameFatalError.Lookup(false, () => sg, "+r_foo 1", now.AddSeconds(-5)));
            // A log that keeps earlier starts' lines can hold an old FATAL line: nothing is read.
            Assert.Null(GameFatalError.Lookup(false, () => sg, "+set LOGFILEPERSISTENT 1", now.AddSeconds(-5)));
            // The layer loaded: the game's line is not looked up.
            Assert.Null(GameFatalError.Lookup(true, () => sg, null, now.AddSeconds(-5)));
        }

        [Fact]
        public void TheLookupAllowsTwoSecondsOfWriteTimeSlack()
        {
            var sg = Path.Combine(dir, "sg");
            Directory.CreateDirectory(Path.Combine(sg, "base"));
            var path = Path.Combine(sg, "base", "qconsole.log");
            File.WriteAllText(path, Real + "\r\n");
            File.SetLastWriteTimeUtc(path, start.AddSeconds(-1.5));
            Assert.Equal(Reason, GameFatalError.Lookup(false, () => sg, null, start));
            File.SetLastWriteTimeUtc(path, start.AddSeconds(-3));
            Assert.Null(GameFatalError.Lookup(false, () => sg, null, start));
        }

        [Fact]
        public void TheLogIsFoundInTheBaseFolderOfSavedGames()
        {
            Assert.Equal(Path.Combine("sg", "base", "qconsole.log"), GameFatalError.LogPath("sg"));
            Assert.Null(GameFatalError.LogPath(null));
        }
    }
}
