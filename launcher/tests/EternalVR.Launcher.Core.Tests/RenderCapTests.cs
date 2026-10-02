using System.IO;
using System.Linq;
using EternalVR.Launcher.Core.Headsets;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Preflight;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>The last session's eye size against the planned one (<see cref="RenderCap"/>).</summary>
    public class RenderCapTests
    {
        // A status file from a session on a driver without present scaling (the layer's render and render_planned keys).
        private const string CappedStatus = "state=vr\nreason=the headset shows the game\nstereo=on\nversion=0.1.11\npid=1\n"
            + "recommended=2016x2112\nrender=958x1009\nrender_planned=2016x2112\nrender_capped=1\nrefresh_hz=90.0\n";

        [Fact]
        public void TheStatusFileGivesTheRealAndThePlannedSize()
        {
            var cap = RenderCap.FromStatus(CappedStatus);
            Assert.NotNull(cap);
            Assert.Equal(new Extent(958, 1009), cap.Real);
            Assert.Equal(new Extent(2016, 2112), cap.Planned);
            Assert.True(cap.Capped);
            // 958 / 2016 = 47.5%, 1009 / 2112 = 47.8%: the smaller side, rounded down.
            Assert.Equal(47, cap.Percent);
        }

        [Fact]
        public void TheWarningIsPlain()
        {
            var cap = RenderCap.FromStatus(CappedStatus);
            Assert.Equal("Your graphics driver cannot render above the window size, so each eye rendered at 958 x 1009, 47% of the planned "
                + "2016 x 2112. A larger display helps; a full fix is being worked on.", cap.Warning());
            Assert.DoesNotContain("\u2014", cap.Warning());
            Assert.Equal("958 x 1009 last session, 47% of the planned 2016 x 2112\n"
                + "Your graphics driver renders at the window's size", cap.EachEyeText());
        }

        [Fact]
        public void AtThePlannedSizeItIsNotCapped()
        {
            var cap = RenderCap.FromStatus("render=1280x1400\r\nrender_planned=1280x1400\r\nrender_capped=0\r\n");
            Assert.NotNull(cap);
            Assert.False(cap.Capped);
            Assert.Equal(100, cap.Percent);
            // The ROG Ally's case: one side short is capped.
            Assert.True(RenderCap.FromStatus("render=672x701\nrender_planned=672x720\n").Capped);
        }

        [Fact]
        public void WithoutAPlanThereIsNothingToCompare()
        {
            Assert.Null(RenderCap.FromStatus("state=vr\nrender=1280x720\n")); // render size off, or an older layer
            Assert.Null(RenderCap.FromStatus("render_planned=2016x2112\n"));  // VR never came up
            Assert.Null(RenderCap.FromStatus("render=0x0\nrender_planned=2016x2112\n"));
            Assert.Null(RenderCap.FromStatus(null));
        }

        [Fact]
        public void ACappedSessionIsRememberedAndAFullOneForgetsIt()
        {
            using (var dir = new TempDir())
            {
                var path = dir.Combine("render-cap.txt");
                Assert.Null(RenderCap.Load(path));
                RenderCap.Remember(path, RenderCap.FromStatus(CappedStatus));
                var loaded = RenderCap.Load(path);
                Assert.NotNull(loaded);
                Assert.Equal(new Extent(958, 1009), loaded.Real);
                Assert.Equal(new Extent(2016, 2112), loaded.Planned);
                RenderCap.Remember(path, new RenderCap(new Extent(2016, 2112), new Extent(2016, 2112)));
                Assert.False(File.Exists(path));
                Assert.Null(RenderCap.Load(path));
            }
        }

        [Fact]
        public void TheEachEyeLineSaysWhatTheLastSessionGot()
        {
            var cap = RenderCap.FromStatus(CappedStatus);
            var s = new LauncherSettings { RenderSize = "auto" };
            Assert.StartsWith(cap.EachEyeText() + "\n", HeadsetView.EachEye(s, null, null, cap));
            // Not capped: the plan as before.
            Assert.Equal("Set from your headset at Launch VR",
                HeadsetView.EachEye(s, null, null, new RenderCap(new Extent(2016, 2112), new Extent(2016, 2112))));
            // The render size off: the window's size whatever the last session got.
            Assert.Equal("The game window's size (render_size off)", HeadsetView.EachEye(new LauncherSettings { RenderSize = "off" }, null, null, cap));
        }

        [Fact]
        public void PreflightWarnsAfterACappedSession()
        {
            var facts = new PreflightFacts { LastRenderCap = RenderCap.FromStatus(CappedStatus) };
            var check = PreflightEvaluator.Evaluate(facts).Checks.Single(c => c.Id == "render-size");
            Assert.Equal(Severity.Warn, check.Severity);
            Assert.StartsWith("Last session: Your graphics driver cannot render above the window size", check.Message);
            Assert.DoesNotContain(PreflightEvaluator.Evaluate(new PreflightFacts()).Checks, c => c.Id == "render-size");
        }
    }
}
