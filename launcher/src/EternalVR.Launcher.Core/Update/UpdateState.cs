using System;
using System.Globalization;
using System.IO;
using System.Text;
using EternalVR.Launcher.Core.Data;

namespace EternalVR.Launcher.Core.Update
{
    /// <summary>
    /// The update check's own file, <c>updates\state.txt</c> in the data folder (not launcher.ini: it is this machine's
    /// and no profile or Reset touches it): whether to check, a version the player chose to skip, and when it last asked.
    /// </summary>
    public sealed class UpdateState
    {
        /// <summary>How often the launcher asks GitHub at most (GitHub allows 60 anonymous API requests an hour).</summary>
        public static readonly TimeSpan Interval = TimeSpan.FromHours(1);

        public bool Check { get; set; } = true;
        /// <summary>The version the player chose to skip; null for none.</summary>
        public Version Skipped { get; set; }
        /// <summary>The newer version the last check found; null for none (offered again until the next check).</summary>
        public Version Found { get; set; }
        /// <summary>When the launcher last asked (UTC); null for never.</summary>
        public DateTime? LastCheck { get; set; }

        /// <summary>True when checks are on and the last one is older than <see cref="Interval"/> (or never ran).</summary>
        public bool Due(DateTime nowUtc) => Check && (LastCheck == null || nowUtc - LastCheck.Value >= Interval || LastCheck.Value > nowUtc);

        public static UpdateState Load(string path)
        {
            var s = new UpdateState();
            if (!File.Exists(path)) return s;
            foreach (var r in DataFile.ParseRecords(File.ReadAllText(path).Replace('=', '|')))
            {
                var key = DataFile.Field(r, 0).ToLowerInvariant();
                var value = DataFile.Field(r, 1);
                if (key == "check") s.Check = !string.Equals(value, "off", StringComparison.OrdinalIgnoreCase);
                else if (key == "skip" && Version.TryParse(value, out var v)) s.Skipped = v;
                else if (key == "found" && Version.TryParse(value, out var f)) s.Found = f;
                else if (key == "last_check" && DateTime.TryParse(value, CultureInfo.InvariantCulture, DateTimeStyles.AdjustToUniversal | DateTimeStyles.AssumeUniversal, out var t))
                    s.LastCheck = t;
            }
            return s;
        }

        public void Save(string path)
        {
            var sb = new StringBuilder();
            sb.Append("# EternalVR launcher: the update check (Checks and log tab)\n");
            sb.Append("check = ").Append(Check ? "on" : "off").Append('\n');
            if (Skipped != null) sb.Append("skip = ").Append(Skipped).Append('\n');
            if (Found != null) sb.Append("found = ").Append(Found).Append('\n');
            if (LastCheck != null) sb.Append("last_check = ").Append(LastCheck.Value.ToString("yyyy-MM-ddTHH:mm:ssZ", CultureInfo.InvariantCulture)).Append('\n');
            Directory.CreateDirectory(Path.GetDirectoryName(path));
            FileUtil.WriteAllTextAtomic(path, sb.ToString());
        }
    }
}
