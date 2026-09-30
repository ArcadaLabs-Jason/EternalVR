using System;
using System.Collections.Generic;
using System.Diagnostics.Eventing.Reader;
using System.Globalization;
using System.IO;
using System.Linq;
using EternalVR.Launcher.Core.Report;

namespace EternalVR.Launcher.Platform
{
    /// <summary>
    /// Reads the Windows event log entries of a report (read only). Which entries are taken and how they are written
    /// is Core's (<see cref="WindowsEvents"/>); a log that cannot be read is noted in the report, never an error.
    /// </summary>
    public static class WindowsEventLogs
    {
        /// <summary>The most entries looked at per log, newest first (the System query takes every error of the last days).</summary>
        private const int MaxLookedAt = 5000;

        public static IReadOnlyList<WindowsEventLogRead> Read() => WindowsEvents.Logs.Select(Read).ToList();

        private static WindowsEventLogRead Read(string log)
        {
            var taken = new List<WindowsEvent>();
            try
            {
                var query = new EventLogQuery(log, PathType.LogName, WindowsEvents.Query(log)) { ReverseDirection = true };
                using (var reader = new EventLogReader(query))
                {
                    for (int n = 0; n < MaxLookedAt && taken.Count < WindowsEvents.PerLog; n++)
                    {
                        using (var r = reader.ReadEvent())
                        {
                            if (r == null) break;
                            int level = r.Level ?? 0;
                            if (!WindowsEvents.IsCandidate(log, r.ProviderName, r.Id, level)) continue;
                            // The program names are in the entry's data; its text (slow to render) only for the entries taken.
                            var time = r.TimeCreated ?? DateTime.MinValue;
                            if (!WindowsEvents.Matches(new WindowsEvent(time, log, r.ProviderName, r.Id, level, Data(r)))) continue;
                            taken.Add(new WindowsEvent(time, log, r.ProviderName, r.Id, level, Message(r)));
                        }
                    }
                }
                return new WindowsEventLogRead(log, taken, null);
            }
            catch (Exception e) when (e is EventLogException || e is UnauthorizedAccessException || e is InvalidOperationException
                                      || e is IOException || e is ArgumentException || e is PlatformNotSupportedException)
            {
                return new WindowsEventLogRead(log, taken, e.GetType().Name + ": " + e.Message);
            }
        }

        /// <summary>The entry's text as Event Viewer shows it, else its data values (when the source's message file is missing).</summary>
        private static string Message(EventRecord r)
        {
            try
            {
                var text = r.FormatDescription();
                if (!string.IsNullOrWhiteSpace(text)) return text;
            }
            catch (Exception e) when (e is EventLogException || e is InvalidOperationException) { }
            return Data(r);
        }

        /// <summary>The entry's data values, one per line (for Application Error: the program, the faulting module ...).</summary>
        private static string Data(EventRecord r)
        {
            try
            {
                return string.Join("\n", r.Properties.Select(p => Convert.ToString(p.Value, CultureInfo.InvariantCulture)));
            }
            catch (Exception e) when (e is EventLogException || e is InvalidOperationException) { return null; }
        }
    }
}
