using System.Collections.Generic;

namespace EternalVR.Launcher.Core.Report
{
    /// <summary>Where a report entry comes from.</summary>
    public enum ReportSource
    {
        /// <summary>Written by the launcher at export time (<see cref="ReportManifest.SystemFile"/>, <see cref="ReportManifest.PreflightFile"/>).</summary>
        Generated,
        /// <summary>A file in the data folder (<c>%LOCALAPPDATA%\EternalVR</c>).</summary>
        DataFolder,
        /// <summary>A file next to the launcher exe.</summary>
        ProgramFolder,
        /// <summary>A file in the layer folder the launcher uses.</summary>
        LayerFolder,
        /// <summary>Files matching a pattern in each of the newest session log folders (<c>logs\&lt;yyyyMMdd-HHmmss&gt;</c>).</summary>
        SessionFolder,
        /// <summary>
        /// The in-headset captures (left Menu held + a trigger) in each of the newest session folders' <c>captures</c>
        /// folder: PNG images taken as they are (not redacted: pixels only) with their text files (redacted),
        /// newest capture first, whole captures up to <see cref="ReportManifest.CapturesCapBytes"/>.
        /// </summary>
        SessionCaptures,
    }

    /// <summary>One line of the report manifest.</summary>
    public sealed class ReportItem
    {
        public ReportItem(ReportSource source, string pattern, string zipPath, string description, long headBytes = 0, long tailBytes = 0, int sessions = 0)
        {
            Source = source;
            Pattern = pattern;
            ZipPath = zipPath;
            Description = description;
            HeadBytes = headBytes;
            TailBytes = tailBytes;
            Sessions = sessions;
        }

        public ReportSource Source { get; }
        /// <summary>The path relative to the source folder; for session items a file name pattern (<c>*</c> wildcard).</summary>
        public string Pattern { get; }
        /// <summary>Where it goes in the zip; <c>{session}</c> and <c>{name}</c> are filled in for session items.</summary>
        public string ZipPath { get; }
        public string Description { get; }
        /// <summary>A longer file keeps its first <see cref="HeadBytes"/> and last <see cref="TailBytes"/> bytes (whole lines); 0 and 0 keep all.</summary>
        public long HeadBytes { get; }
        public long TailBytes { get; }
        /// <summary>Session items: how many of the newest session folders are searched.</summary>
        public int Sessions { get; }
    }

    /// <summary>
    /// What an exported report contains (ROADMAP M4.5 "Export report", T-095), in the order it is filled up to
    /// <see cref="TotalCapBytes"/>. docs/release/TROUBLESHOOTING.md documents the same list; keep both in step.
    /// Only text files are ever taken, and every one is redacted (<see cref="Redactor"/>) before zipping. Nothing
    /// else in the data folder goes in: no settings snapshots, no save backups, no memory dumps.
    /// </summary>
    public static class ReportManifest
    {
        private const long MiB = 1024 * 1024;

        public const string SystemFile = "system.txt";
        public const string PreflightFile = "preflight.txt";
        public const string ContentsFile = "report-contents.txt";

        /// <summary>The newest session folders whose layer logs are included.</summary>
        public const int SessionsKept = 3;

        /// <summary>
        /// The most uncompressed text a report holds. Logs compress about tenfold, so the zip stays far below
        /// GitHub's 25 MB attachment limit; an entry that would pass this cap is dropped and listed as dropped.
        /// </summary>
        public const long TotalCapBytes = 20 * MiB;

        /// <summary>
        /// The most capture files (<see cref="ReportSource.SessionCaptures"/>) a report holds, uncompressed, apart
        /// from <see cref="TotalCapBytes"/>. The layer's PNG files are not compressed, so they shrink severalfold in
        /// the zip; a capture that would pass this cap is left out whole and listed as left out.
        /// </summary>
        public const long CapturesCapBytes = 48 * MiB;

        public static readonly IReadOnlyList<ReportItem> Items = new[]
        {
            new ReportItem(ReportSource.Generated, SystemFile, SystemFile,
                "Launcher and layer versions and their check, Windows version, GPUs and drivers, the OpenXR runtime, the HAGS state, the folders in use"),
            new ReportItem(ReportSource.Generated, PreflightFile, PreflightFile, "The launcher's checks, run at export time"),
            new ReportItem(ReportSource.DataFolder, @"logs\launcher.log", "launcher.log",
                "The launcher log (checks, launch plans, settings restores); its last 4 MB", tailBytes: 4 * MiB),
            new ReportItem(ReportSource.DataFolder, "launcher.ini", "launcher.ini", "The launcher settings"),
            new ReportItem(ReportSource.ProgramFolder, "BUILD-INFO.txt", "BUILD-INFO.txt", "The release's version, commit and supported game builds"),
            new ReportItem(ReportSource.LayerFolder, "VK_LAYER_ETERNALVR.json", "layer/VK_LAYER_ETERNALVR.json", "The layer manifest"),
            new ReportItem(ReportSource.SessionFolder, "LAYER_LOADED", "sessions/{session}/{name}",
                "Whether the layer loaded in that session", sessions: SessionsKept),
            new ReportItem(ReportSource.SessionFolder, "eternalvr-*.log", "sessions/{session}/{name}",
                "The layer's log of each of the newest 3 sessions; a longer log keeps its first 1 MB and last 3 MB",
                headBytes: 1 * MiB, tailBytes: 3 * MiB, sessions: SessionsKept),
            new ReportItem(ReportSource.SessionFolder, "eternalvr-frames-*.csv", "sessions/{session}/{name}",
                "The frame timing table of the newest session only: its header and last 3 MB",
                headBytes: 16 * 1024, tailBytes: 3 * MiB, sessions: 1),
            new ReportItem(ReportSource.SessionCaptures, "capture-*", "sessions/{session}/captures/{name}",
                "The in-headset captures (left Menu held + a trigger: eye L, eye R and UI images with a text file each) of the newest 3 sessions, newest first, up to 48 MB",
                sessions: SessionsKept),
        };

        /// <summary>Shown in the contents file: what is never taken.</summary>
        public const string NeverIncluded =
            "Never included: memory dumps (*.dmp), save games and save backups, settings snapshots, the game's config files, "
            + "older session folders, and any file not listed above. The capture images are the only files not text.";
    }
}
