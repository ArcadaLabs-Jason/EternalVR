using System.Globalization;

namespace EternalVR.Launcher.Core.Launch
{
    /// <summary>
    /// What the game's exit code says about how it ended. A crash that the game's own handler catches writes a
    /// Crash.*.html and ends the process; one that skips it ends with the exception's status code (0xC0000005 and
    /// the like). A program that ends itself or is ended by another (Task Manager uses 1) leaves the code it was
    /// given, and a normal quit leaves 0. The log line lets a report tell these apart when neither the game nor
    /// Windows recorded a crash (public issue #5).
    /// </summary>
    public static class GameExit
    {
        /// <summary>An NTSTATUS error code (severity bits 11) or an unhandled breakpoint: the process ended on an
        /// exception or a fatal check.</summary>
        public static bool IsCrash(int exitCode) =>
            (unchecked((uint)exitCode) >> 30) == 3 || unchecked((uint)exitCode) == 0x80000003;

        /// <summary>"exit code 0xC0000005 (access violation: a crash)" and the like.</summary>
        public static string Describe(int exitCode)
        {
            uint code = unchecked((uint)exitCode);
            string number = code > 0xFFFF
                ? "0x" + code.ToString("X8", CultureInfo.InvariantCulture)
                : code.ToString(CultureInfo.InvariantCulture);
            return "exit code " + number + " (" + Meaning(code) + ")";
        }

        private static string Meaning(uint code)
        {
            switch (code)
            {
                case 0: return "a normal quit";
                case 0xC0000005: return "access violation: a crash";
                case 0xC000001D: return "illegal instruction: a crash";
                case 0xC0000094: return "integer division by zero: a crash";
                case 0xC00000FD: return "stack overflow: a crash";
                case 0xC0000374: return "heap corruption: a crash";
                case 0xC0000409: return "fast fail: the program stopped itself on a failed check, a crash";
                case 0xC000013A: return "closed with Ctrl+C or its console";
                case 0xC0000142: return "a DLL failed to start";
                case 0x40010004: return "ended by a debugger";
                case 0xE06D7363: return "an unhandled C++ exception: a crash";
                case 0x80000003: return "a breakpoint: a crash";
            }
            if (IsCrash(unchecked((int)code))) return "an error status: a crash";
            return "the game ended itself with this code, or another program ended it (Task Manager uses 1)";
        }
    }
}
