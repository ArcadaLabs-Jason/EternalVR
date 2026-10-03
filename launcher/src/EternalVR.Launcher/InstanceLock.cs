using System;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using EternalVR.Launcher.Core.Game;

namespace EternalVR.Launcher
{
    /// <summary>
    /// One launcher per data folder. Two launchers on one data folder would share <c>SESSION_PENDING</c>:
    /// one could restore settings while the other's game runs, or delete the other's marker. A named
    /// mutex per data folder (per logon session) keeps the second one out; a mutex left by a killed
    /// launcher is abandoned and taken over.
    /// </summary>
    public sealed class InstanceLock : IDisposable
    {
        private Mutex mutex;

        private InstanceLock(Mutex mutex) { this.mutex = mutex; }

        public static string MutexName(string dataRoot)
        {
            using (var sha = SHA256.Create())
                return @"Local\EternalVR.Launcher." + KnownBuilds.ToHex(sha.ComputeHash(Encoding.UTF8.GetBytes(dataRoot.TrimEnd('\\').ToUpperInvariant()))).Substring(0, 32);
        }

        /// <summary>The lock, or null when another launcher holds it.</summary>
        public static InstanceLock TryAcquire(string dataRoot)
        {
            var m = new Mutex(false, MutexName(dataRoot));
            bool owned;
            try { owned = m.WaitOne(0); }
            catch (AbandonedMutexException) { owned = true; }
            if (owned) return new InstanceLock(m);
            m.Dispose();
            return null;
        }

        /// <summary>
        /// <see cref="TryAcquire"/>, waiting for a session finisher that holds the lock while it completes a restore
        /// (<see cref="Core.Safety.FinisherWait"/>) before another launcher is assumed.
        /// </summary>
        public static InstanceLock TryAcquireAfterFinisher(string dataRoot, string sessionMarker, Func<bool> gameRunning) =>
            Core.Safety.FinisherWait.Acquire(() => TryAcquire(dataRoot), () => System.IO.File.Exists(sessionMarker), gameRunning, Thread.Sleep);

        public void Dispose()
        {
            if (mutex == null) return;
            try { mutex.ReleaseMutex(); }
            catch (ApplicationException) { }
            mutex.Dispose();
            mutex = null;
        }
    }
}
