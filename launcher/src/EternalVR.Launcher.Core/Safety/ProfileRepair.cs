using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;

namespace EternalVR.Launcher.Core.Safety
{
    /// <summary>
    /// The one-time repair of camera shake in the player's DOOM profile (docs/rig-findings/profile-leak.md). Before the layer's
    /// profile hooks, every stereo session held <c>view_skipShakes 1</c> and the game saved it into profile.bin, so the flat
    /// game had no camera shake afterwards. The layer can set it back to 0 at its next profile load
    /// (<c>ETERNALVR_PROFILE_REPAIR=1</c>, vkcore/profile_hooks.hpp); the launcher asks for that once:
    /// <list type="bullet">
    /// <item>only for a stereo launch, only while its marker (<see cref="DataPaths.ProfileRepairMarker"/>) is absent, and only
    /// when a kept session's layer log shows that hold (<c>cvars: held at run time: ... view_skipShakes 1</c>) without the
    /// hooks (no <c>profile cvar serializer hooked</c> line): a fresh install, a player who only played in mono, or one whose
    /// stereo sessions all had the hooks is never touched;</item>
    /// <item>the layer reports <c>profile_repair=done|not_needed|off</c> in its status file after the first profile load in stereo
    /// (<c>done</c> only once a save wrote the 0); the marker is written only after one of them is read, so a session that ends
    /// before the profile load asks again next time. Any of the three settles it: from the first session with the hooks on, no
    /// stereo session leaves a 1 behind.</item>
    /// </list>
    /// r_sharpening is never repaired: its value before VR is not known, and the game's menu shows it. The marker is per PC,
    /// not per profile: a second Steam account, or Steam and Game Pass on one PC, is not repaired once the first settles.
    /// </summary>
    public static class ProfileRepair
    {
        public const string Variable = "ETERNALVR_PROFILE_REPAIR";
        public const string StatusKey = "profile_repair";

        /// <summary>Lines read per layer log at most: the hold's line comes in the first seconds of a session.</summary>
        public const int LinesPerLog = 20000;

        /// <summary>The layer's line once its profile hooks are in (vkcore/profile_hooks.cpp): that session's profile kept the player's value.</summary>
        public const string HookedLine = "profile cvar serializer hooked";

        private static readonly Regex HeldShakes =
            new Regex(@"cvars: held at run time: .*\bview_skipShakes 1\b", RegexOptions.CultureInvariant);

        /// <summary>A layer log line showing that the session held view_skipShakes at 1.</summary>
        public static bool ShowsShakesHeld(string line) => line != null && HeldShakes.IsMatch(line);

        /// <summary>A layer log line showing that the session's profile hooks were in.</summary>
        public static bool ShowsHooked(string line) => line != null && line.Contains(HookedLine);

        /// <summary>
        /// Whether a layer log's lines show a session that left view_skipShakes 1 in the profile: the hold's line and no
        /// <see cref="HookedLine"/> (a build before the hooks, or hooks that did not go in).
        /// </summary>
        public static bool LeftShakesHeld(IEnumerable<string> lines)
        {
            bool held = false;
            foreach (var line in lines ?? Enumerable.Empty<string>())
            {
                if (ShowsHooked(line)) return false;
                held = held || ShowsShakesHeld(line);
            }
            return held;
        }

        /// <summary>Ask the layer for the repair: a stereo launch, no marker yet, and a kept session left view_skipShakes 1.</summary>
        public static bool Wanted(bool stereo, bool markerExists, Func<bool> anySessionLeftShakes) =>
            stereo && !markerExists && anySessionLeftShakes != null && anySessionLeftShakes();

        /// <summary>A status value that settles the repair (the marker is written): <c>done</c>, <c>not_needed</c> or <c>off</c>.</summary>
        public static bool Settles(string statusValue)
        {
            switch ((statusValue ?? string.Empty).Trim().ToLowerInvariant())
            {
                case "done":
                case "not_needed":
                case "off":
                    return true;
                default:
                    return false;
            }
        }

        /// <summary>The marker's text: when, which session, what the layer reported.</summary>
        public static string MarkerText(DateTime now, string sessionId, string statusValue) =>
            "profile_repair=" + statusValue + "\nsession=" + sessionId + "\nwritten=" + now.ToString("s", CultureInfo.InvariantCulture) + "\n";

        /// <summary>
        /// Whether any session folder under <paramref name="logsDir"/> other than <paramref name="exceptDir"/> has a layer log
        /// (<c>eternalvr-*.log</c>) that <see cref="LeftShakesHeld"/>. Unreadable files and session folders that cannot be
        /// listed are skipped; a logs folder that cannot be listed gives false (no repair, the launch goes on).
        /// </summary>
        public static bool AnySessionLeftShakes(string logsDir, string exceptDir = null) =>
            AnySessionLeftShakes(logsDir, exceptDir, Directory.GetDirectories);

        internal static bool AnySessionLeftShakes(string logsDir, string exceptDir, Func<string, string[]> sessionDirs)
        {
            try
            {
                if (string.IsNullOrEmpty(logsDir) || !Directory.Exists(logsDir)) return false;
                foreach (var dir in sessionDirs(logsDir))
                {
                    if (exceptDir != null && string.Equals(Path.GetFullPath(dir), Path.GetFullPath(exceptDir), StringComparison.OrdinalIgnoreCase)) continue;
                    foreach (var file in LayerLogs(dir))
                        if (LogLeftShakes(file)) return true;
                }
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException) { }
            return false;
        }

        // One session folder that cannot be listed does not hide the others.
        private static string[] LayerLogs(string dir)
        {
            try
            {
                return Directory.GetFiles(dir, "eternalvr-*.log");
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException)
            {
                return Array.Empty<string>();
            }
        }

        private static bool LogLeftShakes(string file)
        {
            try
            {
                using (var stream = new FileStream(file, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
                using (var reader = new StreamReader(stream))
                    return LeftShakesHeld(Lines(reader).Take(LinesPerLog));
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException) { }
            return false;
        }

        private static IEnumerable<string> Lines(TextReader reader)
        {
            string line;
            while ((line = reader.ReadLine()) != null) yield return line;
        }

        /// <summary>The layer's <c>profile_repair</c> value from a status file's text, or null.</summary>
        public static string StatusValue(string statusText)
        {
            foreach (var raw in (statusText ?? string.Empty).Replace("\r\n", "\n").Split('\n'))
            {
                int eq = raw.IndexOf('=');
                if (eq > 0 && string.Equals(raw.Substring(0, eq).Trim(), StatusKey, StringComparison.OrdinalIgnoreCase))
                    return raw.Substring(eq + 1).Trim();
            }
            return null;
        }
    }
}
