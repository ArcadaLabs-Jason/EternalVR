using EternalVR.Launcher.Core.Launch;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    public class LayerStatusTests
    {
        [Fact]
        public void TheStatusFileParses()
        {
            var s = LayerStatusFile.Parse("state=vr\r\nreason=VR is running\r\nstereo=off: the per-eye hook was not found\r\nversion=0.1.0+abc\r\npid=4242\r\n");
            Assert.Equal(LayerState.Vr, s.State);
            Assert.Equal("VR is running", s.Reason);
            Assert.False(s.Stereo);
            Assert.Equal("the per-eye hook was not found", s.StereoReason);
            Assert.Equal("0.1.0+abc", s.Version);
            Assert.Equal("4242", s.Pid);
        }

        [Fact]
        public void AnUndecidedStereoAndUnknownKeysAreTolerated()
        {
            var s = LayerStatusFile.Parse("state=starting\nstereo=\nfuture=1\nno equals sign\n");
            Assert.Equal(LayerState.Starting, s.State);
            Assert.Null(s.Stereo);
            Assert.Equal(LayerState.Unknown, LayerStatusFile.Parse("state=sideways").State);
            Assert.Equal(LayerState.Unknown, LayerStatusFile.Parse(null).State);
        }

        private static LayerStatusFile File(string text) => LayerStatusFile.Parse(text);

        [Fact]
        public void NothingIsSaidUntilTheLayerLoadsOrTheTimeoutPasses()
        {
            Assert.Null(LayerWatch.Decide(false, null, 5));
            var late = LayerWatch.Decide(false, null, LayerWatch.LoadTimeoutSeconds);
            Assert.Equal(StatusKind.Problem, late.Kind);
            Assert.Contains("did not load", late.Text);
            Assert.True(LayerWatch.NeedsDialog(late));
            Assert.Equal(StatusKind.Info, LayerWatch.Decide(true, null, 100).Kind);
        }

        [Theory]
        [InlineData("state=starting", StatusKind.Info, "VR is starting")]
        [InlineData("state=waiting\nreason=No headset is connected.", StatusKind.Warning, "No headset is connected")]
        [InlineData("state=flat\nreason=This game build is not supported.", StatusKind.Problem, "This game build is not supported")]
        [InlineData("state=vr\nstereo=on", StatusKind.Good, "in stereo")]
        [InlineData("state=vr\nstereo=off: TAA hooks missing", StatusKind.Warning, "TAA hooks missing")]
        [InlineData("state=vr", StatusKind.Good, "VR is running")]
        public void EachStateHasItsMessage(string text, StatusKind kind, string contains)
        {
            var s = LayerWatch.Decide(true, File(text), 10);
            Assert.Equal(kind, s.Kind);
            Assert.Contains(contains, s.Text);
        }

        [Fact]
        public void StartingThatNeverEndsIsNotShownAsStartingForever()
        {
            var early = LayerWatch.Decide(true, File("state=starting"), LayerWatch.StartTimeoutSeconds - 1);
            Assert.Equal(new SessionStatus(StatusKind.Info, LayerWatch.StartingMessage), early);
            foreach (var status in new[] { File("state=starting"), null })
            {
                var late = LayerWatch.Decide(true, status, LayerWatch.StartTimeoutSeconds);
                Assert.Equal(new SessionStatus(StatusKind.Warning, LayerWatch.NotStartedMessage), late);
                Assert.False(LayerWatch.NeedsDialog(late));
            }
            // VR coming up late replaces it.
            Assert.Equal(StatusKind.Good, LayerWatch.Decide(true, File("state=vr\nstereo=on"), 600).Kind);
        }

        [Fact]
        public void AMultiplayerArgumentRefusedByTheLayerIsShownWithItsReason()
        {
            // What the layer writes when the command line asks for multiplayer (mp_guard.cpp, screenCommandLine).
            var s = LayerWatch.Decide(true, File("state=flat\nreason=\"+com_gamemode\" on the command line requests a game mode "
                + "other than the campaign; EternalVR is single-player only"), 3);
            Assert.Equal(StatusKind.Problem, s.Kind);
            Assert.Equal("VR is off for this session: \"+com_gamemode\" on the command line requests a game mode other than the "
                + "campaign; EternalVR is single-player only. The game runs flat.", s.Text);
        }

        [Fact]
        public void TheStatusFileWinsOverTheTimeout()
        {
            // A status file without the marker (a marker removed by a cleaner) still counts.
            Assert.Equal(StatusKind.Good, LayerWatch.Decide(false, File("state=vr\nstereo=on"), 999).Kind);
        }

        [Fact]
        public void AProblemIsHeldUntilItHasStoodFiveSeconds()
        {
            var hold = new ProblemHold();
            var ok = new SessionStatus(StatusKind.Good, "VR is running in stereo.");
            var off = new SessionStatus(StatusKind.Problem, "VR is off for this session: x. The game runs flat.");
            Assert.Same(ok, hold.Offer(ok, 10));
            Assert.Null(hold.Offer(off, 20));
            Assert.Same(off, hold.Pending);
            Assert.Null(hold.Offer(off, 24.9));
            Assert.Same(off, hold.Offer(off, 25));
            // A good status in between starts the hold again.
            Assert.Same(ok, hold.Offer(ok, 26));
            Assert.Null(hold.Pending);
            Assert.Null(hold.Offer(off, 27));
            Assert.Null(hold.Offer(null, 28));
            Assert.Null(hold.Pending);
        }

        [Fact]
        public void StatusesCompareByKindAndText()
        {
            Assert.Equal(new SessionStatus(StatusKind.Info, "a"), new SessionStatus(StatusKind.Info, "a"));
            Assert.NotEqual(new SessionStatus(StatusKind.Info, "a"), new SessionStatus(StatusKind.Warning, "a"));
        }
    }
}
