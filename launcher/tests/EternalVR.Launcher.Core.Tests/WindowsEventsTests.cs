using System;
using System.Linq;
using EternalVR.Launcher.Core.Report;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    public class WindowsEventsTests
    {
        private static readonly DateTime Now = new DateTime(2026, 9, 27, 12, 0, 0);
        private const string App = WindowsEvents.ApplicationLog;
        private const string Sys = WindowsEvents.SystemLog;

        private static WindowsEvent E(string log, string source, int id, int level, string message = "", double hoursAgo = 1) =>
            new WindowsEvent(Now.AddHours(-hoursAgo), log, source, id, level, message);

        [Fact]
        public void TheQueriesAskForTheLastWeek()
        {
            var app = WindowsEvents.Query(App);
            Assert.Equal("*[System[Provider[@Name='Application Error' or @Name='Windows Error Reporting' or @Name='Application Hang'] "
                + "and (EventID=1000 or EventID=1001 or EventID=1002) and TimeCreated[timediff(@SystemTime) <= 604800000]]]", app);
            Assert.Equal("*[System[(EventID=4101 or Level=1 or Level=2) and TimeCreated[timediff(@SystemTime) <= 604800000]]]", WindowsEvents.Query(Sys));
        }

        [Fact]
        public void ApplicationEntriesAreTakenWhenTheyNameTheGameTheLauncherOrTheLayer()
        {
            Assert.True(WindowsEvents.Matches(E(App, "Application Error", 1000, 2, "Faulting application name: DOOMEternalx64vk.exe, version: 1.0.0.0")));
            Assert.True(WindowsEvents.Matches(E(App, "Application Error", 1000, 2, "Faulting module name: eternalvr.dll, version: 0.1.8.0")));
            Assert.True(WindowsEvents.Matches(E(App, "Windows Error Reporting", 1001, 4, "P1: EternalVR.Launcher.exe")));
            Assert.True(WindowsEvents.Matches(E(App, "application hang", 1002, 2, "The program DOOMEternalx64vk.exe version 1.0 stopped interacting")));
            Assert.False(WindowsEvents.Matches(E(App, "Application Error", 1000, 2, "Faulting application name: explorer.exe")));
            Assert.False(WindowsEvents.Matches(E(App, "Application Error", 1000, 2, null)));
            Assert.False(WindowsEvents.Matches(E(App, "Application Error", 1001, 2, "DOOMEternalx64vk.exe")));
            Assert.False(WindowsEvents.Matches(E(App, "MsiInstaller", 1000, 2, "DOOMEternalx64vk.exe")));
            Assert.False(WindowsEvents.Matches(null));
        }

        [Fact]
        public void SystemEntriesAreDisplayResetsAndDisplayDriverErrors()
        {
            Assert.True(WindowsEvents.Matches(E(Sys, "Display", 4101, 3, "Display driver nvlddmkm stopped responding and has successfully recovered.")));
            Assert.True(WindowsEvents.Matches(E(Sys, "nvlddmkm", 153, 2)));
            Assert.True(WindowsEvents.Matches(E(Sys, "amdkmdag", 4101, 1)));
            Assert.True(WindowsEvents.Matches(E(Sys, "igfxn", 5, 2)));
            Assert.True(WindowsEvents.Matches(E(Sys, "igfx", 5, 2)));
            Assert.False(WindowsEvents.Matches(E(Sys, "nvlddmkm", 153, 3)));
            Assert.False(WindowsEvents.Matches(E(Sys, "Display", 4102, 3)));
            Assert.False(WindowsEvents.Matches(E(Sys, "disk", 7, 2)));
            Assert.False(WindowsEvents.Matches(E(Sys, null, 4101, 3)));
            Assert.False(WindowsEvents.Matches(E("Security", "Display", 4101, 3)));
            Assert.False(WindowsEvents.Matches(E(App, "Display", 4101, 3, "DOOMEternalx64vk.exe")));
        }

        [Fact]
        public void TheLastWeekIsTakenNewestFirstAtMostTwentyPerLog()
        {
            var events = Enumerable.Range(0, 25).Select(i => E(Sys, "nvlddmkm", 13, 2, "reset " + i, hoursAgo: i + 1))
                .Concat(new[] { E(Sys, "nvlddmkm", 13, 2, "last week", hoursAgo: 24 * 7 + 1), E(Sys, "disk", 7, 2, "not taken") })
                .Reverse()
                .ToList();
            var taken = WindowsEvents.Select(events, Now);
            Assert.Equal(WindowsEvents.PerLog, taken.Count);
            Assert.Equal(Enumerable.Range(0, 20).Select(i => "reset " + i), taken.Select(e => e.Message));
            Assert.Single(WindowsEvents.Select(new[] { E(Sys, "Display", 4101, 3, hoursAgo: 24 * 7) }, Now)); // exactly a week ago counts
            Assert.Empty(WindowsEvents.Select(null, Now));
        }

        [Fact]
        public void TheFileListsEachLogWithItsEntriesOrWhyItWasNotRead()
        {
            var text = WindowsEvents.Format(new[]
            {
                new WindowsEventLogRead(App, new[]
                {
                    E(App, "Application Error", 1000, 2, "Faulting application name: DOOMEternalx64vk.exe\r\nException code: 0xc0000374\r\n\r\nFaulting module path: C:\\WINDOWS\\SYSTEM32\\ntdll.dll  ", hoursAgo: 2),
                    E(App, "Application Hang", 1002, 2, "DOOMEternalx64vk.exe stopped interacting", hoursAgo: 1),
                }, null),
                new WindowsEventLogRead(Sys, null, "EventLogException: The RPC server is unavailable."),
            }, Now);
            Assert.StartsWith("Windows event log entries since 2026-09-20 12:00 (the last 7 days), newest first, at most 20 per log.\n", text);
            Assert.Contains("\nApplication: 2 entries\n\n2026-09-27 11:00:00  Application  Application Hang  1002  Error\n    DOOMEternalx64vk.exe stopped interacting\n", text);
            Assert.Contains("\n2026-09-27 10:00:00  Application  Application Error  1000  Error\n    Faulting application name: DOOMEternalx64vk.exe\n"
                + "    Exception code: 0xc0000374\n\n    Faulting module path: C:\\WINDOWS\\SYSTEM32\\ntdll.dll\n", text);
            Assert.EndsWith("\nSystem: none\n  could not be read: EventLogException: The RPC server is unavailable.\n", text);
            Assert.DoesNotContain("\r", text);

            Assert.EndsWith("\nThe event logs were not read.\n", WindowsEvents.Format(null, Now));
            var one = WindowsEvents.Format(new[] { new WindowsEventLogRead(Sys, new[] { E(Sys, "nvlddmkm", 13, 2, " \r\n ") }, null) }, Now);
            Assert.Contains("\nSystem: 1 entry\n", one);
            Assert.Contains("  System  nvlddmkm  13  Error\n    (no message text)\n", one);
        }

        [Theory]
        [InlineData(1, "Critical")]
        [InlineData(2, "Error")]
        [InlineData(3, "Warning")]
        [InlineData(4, "Information")]
        [InlineData(0, "Information")]
        [InlineData(5, "Verbose")]
        public void LevelNames(int level, string name) => Assert.Equal(name, WindowsEvents.LevelName(level));
    }
}
