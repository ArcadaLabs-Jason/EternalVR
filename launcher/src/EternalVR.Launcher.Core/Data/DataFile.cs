using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace EternalVR.Launcher.Core.Data
{
    /// <summary>
    /// The launcher's data files (<c>data\*.txt</c>): one record per line, fields separated by '|',
    /// '#' starts a comment line, blank lines are ignored. Fields are trimmed.
    /// </summary>
    public static class DataFile
    {
        public static IReadOnlyList<string[]> ParseRecords(string text)
        {
            var records = new List<string[]>();
            foreach (var raw in (text ?? string.Empty).Replace("\r\n", "\n").Split('\n'))
            {
                var line = raw.Trim();
                if (line.Length == 0 || line.StartsWith("#", StringComparison.Ordinal)) continue;
                records.Add(line.Split('|').Select(f => f.Trim()).ToArray());
            }
            return records;
        }

        public static IReadOnlyList<string[]> Load(string path) => ParseRecords(File.ReadAllText(path));

        public static string Field(string[] record, int index) => index < record.Length ? record[index] : string.Empty;
    }
}
