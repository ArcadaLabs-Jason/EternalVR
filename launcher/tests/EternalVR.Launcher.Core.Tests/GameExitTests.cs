using EternalVR.Launcher.Core.Launch;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    public class GameExitTests
    {
        [Fact]
        public void ANormalQuitIsNotACrash()
        {
            Assert.False(GameExit.IsCrash(0));
            Assert.Equal("exit code 0 (a normal quit)", GameExit.Describe(0));
        }

        [Fact]
        public void ExceptionStatusCodesAreCrashesInHex()
        {
            Assert.True(GameExit.IsCrash(unchecked((int)0xC0000005)));
            Assert.Equal("exit code 0xC0000005 (access violation: a crash)", GameExit.Describe(unchecked((int)0xC0000005)));
            Assert.Contains("fast fail", GameExit.Describe(unchecked((int)0xC0000409)));
            Assert.Contains("heap corruption", GameExit.Describe(unchecked((int)0xC0000374)));
            Assert.Contains("stack overflow", GameExit.Describe(unchecked((int)0xC00000FD)));
            // An error status without a name of its own.
            Assert.True(GameExit.IsCrash(unchecked((int)0xC0001234)));
            Assert.Equal("exit code 0xC0001234 (an error status: a crash)", GameExit.Describe(unchecked((int)0xC0001234)));
        }

        [Fact]
        public void SmallCodesAreTheGameOrAnotherProgramEndingIt()
        {
            Assert.False(GameExit.IsCrash(1));
            Assert.StartsWith("exit code 1 (", GameExit.Describe(1));
            Assert.Contains("Task Manager uses 1", GameExit.Describe(1));
            // The C++ exception code's severity bits are 11 too; a breakpoint's are 10 but it is a crash all the same.
            Assert.True(GameExit.IsCrash(unchecked((int)0xE06D7363)));
            Assert.True(GameExit.IsCrash(unchecked((int)0x80000003)));
            Assert.Contains("a crash", GameExit.Describe(unchecked((int)0x80000003)));
            Assert.False(GameExit.IsCrash(unchecked((int)0x80000005)));
        }

        [Fact]
        public void MinusOneIsTheGameBeingEndedNotACrash()
        {
            // 0xFFFFFFFF has the error status's top bits, but a frozen game that was closed ends with it.
            Assert.False(GameExit.IsCrash(-1));
            Assert.False(GameExit.IsCrash(unchecked((int)0xFFFFFFFF)));
            Assert.True(GameExit.WasEnded(-1));
            Assert.False(GameExit.WasEnded(0));
            Assert.False(GameExit.WasEnded(1));
            Assert.False(GameExit.WasEnded(unchecked((int)0xC0000005)));
            string text = GameExit.Describe(-1);
            Assert.StartsWith("exit code -1 (the game was ended", text);
            Assert.Contains("often after it froze", text);
            Assert.DoesNotContain("a crash)", text);
            // Its neighbours keep their meaning.
            Assert.True(GameExit.IsCrash(unchecked((int)0xFFFFFFFE)));
            Assert.Equal("exit code 0xFFFFFFFE (an error status: a crash)", GameExit.Describe(unchecked((int)0xFFFFFFFE)));
        }

        [Fact]
        public void TheStatusLineAsksForAReportAfterACrashOrMinusOne()
        {
            Assert.Equal(string.Empty, GameExit.StatusPrefix(null));
            Assert.Equal(string.Empty, GameExit.StatusPrefix(0));
            Assert.Equal(string.Empty, GameExit.StatusPrefix(1));
            Assert.Equal("The game crashed (exit code 0xC0000005 (access violation: a crash)). Click Export report and post the zip with your bug report. ",
                GameExit.StatusPrefix(unchecked((int)0xC0000005)));
            string ended = GameExit.StatusPrefix(-1);
            Assert.StartsWith("The game was ended (exit code -1)", ended);
            Assert.DoesNotContain("crashed", ended);
            Assert.True(Report.ReportHint.In(ended));
        }
    }
}
