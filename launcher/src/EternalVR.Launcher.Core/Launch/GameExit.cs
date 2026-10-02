using System.Globalization;

namespace EternalVR.Launcher.Core.Launch
{
    /// <summary>
    /// What the game's exit code says about how it ended. A crash that the game's own handler catches writes a
    /// Crash.*.html and ends the process; one that skips it ends with the exception's status code (0xC0000005 and
    /// the like). A program that ends itself or is ended by another (Task Manager uses 1) leaves the code it was
    /// given, and a normal quit leaves 0. The log line lets a report tell these apart when neither the game nor
    /// Windows recorded a crash (public issue #5). -1 (0xFFFFFFFF) has the error status's top bits but is not an
    /// exception code: the game was ended, by itself or by another program, typically after it froze (frozen VR
    /// sessions on an AMD card ended with it), so it is not called a crash.
    /// </summary>
    public static class GameExit
    {
        /// <summary>An NTSTATUS error code (severity bits 11, -1 excepted) or an unhandled breakpoint: the process
        /// ended on an exception or a fatal check.</summary>
        public static bool IsCrash(int exitCode) =>
            ((unchecked((uint)exitCode) >> 30) == 3 && !WasEnded(exitCode)) || unchecked((uint)exitCode) == 0x80000003;

        /// <summary>Exit code -1: the game was ended (by itself or by another program), often after a freeze; not
        /// a crash.</summary>
        public static bool WasEnded(int exitCode) => exitCode == -1;

        /// <summary>The start of the launcher's status line after the game exits: what to do after a crash or after
        /// the game was ended with -1; empty for any other exit (or none known).</summary>
        public static string StatusPrefix(int? exitCode)
        {
            if (!exitCode.HasValue) return string.Empty;
            if (IsCrash(exitCode.Value))
                return "The game crashed (" + Describe(exitCode.Value) + "). Use Export report... and attach the zip to a GitHub issue. ";
            if (WasEnded(exitCode.Value))
                return "The game was ended (exit code -1), often after it froze. If it froze, use Export report... and attach the zip to a GitHub issue. ";
            return string.Empty;
        }

        /// <summary>"exit code 0xC0000005 (access violation: a crash)" and the like.</summary>
        public static string Describe(int exitCode)
        {
            uint code = unchecked((uint)exitCode);
            string number = WasEnded(exitCode) ? "-1"
                : code > 0xFFFF
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
                case 0xFFFFFFFF: return "the game was ended, by itself or by another program, often after it froze";
            }
            if (IsCrash(unchecked((int)code))) return "an error status: a crash";
            return "the game ended itself with this code, or another program ended it (Task Manager uses 1)";
        }
    }
}
