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
    }
}
