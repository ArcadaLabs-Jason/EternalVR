using System;
using System.IO;
using System.Linq;
using System.Text;

namespace EternalVR.Launcher.Core.Launch
{
    /// <summary>
    /// The game's own reason when it stops before EternalVR loaded: the last line starting with <c>FATAL ERROR:</c> in
    /// its console log (<c>qconsole.log</c>, in the <c>base</c> folder of its Saved Games folder). A player's game that
    /// found no working Vulkan driver wrote "FATAL ERROR: Please update your driver: VK_KHR_get_surface_capabilities2 is
    /// required." there and exited 1.1 s after it started; the launcher's own advice could not help with that.
    /// </summary>
    public static class GameFatalError
    {
        public const string Prefix = "FATAL ERROR:";
        public const string FileName = "qconsole.log";
        public const int TailBytes = 64 * 1024;
        public const int MaxLength = 200;

        /// <summary>The text after the prefix of the log's last such line, or null when the log is missing, unreadable, older
        /// than <paramref name="notBeforeUtc"/> (an earlier start's log) or has no such line. Never throws.</summary>
        public static string Read(string consoleLogPath, DateTime notBeforeUtc)
        {
            try
            {
                if (string.IsNullOrEmpty(consoleLogPath)) return null;
                var info = new FileInfo(consoleLogPath);
                if (!info.Exists || info.LastWriteTimeUtc < notBeforeUtc) return null;
                using (var stream = new FileStream(consoleLogPath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
                {
                    var length = stream.Length;
                    var start = Math.Max(0, length - TailBytes);
                    stream.Seek(start, SeekOrigin.Begin);
                    var buffer = new byte[length - start];
                    int read = 0, n;
                    while (read < buffer.Length && (n = stream.Read(buffer, read, buffer.Length - read)) > 0) read += n;
                    return LastFatalLine(buffer, read);
                }
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException || e is ArgumentException
                || e is NotSupportedException || e is OutOfMemoryException)
            {
                return null;
            }
        }

        /// <summary>The console log's path for a Saved Games folder.</summary>
        public static string LogPath(string savedGamesDir) =>
            string.IsNullOrEmpty(savedGamesDir) ? null : Path.Combine(savedGamesDir, Game.GameLayout.SavedGamesBaseFolder, FileName);

        /// <summary>Write-time rounding of FAT/exFAT (2 s) and a start time converted from local time at a clock change.
        /// Safe because the game truncates its console log at every start (see <see cref="Lookup"/>).</summary>
        public static readonly TimeSpan StartSlack = TimeSpan.FromSeconds(2);

        /// <summary>
        /// What the launcher shows: the game's own error from this launch's console log, or null (also when the layer loaded).
        /// The whole lookup, the Saved Games folder's path included, never throws. A log written since
        /// <paramref name="gameStartUtc"/> belongs to this launch only because the game truncates <c>qconsole.log</c> at start
        /// (<c>logFilePersistent</c> defaults to 0); with that cvar in the player's extra arguments the log keeps older starts'
        /// lines, so no line is read at all and the old message stays.
        /// </summary>
        public static string Lookup(bool layerLoaded, Func<string> savedGamesDir, string extraArguments, DateTime gameStartUtc)
        {
            try
            {
                if (layerLoaded) return null;
                if (extraArguments != null && extraArguments.IndexOf("logFilePersistent", StringComparison.OrdinalIgnoreCase) >= 0) return null;
                return Read(LogPath(savedGamesDir()), gameStartUtc - StartSlack);
            }
            catch (Exception e) when (!(e is OutOfMemoryException))
            {
                return null;
            }
        }

        internal static string LastFatalLine(byte[] bytes, int count)
        {
            // NUL bytes out: UTF-16 text then reads as its ASCII range, whatever the start of the tail cut it at.
            var clean = bytes.Take(count).Where(b => b != 0).ToArray();
            var text = new UTF8Encoding(false, false).GetString(clean);
            var line = text.Split('\n').Select(l => l.Trim().TrimStart('\uFEFF', '\uFFFD').TrimStart())
                .LastOrDefault(l => l.StartsWith(Prefix, StringComparison.Ordinal));
            if (line == null) return null;
            var reason = new string(line.Substring(Prefix.Length).Select(c => char.IsControl(c) ? ' ' : c).ToArray()).Trim();
            if (reason.Length == 0) return null;
            return reason.Length > MaxLength ? reason.Substring(0, MaxLength - 3).TrimEnd() + "..." : reason;
        }
    }
}
