using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;

namespace EternalVR.Launcher.Core
{
    /// <summary>
    /// How fast the game really drew during a VR session, from the layer's <c>rates:</c> lines (one every 10 s). The layer
    /// hands the headset a frame at the headset's own rate: when the game has no new pair of eye images ready, the newest one
    /// is shown again, turned to follow the head. Headset overlays count differently (some every frame handed over, some only
    /// frames with a new image), so the number that says how the game runs is the pairs of eye images it drew per second.
    /// </summary>
    public sealed class SessionRates
    {
        /// <summary>A 10 s window with fewer new pairs a second than this was a menu, a loading screen or a pause.</summary>
        public const double PlayThreshold = 10.0;
        /// <summary>The fewest windows in play (seconds / 10) worth a summary.</summary>
        public const int MinWindows = 3;

        private static readonly Regex RatesLine = new Regex(
            @"\] rates: game [0-9.]+ present\(s\)/s, [0-9.]+ tick\(s\)/s, (?<pairs>[0-9.]+) stereo pair\(s\)/s shown; XR (?<xr>[0-9.]+) frame\(s\)/s",
            RegexOptions.CultureInvariant);

        private SessionRates(int windows, double pairsMedian, double pairsLow, double headsetMedian)
        {
            Windows = windows;
            PairsMedian = pairsMedian;
            PairsLow = pairsLow;
            HeadsetMedian = headsetMedian;
        }

        /// <summary>The 10 s windows in play.</summary>
        public int Windows { get; }
        /// <summary>New pairs of eye images a second, the median over the windows in play.</summary>
        public double PairsMedian { get; }
        /// <summary>The slowest tenth of the windows in play: at or below this.</summary>
        public double PairsLow { get; }
        /// <summary>Frames a second handed to the headset over the same windows (what a headset's counter shows).</summary>
        public double HeadsetMedian { get; }

        /// <summary>The summary of a layer log's lines; null without enough time in play (or a session without stereo).</summary>
        public static SessionRates FromLines(IEnumerable<string> lines)
        {
            var pairs = new List<double>();
            var headset = new List<double>();
            foreach (var line in lines ?? Enumerable.Empty<string>())
            {
                var m = RatesLine.Match(line ?? string.Empty);
                if (!m.Success) continue;
                if (!double.TryParse(m.Groups["pairs"].Value, NumberStyles.Float, CultureInfo.InvariantCulture, out var p)) continue;
                if (!double.TryParse(m.Groups["xr"].Value, NumberStyles.Float, CultureInfo.InvariantCulture, out var x)) continue;
                if (p < PlayThreshold) continue;
                pairs.Add(p);
                headset.Add(x);
            }
            if (pairs.Count < MinWindows) return null;
            pairs.Sort();
            headset.Sort();
            return new SessionRates(pairs.Count, Median(pairs), pairs[(int)Math.Floor(pairs.Count * 0.1)], Median(headset));
        }

        /// <summary>The summary of the layer logs (<c>eternalvr-*.log</c>) in a session's log folder; null when there is none.</summary>
        public static SessionRates FromSessionDir(string logDir)
        {
            if (string.IsNullOrEmpty(logDir) || !Directory.Exists(logDir)) return null;
            var lines = new List<string>();
            foreach (var file in Directory.GetFiles(logDir, "eternalvr-*.log"))
            {
                try
                {
                    // The game may still hold the file open for writing.
                    using (var stream = new FileStream(file, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
                    using (var reader = new StreamReader(stream))
                    {
                        string line;
                        while ((line = reader.ReadLine()) != null) lines.Add(line);
                    }
                }
                catch (Exception e) when (e is IOException || e is UnauthorizedAccessException) { }
            }
            return FromLines(lines);
        }

        /// <summary>One sentence for the launcher's status line.</summary>
        public string Describe() => string.Format(CultureInfo.InvariantCulture,
            "In play the game drew about {0:0} new frames a second (the slowest tenth {1:0} or less); the headset ran at {2:0}, " +
            "showing the newest frame again, turned to follow your head, until the next one was ready.",
            PairsMedian, PairsLow, HeadsetMedian);

        /// <summary>The numbers for the launcher log.</summary>
        public string LogText() => string.Format(CultureInfo.InvariantCulture,
            "session rates: {0} window(s) of 10 s in play; new stereo pairs/s median {1:0.0}, slowest tenth {2:0.0}; headset frames/s median {3:0.0}",
            Windows, PairsMedian, PairsLow, HeadsetMedian);

        private static double Median(List<double> sorted)
        {
            int n = sorted.Count;
            return n % 2 == 1 ? sorted[n / 2] : (sorted[n / 2 - 1] + sorted[n / 2]) / 2.0;
        }
    }
}
