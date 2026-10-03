using System;

namespace EternalVR.Launcher.Core.Safety
{
    /// <summary>
    /// A launcher being opened while the session finisher holds the data folder's lock. The finisher completes the
    /// restore of a launcher closed mid-session once the game has exited (a few seconds), then deletes the session marker
    /// and lets go of the lock. While the marker exists and no game runs, the holder is most likely the finisher: the lock
    /// is tried again for a while before another launcher is assumed. The finisher deletes the marker just before it
    /// lets go, so once the marker is seen gone the lock is tried once more after a short pause.
    /// </summary>
    public static class FinisherWait
    {
        public const int StepMs = 500;
        public const int MaxWaitMs = 15000;

        /// <summary>The lock from <paramref name="tryAcquire"/> (null when it is held), waiting for a finisher as above.</summary>
        public static T Acquire<T>(Func<T> tryAcquire, Func<bool> markerExists, Func<bool> gameRunning, Action<int> sleep, int maxWaitMs = MaxWaitMs)
            where T : class
        {
            var got = tryAcquire();
            if (got != null) return got;
            bool sawMarker = false;
            for (int waited = 0; got == null && waited < maxWaitMs; waited += StepMs)
            {
                if (!markerExists())
                {
                    if (!sawMarker) return null;
                    // The finisher is done or nearly so: between deleting the marker and letting go of the lock.
                    sleep(StepMs);
                    return tryAcquire();
                }
                sawMarker = true;
                if (gameRunning()) return null;
                sleep(StepMs);
                got = tryAcquire();
            }
            return got;
        }
    }
}
