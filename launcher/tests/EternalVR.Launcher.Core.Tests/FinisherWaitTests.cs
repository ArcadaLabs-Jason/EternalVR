using System;
using System.Collections.Generic;
using EternalVR.Launcher.Core.Safety;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    public class FinisherWaitTests
    {
        /// <summary>A lock and a marker on a clock that moves only when the launcher sleeps.</summary>
        private sealed class Machine
        {
            public int Now;
            public int LockFreeAt = int.MaxValue;
            public int MarkerGoneAt = int.MaxValue;
            public bool Game;
            public readonly List<int> Tries = new List<int>();

            public object TryAcquire()
            {
                Tries.Add(Now);
                return Now >= LockFreeAt ? new object() : null;
            }

            public bool Marker() => Now < MarkerGoneAt;
            public void Sleep(int ms) => Now += ms;
            public object Acquire() => FinisherWait.Acquire(TryAcquire, Marker, () => Game, Sleep);
        }

        [Fact]
        public void AFreeLockIsTakenAtOnce()
        {
            var m = new Machine { LockFreeAt = 0 };
            Assert.NotNull(m.Acquire());
            Assert.Equal(0, m.Now);
        }

        [Fact]
        public void AnotherLauncherWithoutAPendingRestoreIsNotWaitedFor()
        {
            var m = new Machine { MarkerGoneAt = 0 };
            Assert.Null(m.Acquire());
            Assert.Equal(0, m.Now);
        }

        [Fact]
        public void ALauncherInASessionIsNotWaitedFor()
        {
            var m = new Machine { Game = true };
            Assert.Null(m.Acquire());
            Assert.Equal(0, m.Now);
        }

        [Fact]
        public void TheFinisherIsWaitedForUntilItLetsGo()
        {
            var m = new Machine { MarkerGoneAt = 3000, LockFreeAt = 3000 };
            Assert.NotNull(m.Acquire());
            Assert.Equal(3000, m.Now);
        }

        [Fact]
        public void TheGapBetweenTheMarkersDeleteAndTheLocksReleaseIsCovered()
        {
            // The finisher deletes the marker, then lets go of the lock a moment later.
            var m = new Machine { MarkerGoneAt = 2900, LockFreeAt = 3200 };
            Assert.NotNull(m.Acquire());
            Assert.Equal(3500, m.Now);
        }

        [Fact]
        public void AHolderThatNeverLetsGoIsGivenUpOn()
        {
            var m = new Machine();
            Assert.Null(m.Acquire());
            Assert.Equal(FinisherWait.MaxWaitMs, m.Now);
        }
    }
}
