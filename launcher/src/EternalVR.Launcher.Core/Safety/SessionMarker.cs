using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;

namespace EternalVR.Launcher.Core.Safety
{
    public enum SessionState
    {
        /// <summary>Marker written, snapshot not yet complete: nothing was launched.</summary>
        Preparing,
        /// <summary>Snapshot complete; the game may have been started.</summary>
        Snapshotted,
        /// <summary>The game was started; <see cref="SessionMarker.GamePid"/> is set.</summary>
        Running,
    }

    /// <summary>
    /// <c>SESSION_PENDING</c> in the data folder: written before any change and deleted only after a
    /// verified restore, so a launcher or game crash is finished at the next launcher start (T-036, T-093).
    /// </summary>
    public sealed class SessionMarker
    {
        public SessionState State { get; set; }
        public string SessionId { get; set; }
        public string SnapshotDir { get; set; }
        public int GamePid { get; set; }
        public DateTime GameStartUtc { get; set; }
        public string GameExe { get; set; }
        /// <summary>The DOOM Eternal Launcher's <c>launch_target</c> before a Game Pass or Store start (empty: none), put back
        /// by the restore if the launcher was closed or ended while the start had it changed.</summary>
        public string BethesdaTarget { get; set; }

        public static SessionMarker Read(string path)
        {
            if (!File.Exists(path)) return null;
            var map = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            foreach (var line in File.ReadAllLines(path))
            {
                int eq = line.IndexOf('=');
                if (eq > 0) map[line.Substring(0, eq).Trim()] = line.Substring(eq + 1).Trim();
            }
            var m = new SessionMarker();
            if (!map.TryGetValue("state", out var state) || !Enum.TryParse(state, true, out SessionState parsed))
                parsed = SessionState.Snapshotted; // unreadable state: assume the worst that still has a snapshot
            m.State = parsed;
            m.SessionId = Get(map, "session");
            m.SnapshotDir = Get(map, "snapshot");
            m.GameExe = Get(map, "exe");
            m.BethesdaTarget = Get(map, "bethesda_target");
            if (int.TryParse(Get(map, "pid"), NumberStyles.Integer, CultureInfo.InvariantCulture, out var pid)) m.GamePid = pid;
            if (long.TryParse(Get(map, "start_utc_ticks"), NumberStyles.Integer, CultureInfo.InvariantCulture, out var ticks) && ticks > 0)
                m.GameStartUtc = new DateTime(ticks, DateTimeKind.Utc);
            return m;
        }

        public void Write(string path)
        {
            var sb = new StringBuilder();
            sb.AppendLine("state = " + State.ToString().ToLowerInvariant());
            sb.AppendLine("session = " + SessionId);
            sb.AppendLine("snapshot = " + SnapshotDir);
            sb.AppendLine("exe = " + GameExe);
            if (!string.IsNullOrEmpty(BethesdaTarget)) sb.AppendLine("bethesda_target = " + BethesdaTarget);
            sb.AppendLine("pid = " + GamePid.ToString(CultureInfo.InvariantCulture));
            sb.AppendLine("start_utc_ticks = " + GameStartUtc.Ticks.ToString(CultureInfo.InvariantCulture));
            Directory.CreateDirectory(Path.GetDirectoryName(path));
            FileUtil.WriteAllTextAtomic(path, sb.ToString());
        }

        /// <summary>
        /// The snapshot to restore from: the one the marker names when complete, else the session's own
        /// folder, else (a marker left empty or damaged by a power loss) the newest complete snapshot, so
        /// a damaged marker never blocks every later launch. Null when there is none.
        /// </summary>
        public string ResolveSnapshotDir(string snapshotsRoot)
        {
            if (!string.IsNullOrEmpty(SnapshotDir) && SettingsSnapshot.IsComplete(SnapshotDir)) return SnapshotDir;
            if (!string.IsNullOrEmpty(SessionId) && SessionId.IndexOfAny(Path.GetInvalidFileNameChars()) < 0)
            {
                var own = Path.Combine(snapshotsRoot, SessionId);
                if (SettingsSnapshot.IsComplete(own)) return own;
            }
            if (string.IsNullOrEmpty(SnapshotDir) && string.IsNullOrEmpty(SessionId))
                return SettingsSnapshot.NewestComplete(snapshotsRoot);
            return null;
        }

        /// <summary>
        /// Deletes the marker only while it still belongs to <paramref name="sessionId"/>: a restore that
        /// finishes late must never remove the marker of a newer session.
        /// </summary>
        public static bool DeleteIfOwned(string path, string sessionId)
        {
            SessionMarker current;
            try { current = Read(path); }
            catch (IOException) { return false; }
            if (current == null) return true;
            if (!string.Equals(current.SessionId ?? string.Empty, sessionId ?? string.Empty, StringComparison.Ordinal)) return false;
            File.Delete(path);
            return true;
        }

        private static string Get(Dictionary<string, string> map, string key) => map.TryGetValue(key, out var v) ? v : string.Empty;
    }

    public enum RecoveryAction
    {
        /// <summary>No marker: nothing to do.</summary>
        None,
        /// <summary>The marker never got past preparation; nothing was changed, delete it.</summary>
        Discard,
        /// <summary>A game process still runs; restore after it exits.</summary>
        WaitForGame,
        /// <summary>Restore the snapshot now, then delete the marker.</summary>
        Restore,
    }

    public static class SessionRecovery
    {
        /// <summary>
        /// What the launcher does with a marker it finds at start or after the game exits. Any running
        /// game process defers the restore: the game would write its config again on exit.
        /// </summary>
        public static RecoveryAction Decide(SessionMarker marker, bool anyGameProcessRunning)
        {
            if (marker == null) return RecoveryAction.None;
            if (marker.State == SessionState.Preparing) return RecoveryAction.Discard;
            if (anyGameProcessRunning) return RecoveryAction.WaitForGame;
            return RecoveryAction.Restore;
        }
    }
}
