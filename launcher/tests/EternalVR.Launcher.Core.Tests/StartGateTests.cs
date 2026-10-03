using System;
using System.Threading;
using EternalVR.Launcher.Core.Launch;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    public class StartGateTests
    {
        [Fact]
        public void AnOpenGateRunsTheStart()
        {
            var gate = new StartGate();
            int starts = 0;
            Assert.True(gate.Run(() => starts++));
            Assert.Equal(1, starts);
            Assert.False(gate.IsClosed);
        }

        [Fact]
        public void NoGameIsStartedOnceTheWindowClosed()
        {
            var gate = new StartGate();
            gate.Close();
            int starts = 0;
            Assert.False(gate.Run(() => starts++));
            Assert.Equal(0, starts);
            Assert.True(gate.IsClosed);
        }

        /// <summary>The session's launch as SessionRunner runs it, with each step recorded.</summary>
        private static (LaunchOutcome Outcome, string Steps) Launch(StartGate gate, CancellationToken cancel, Action duringPrepare = null)
        {
            var steps = new System.Text.StringBuilder();
            var outcome = gate.Launch(cancel, () => { steps.Append("marker,snapshot;"); duringPrepare?.Invoke(); }, () => steps.Append("game,finisher;"));
            return (outcome, steps.ToString());
        }

        [Fact]
        public void ALaunchPreparesThenStarts()
        {
            var r = Launch(new StartGate(), CancellationToken.None);
            Assert.Equal(LaunchOutcome.Started, r.Outcome);
            Assert.Equal("marker,snapshot;game,finisher;", r.Steps);
        }

        [Fact]
        public void ACancelBeforeTheMarkerPreparesNothing()
        {
            using (var cancel = new CancellationTokenSource())
            {
                cancel.Cancel();
                var r = Launch(new StartGate(), cancel.Token);
                Assert.Equal(LaunchOutcome.StoppedBeforePreparing, r.Outcome);
                Assert.Equal(string.Empty, r.Steps);
            }
            var closed = new StartGate();
            closed.Close();
            Assert.Equal(LaunchOutcome.StoppedBeforePreparing, Launch(closed, CancellationToken.None).Outcome);
        }

        [Fact]
        public void ACancelDuringThePreparationStartsNoGame()
        {
            // The window closed while the snapshot and save backup were taken: the caller undoes them, no game starts.
            using (var cancel = new CancellationTokenSource())
            {
                var r = Launch(new StartGate(), cancel.Token, cancel.Cancel);
                Assert.Equal(LaunchOutcome.StoppedAfterPreparing, r.Outcome);
                Assert.Equal("marker,snapshot;", r.Steps);
            }
            var gate = new StartGate();
            var closedMeanwhile = Launch(gate, CancellationToken.None, gate.Close);
            Assert.Equal(LaunchOutcome.StoppedAfterPreparing, closedMeanwhile.Outcome);
            Assert.Equal("marker,snapshot;", closedMeanwhile.Steps);
        }

        [Fact]
        public void ClosingWaitsForAStartUnderWayToStartTheFinisherToo()
        {
            var gate = new StartGate();
            using (var gameStarted = new ManualResetEventSlim(false))
            using (var letFinish = new ManualResetEventSlim(false))
            {
                bool finisherStarted = false, ran = false;
                var session = new Thread(() => ran = gate.Run(() =>
                {
                    gameStarted.Set();
                    letFinish.Wait(5000);
                    finisherStarted = true;
                }));
                session.Start();
                Assert.True(gameStarted.Wait(5000));

                var closing = new Thread(gate.Close);
                closing.Start();
                // The process may not end while the game runs without its finisher.
                Assert.False(closing.Join(200));
                letFinish.Set();
                Assert.True(closing.Join(5000));
                Assert.True(finisherStarted);
                Assert.True(session.Join(5000));
                Assert.True(ran);

                int later = 0;
                Assert.False(gate.Run(() => later++));
                Assert.Equal(0, later);
            }
        }
    }
}
