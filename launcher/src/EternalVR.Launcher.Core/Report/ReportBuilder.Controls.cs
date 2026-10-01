using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using EternalVR.Launcher.Core.Settings;

namespace EternalVR.Launcher.Core.Report
{
    /// <summary>The player's own controller maps in a report (<see cref="ReportSource.ControlsFolder"/>).</summary>
    public static partial class ReportBuilder
    {
        /// <summary>The contents file's line when the controls folder holds no map of the player's own.</summary>
        internal const string NoControlsNote = "Controls: no maps of your own (the built-in controls are used)";

        /// <summary>
        /// The player's own maps in the controls folder <paramref name="controlsDir"/> (<c>controls</c> in the data folder),
        /// the files the launch hands to the layer (<see cref="ControlsFolder.PlayerMaps"/>): those of the controls used with no
        /// VR settings profile (directly in the folder), then those of each profile's folder in <c>profiles</c>, by profile name;
        /// each set's files by name. Never the built-in copies in a set's <c>defaults</c> folder, its README, a file in another
        /// folder below a set, or a folder in <c>profiles</c> whose name cannot be a profile's (a copy in progress).
        /// </summary>
        public static IReadOnlyList<string> ControlsPlayerMaps(string controlsDir) => ControlsPlayerMaps(controlsDir, null);

        /// <summary>
        /// The placeholders in a redacted zip path (<c>&lt;user&gt;</c>, <c>&lt;player&gt;</c> and so on) become <c>[user]</c>,
        /// <c>[player]</c>: a Windows file name cannot hold <c>&lt;</c> or <c>&gt;</c>, so the zip could not be unpacked.
        /// </summary>
        internal static string ZipSafe(string zipPath) => zipPath.Replace('<', '[').Replace('>', ']');

        /// <summary>The player's maps, each under <c>controls/</c> with its path below the controls folder; a note when there are none.</summary>
        private static void AddControls(List<ReportFile> files, List<string> dropped, List<string> notes, string dataRoot, ReportItem item)
        {
            if (string.IsNullOrWhiteSpace(dataRoot)) { notes.Add(NoControlsNote); return; }
            var root = new ControlSets(new DataPaths(dataRoot).Controls).Root;
            int unreadable = dropped.Count;
            var maps = ControlsPlayerMaps(root, dropped);
            if (maps.Count == 0 && dropped.Count == unreadable) notes.Add(NoControlsNote);
            foreach (var path in maps)
            {
                int before = files.Count;
                AddFile(files, dropped, path, item, item.ZipPath.Replace("{path}", ControlsRelative(root, path)));
                if (files.Count > before) files[before].RedactZipPath = true; // the profile folder's name is the player's choice
            }
        }

        /// <summary>See <see cref="ControlsPlayerMaps(string)"/>; a set that cannot be listed is named in <paramref name="dropped"/> when given.</summary>
        private static IReadOnlyList<string> ControlsPlayerMaps(string controlsDir, List<string> dropped)
        {
            var maps = new List<string>();
            if (string.IsNullOrWhiteSpace(controlsDir) || !Directory.Exists(controlsDir)) return maps;
            var sets = new ControlSets(controlsDir);
            var folders = new List<ControlsFolder> { sets.Shared };
            try
            {
                if (Directory.Exists(sets.ProfilesDir))
                    folders.AddRange(Directory.GetDirectories(sets.ProfilesDir)
                        .Select(Path.GetFileName)
                        .Where(n => ProfileStore.NormaliseName(n) == n)
                        .OrderBy(n => n, StringComparer.OrdinalIgnoreCase)
                        .Select(sets.Of));
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException)
            {
                dropped?.Add($"controls/{ControlSets.ProfilesName}/: could not be read ({e.GetType().Name})");
            }
            foreach (var folder in folders)
            {
                try { maps.AddRange(folder.PlayerMaps().OrderBy(p => Path.GetFileName(p), StringComparer.OrdinalIgnoreCase)); }
                catch (Exception e) when (e is IOException || e is UnauthorizedAccessException)
                {
                    dropped?.Add($"controls/{ControlsRelative(sets.Root, folder.Dir)}: could not be read ({e.GetType().Name})");
                }
            }
            return maps;
        }

        /// <summary>A path below the controls folder with <c>/</c> separators (<c>profiles/name/valve_index.toml</c>); empty for the folder itself.</summary>
        private static string ControlsRelative(string root, string path) =>
            path.Substring(Math.Min(root.Length, path.Length)).TrimStart('\\', '/').Replace('\\', '/');
    }
}
