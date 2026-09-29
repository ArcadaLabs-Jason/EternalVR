using System.IO;
using System.Linq;
using EternalVR.Launcher.Core;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    public class SessionRatesTests
    {
        private static string Rates(double pairs, double xr) =>
            $"[  120.000] [34364] rates: game {pairs * 2:0.0} present(s)/s, {pairs:0.0} tick(s)/s, {pairs:0.0} stereo pair(s)/s shown; XR {xr:0.0} frame(s)/s, {pairs:0.0} new image(s)/s";

        [Fact]
        public void PlayWindowsGiveTheRealRateBesideTheHeadsetsRate()
        {
            // The owner's session of 2026-09-28 13:53 in short: menus and loads (0 pairs) around play at 63-87 pairs/s, 90 Hz.
            var lines = new[] { Rates(0, 90), Rates(72, 90), Rates(63, 90), Rates(87, 90), Rates(74, 90), Rates(0.4, 90), "[  1.0] [1] other" };
            var r = SessionRates.FromLines(lines);
            Assert.NotNull(r);
            Assert.Equal(4, r.Windows);
            Assert.Equal(73, r.PairsMedian, 3);
            Assert.Equal(63, r.PairsLow, 3);
            Assert.Equal(90, r.HeadsetMedian, 3);
            Assert.Contains("about 73 new frames a second", r.Describe());
            Assert.Contains("the headset ran at 90", r.Describe());
        }

        [Fact]
        public void TooLittlePlayOrNoStereoGivesNoSummary()
        {
            Assert.Null(SessionRates.FromLines(new[] { Rates(80, 90), Rates(81, 90) }));
            Assert.Null(SessionRates.FromLines(new[] { Rates(0, 90), Rates(0, 90), Rates(0, 90), Rates(0, 90) }));
            Assert.Null(SessionRates.FromLines(null));
        }

        [Fact]
        public void TheSlowestTenthIsTheTenthPercentile()
        {
            var lines = Enumerable.Range(1, 20).Select(i => Rates(50 + i, 90)).ToArray(); // 51..70
            var r = SessionRates.FromLines(lines);
            Assert.Equal(20, r.Windows);
            Assert.Equal(53, r.PairsLow, 3); // index floor(20 * 0.1) = 2
            Assert.Equal(60.5, r.PairsMedian, 3);
        }

        [Fact]
        public void ReadsTheLayerLogsOfASessionFolderEvenWhileOpen()
        {
            using (var dir = new TempDir())
            {
                var log = Path.Combine(dir.Path, "eternalvr-20260928-135303-1234.log");
                File.WriteAllLines(log, new[] { Rates(70, 90), Rates(72, 90), Rates(74, 90) });
                File.WriteAllText(Path.Combine(dir.Path, "eternalvr-status.txt"), Rates(10, 90)); // not a layer log
                using (new FileStream(log, FileMode.Open, FileAccess.ReadWrite, FileShare.ReadWrite))
                {
                    var r = SessionRates.FromSessionDir(dir.Path);
                    Assert.NotNull(r);
                    Assert.Equal(72, r.PairsMedian, 3);
                }
                Assert.Null(SessionRates.FromSessionDir(Path.Combine(dir.Path, "missing")));
            }
        }
    }
}
