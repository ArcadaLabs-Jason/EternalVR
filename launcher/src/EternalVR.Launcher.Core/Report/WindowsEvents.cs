using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;

namespace EternalVR.Launcher.Core.Report
{
    /// <summary>One Windows event log entry, as the launcher read it.</summary>
    public sealed class WindowsEvent
    {
        public WindowsEvent(DateTime time, string log, string source, int id, int level, string message)
        {
            Time = time;
            Log = log;
            Source = source;
            Id = id;
            Level = level;
            Message = message;
        }

        /// <summary>When it was written, local time.</summary>
        public DateTime Time { get; }
        public string Log { get; }
        /// <summary>The provider (the "Source" column of Event Viewer).</summary>
        public string Source { get; }
        public int Id { get; }
        /// <summary>1 critical, 2 error, 3 warning, 4 or 0 information, 5 verbose.</summary>
        public int Level { get; }
        public string Message { get; }
    }

    /// <summary>What was read from one event log: its entries, or why it could not be read.</summary>
    public sealed class WindowsEventLogRead
    {
        public WindowsEventLogRead(string log, IReadOnlyList<WindowsEvent> events, string error)
        {
            Log = log;
            Events = events ?? new WindowsEvent[0];
            Error = error;
        }

        public string Log { get; }
        public IReadOnlyList<WindowsEvent> Events { get; }
        /// <summary>Why the log could not be read (or not to the end); null when it was.</summary>
        public string Error { get; }
    }

    /// <summary>
    /// The Windows event log entries of a report (<see cref="ReportManifest.WindowsEventsFile"/>): from the Application
    /// log, the crashes (Application Error 1000), crash reports (Windows Error Reporting 1001) and hangs (Application
    /// Hang 1002) that name the game, the launcher or the layer; from the System log, display driver resets (Display
    /// 4101) and the errors of the NVIDIA, AMD and Intel display drivers. Only the last <see cref="Days"/> days, newest
    /// first, at most <see cref="PerLog"/> per log. The launcher reads the logs (Platform/WindowsEventLogs.cs); what is
    /// taken and how it reads is decided here.
    /// </summary>
    public static class WindowsEvents
    {
        public const string ApplicationLog = "Application";
        public const string SystemLog = "System";
        public const int Days = 7;
        public const int PerLog = 20;

        /// <summary>An Application log entry is taken when its text names one of these.</summary>
        public static readonly IReadOnlyList<string> Programs = new[] { "DOOMEternalx64vk.exe", "EternalVR.Launcher.exe", "EternalVR.dll" };

        private static readonly KeyValuePair<string, int>[] ApplicationEvents =
        {
            new KeyValuePair<string, int>("Application Error", 1000),
            new KeyValuePair<string, int>("Windows Error Reporting", 1001),
            new KeyValuePair<string, int>("Application Hang", 1002),
        };

        /// <summary>A display driver reset ("Display driver ... stopped responding and has successfully recovered").</summary>
        private const string DisplaySource = "Display";
        private const int DisplayResetId = 4101;

        /// <summary>The display drivers' sources whose errors are taken: NVIDIA, AMD, and Intel's (<c>igfx</c>, <c>igfxn</c> ...).</summary>
        private static readonly string[] DriverSources = { "nvlddmkm", "amdkmdag" };
        private const string IntelDriverPrefix = "igfx";

        /// <summary>The logs read, in the order they are written.</summary>
        public static readonly IReadOnlyList<string> Logs = new[] { ApplicationLog, SystemLog };

        /// <summary>
        /// The event log query (XPath) of a log: the entries that may be taken, of the last <see cref="Days"/> days.
        /// The System query takes every error so that any <c>igfx*</c> source is seen; <see cref="IsCandidate"/> narrows it.
        /// </summary>
        public static string Query(string log)
        {
            const long ms = Days * 24L * 3600 * 1000;
            var time = "TimeCreated[timediff(@SystemTime) <= " + ms.ToString(CultureInfo.InvariantCulture) + "]";
            if (log == ApplicationLog)
            {
                var providers = string.Join(" or ", ApplicationEvents.Select(e => "@Name='" + e.Key + "'"));
                var ids = string.Join(" or ", ApplicationEvents.Select(e => "EventID=" + e.Value.ToString(CultureInfo.InvariantCulture)));
                return "*[System[Provider[" + providers + "] and (" + ids + ") and " + time + "]]";
            }
            return "*[System[(EventID=" + DisplayResetId.ToString(CultureInfo.InvariantCulture) + " or Level=1 or Level=2) and " + time + "]]";
        }

        /// <summary>Whether an entry's log, source, ID and level make it one to take (before its text is looked at).</summary>
        public static bool IsCandidate(string log, string source, int id, int level)
        {
            if (source == null) return false;
            if (log == ApplicationLog)
                return ApplicationEvents.Any(e => string.Equals(e.Key, source, StringComparison.OrdinalIgnoreCase) && e.Value == id);
            if (log != SystemLog) return false;
            if (string.Equals(source, DisplaySource, StringComparison.OrdinalIgnoreCase) && id == DisplayResetId) return true;
            bool driver = DriverSources.Any(d => string.Equals(d, source, StringComparison.OrdinalIgnoreCase))
                || source.StartsWith(IntelDriverPrefix, StringComparison.OrdinalIgnoreCase);
            return driver && (level == 1 || level == 2);
        }

        /// <summary>Whether an entry is taken: a candidate, and for the Application log one whose text names a program of <see cref="Programs"/>.</summary>
        public static bool Matches(WindowsEvent e)
        {
            if (e == null || !IsCandidate(e.Log, e.Source, e.Id, e.Level)) return false;
            if (e.Log != ApplicationLog) return true;
            return e.Message != null && Programs.Any(p => e.Message.IndexOf(p, StringComparison.OrdinalIgnoreCase) >= 0);
        }

        /// <summary>The entries taken of one log: matching, of the last <see cref="Days"/> days before <paramref name="now"/>, newest first, at most <see cref="PerLog"/>.</summary>
        public static IReadOnlyList<WindowsEvent> Select(IEnumerable<WindowsEvent> events, DateTime now)
        {
            var since = now.AddDays(-Days);
            return (events ?? Enumerable.Empty<WindowsEvent>())
                .Where(e => Matches(e) && e.Time >= since)
                .OrderByDescending(e => e.Time)
                .Take(PerLog)
                .ToList();
        }

        public static string LevelName(int level)
        {
            switch (level)
            {
                case 1: return "Critical";
                case 2: return "Error";
                case 3: return "Warning";
                case 5: return "Verbose";
                default: return "Information";
            }
        }

        /// <summary>The text of <see cref="ReportManifest.WindowsEventsFile"/>; <paramref name="logs"/> null means the logs were not read.</summary>
        public static string Format(IReadOnlyList<WindowsEventLogRead> logs, DateTime now)
        {
            var sb = new StringBuilder();
            sb.Append("Windows event log entries since ").Append(now.AddDays(-Days).ToString("yyyy-MM-dd HH:mm", CultureInfo.InvariantCulture))
              .Append(" (the last ").Append(Days).Append(" days), newest first, at most ").Append(PerLog).Append(" per log.\n");
            sb.Append("Application log: crashes (Application Error 1000), crash reports (Windows Error Reporting 1001) and hangs (Application Hang 1002) naming ")
              .Append(string.Join(", ", Programs)).Append(".\n");
            sb.Append("System log: display driver resets (Display 4101) and errors of the display drivers (nvlddmkm, amdkmdag, igfx).\n");
            if (logs == null) return sb.Append("\nThe event logs were not read.\n").ToString();

            foreach (var read in logs)
            {
                var taken = Select(read.Events, now);
                sb.Append('\n').Append(read.Log).Append(": ")
                  .Append(taken.Count == 0 ? "none" : taken.Count.ToString(CultureInfo.InvariantCulture) + (taken.Count == 1 ? " entry" : " entries")).Append('\n');
                if (read.Error != null) sb.Append("  could not be read: ").Append(read.Error).Append('\n');
                foreach (var e in taken)
                {
                    sb.Append('\n').Append(e.Time.ToString("yyyy-MM-dd HH:mm:ss", CultureInfo.InvariantCulture)).Append("  ").Append(e.Log)
                      .Append("  ").Append(e.Source).Append("  ").Append(e.Id.ToString(CultureInfo.InvariantCulture)).Append("  ").Append(LevelName(e.Level)).Append('\n');
                    var message = (e.Message ?? string.Empty).Replace("\r\n", "\n").Replace('\r', '\n').Trim();
                    if (message.Length == 0) message = "(no message text)";
                    foreach (var line in message.Split('\n').Select(l => l.TrimEnd()))
                        sb.Append(line.Length == 0 ? string.Empty : "    ").Append(line).Append('\n');
                }
            }
            return sb.ToString();
        }
    }
}
