using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using EternalVR.Launcher.Core.Launch;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>
    /// The launcher's own command lines (the game's, the session finisher's, the restart after an update) are joined with
    /// <see cref="LaunchPlan.QuoteIfNeeded"/>: every argument must come back whole when Windows splits the line again.
    /// </summary>
    public class CommandLineQuotingTests
    {
        public static IEnumerable<object[]> Arguments() => new[]
        {
            new object[] { new[] { "--data-root", @"E:\EVR Data\", "--finish-session" } },
            new object[] { new[] { "--data-root", @"E:\EVR Data\\", "--finish-session" } },
            new object[] { new[] { "--data-root", @"E:\", "--finish-session" } },
            new object[] { new[] { @"a ""b"" c", @"a\""b", @"ends with a quote""", @"""" } },
            new object[] { new[] { @"C:\Program Files\x.json", "", "+m_sensitivity", "3" } },
            new object[] { new[] { "tab\there", @"\\server\share\dir with space\", @"trailing\\\" } },
        };

        [Theory]
        [MemberData(nameof(Arguments))]
        public void EveryArgumentComesBackWhole(string[] args)
        {
            var line = string.Join(" ", args.Select(LaunchPlan.QuoteIfNeeded));
            Assert.Equal(args, Split(line));
            if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows)) Assert.Equal(args, SplitByWindows(line));
        }

        [Fact]
        public void AFolderEndingInABackslashKeepsItsClosingQuote()
        {
            // The finisher's old quoting gave "E:\EVR Data\", which Windows reads as one argument running into the next.
            Assert.Equal(@"""E:\EVR Data\\""", LaunchPlan.QuoteIfNeeded(@"E:\EVR Data\"));
            Assert.Equal(new[] { "--data-root", @"E:\EVR Data\", "--finish-session" }, Split(@"--data-root ""E:\EVR Data\\"" --finish-session"));
            Assert.Equal(new[] { "--data-root", @"E:\EVR Data"" --finish-session" }, Split(@"--data-root ""E:\EVR Data\"" --finish-session"));
        }

        /// <summary>The CommandLineToArgvW rules for the arguments after the program name.</summary>
        private static string[] Split(string line)
        {
            var args = new List<string>();
            var sb = new System.Text.StringBuilder();
            bool quoted = false, any = false;
            for (int i = 0; i < line.Length; i++)
            {
                char c = line[i];
                if (c == '\\')
                {
                    int n = 0;
                    while (i < line.Length && line[i] == '\\') { n++; i++; }
                    if (i < line.Length && line[i] == '"')
                    {
                        sb.Append('\\', n / 2);
                        if (n % 2 == 1) sb.Append('"');
                        else quoted = !quoted;
                    }
                    else
                    {
                        sb.Append('\\', n);
                        i--;
                    }
                    any = true;
                    continue;
                }
                if (c == '"')
                {
                    // Inside quotes, "" is one literal quote.
                    if (quoted && i + 1 < line.Length && line[i + 1] == '"') { sb.Append('"'); i++; }
                    else quoted = !quoted;
                    any = true;
                    continue;
                }
                if (!quoted && (c == ' ' || c == '\t'))
                {
                    if (any) args.Add(sb.ToString());
                    sb.Clear();
                    any = false;
                    continue;
                }
                sb.Append(c);
                any = true;
            }
            if (any) args.Add(sb.ToString());
            return args.ToArray();
        }

        private static string[] SplitByWindows(string line)
        {
            var argv = CommandLineToArgvW("x.exe " + line, out int count);
            try
            {
                var all = new string[count];
                for (int i = 0; i < count; i++) all[i] = Marshal.PtrToStringUni(Marshal.ReadIntPtr(argv, i * IntPtr.Size));
                return all.Skip(1).ToArray();
            }
            finally
            {
                LocalFree(argv);
            }
        }

        [DllImport("shell32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr CommandLineToArgvW(string cmdLine, out int numArgs);

        [DllImport("kernel32.dll")]
        private static extern IntPtr LocalFree(IntPtr mem);
    }
}
