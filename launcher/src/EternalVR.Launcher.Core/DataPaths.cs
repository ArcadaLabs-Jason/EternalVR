using System;
using System.IO;

namespace EternalVR.Launcher.Core
{
    /// <summary>
    /// The user data folder (T-093): <c>%LOCALAPPDATA%\EternalVR\</c> in normal use. Everything the
    /// launcher writes goes under it; the program folder is treated as read-only.
    /// </summary>
    public sealed class DataPaths
    {
        public DataPaths(string root)
        {
            if (string.IsNullOrWhiteSpace(root)) throw new ArgumentException("data root is empty", nameof(root));
            Root = Path.GetFullPath(root);
        }

        public static DataPaths ForLocalAppData(string localAppData) => new DataPaths(Path.Combine(localAppData, "EternalVR"));

        public string Root { get; }
        public string SettingsFile => Path.Combine(Root, "launcher.ini");
        public string Logs => Path.Combine(Root, "logs");
        public string Snapshots => Path.Combine(Root, "snapshots");
        public string SaveBackups => Path.Combine(Root, "save-backups");
        /// <summary>The player's controller maps and a copy of the built-in ones (<see cref="Settings.ControlsFolder"/>).</summary>
        public string Controls => Path.Combine(Root, "controls");
        /// <summary>The player profiles (<see cref="Settings.ProfileStore"/>).</summary>
        public string Profiles => Path.Combine(Root, "profiles");
        /// <summary>Present while a VR session's restore has not completed (T-036, T-093).</summary>
        public string SessionMarker => Path.Combine(Root, "SESSION_PENDING");
        /// <summary>Present while an HKCU layer registration may exist (flag-gated route only).</summary>
        public string RegistrationMarker => Path.Combine(Root, "REGISTRATION_PENDING");
        public string LauncherLog => Path.Combine(Logs, "launcher.log");

        public string SessionLogDir(string sessionId) => Path.Combine(Logs, sessionId);

        public void EnsureCreated()
        {
            Directory.CreateDirectory(Root);
            Directory.CreateDirectory(Logs);
            Directory.CreateDirectory(Snapshots);
            Directory.CreateDirectory(SaveBackups);
        }

        public static string NewSessionId(DateTime now) => now.ToString("yyyyMMdd-HHmmss", System.Globalization.CultureInfo.InvariantCulture);

        /// <summary>A session ID whose snapshot, save-backup and log folders do not exist yet.</summary>
        public string UniqueSessionId(DateTime now)
        {
            var id = NewSessionId(now);
            for (int n = 2; Exists(id); n++) id = NewSessionId(now) + "-" + n.ToString(System.Globalization.CultureInfo.InvariantCulture);
            return id;
        }

        private bool Exists(string id) =>
            Directory.Exists(Path.Combine(Snapshots, id)) || Directory.Exists(Path.Combine(SaveBackups, id)) || Directory.Exists(SessionLogDir(id));
    }
}
