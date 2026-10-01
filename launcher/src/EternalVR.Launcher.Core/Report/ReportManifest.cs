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
        /// <summary>
        /// A file in the game's own <c>base</c> folder under Saved Games (<c>Saved Games\id Software\DOOMEternal\base</c>,
        /// the folder the settings snapshot knows, <see cref="ReportInputs.GameSavedGamesDirs"/>); the newest when there are several.
        /// </summary>
        GameFolder,
        /// <summary>
        /// The game's crash reports (<c>Crash.&lt;computer&gt;.&lt;number&gt;.html</c>) in that <c>base</c> folder, written since
        /// the oldest session in the report started or in the last <see cref="ReportManifest.GameCrashDays"/> days,
        /// whichever reaches back further: newest first, at most <see cref="ReportManifest.GameCrashesKept"/>.
        /// Never the memory dumps next to them (<c>crash-dumps\*.dmp</c>).
        /// </summary>
        GameCrashes,
        /// <summary>
        /// The player's own controller maps in the controls folder (<c>controls</c> in the data folder), as the launcher and
        /// the layer know them (<see cref="Settings.ControlsFolder.PlayerMaps"/>): the <c>*.toml</c> files directly in it (the
        /// controls used with no VR settings profile) and directly in each profile's folder (<c>controls\profiles\&lt;name&gt;</c>).
        /// Never the built-in copies in <c>defaults</c> or a README. The zip keeps each file's path below the controls folder,
        /// redacted as the text is (<see cref="ReportBuilder.ControlsPlayerMaps"/>).
        /// </summary>
        ControlsFolder,
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
        /// <summary>Where it goes in the zip; <c>{session}</c> and <c>{name}</c> are filled in for session items, <c>{path}</c> for the controls.</summary>
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
        public const string WindowsEventsFile = "windows-events.txt";

        /// <summary>The newest session folders whose layer logs are included.</summary>
        public const int SessionsKept = 5;

        /// <summary>The most game crash reports (<see cref="ReportSource.GameCrashes"/>) a report holds.</summary>
        public const int GameCrashesKept = 3;

        /// <summary>Game crash reports of the last this many days are taken even when older than the sessions in the report.</summary>
        public const int GameCrashDays = 7;

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
                "Launcher and layer versions and their check, Windows version, GPUs and drivers, the OpenXR runtime, chosen SteamVR settings (SteamVrSummary), "
                + "the HAGS state, the game build, the names in the game's Mods folder and any mod loader (GameMods), the folders in use"),
            new ReportItem(ReportSource.Generated, PreflightFile, PreflightFile, "The launcher's checks, run at export time"),
            new ReportItem(ReportSource.DataFolder, @"logs\launcher.log", "launcher.log",
                "The launcher log (checks, launch plans, settings restores); its last 4 MB", tailBytes: 4 * MiB),
            new ReportItem(ReportSource.DataFolder, "launcher.ini", "launcher.ini", "The launcher settings"),
            new ReportItem(ReportSource.ProgramFolder, "BUILD-INFO.txt", "BUILD-INFO.txt", "The release's version, commit and supported game builds"),
            new ReportItem(ReportSource.LayerFolder, "VK_LAYER_ETERNALVR.json", "layer/VK_LAYER_ETERNALVR.json", "The layer manifest"),
            new ReportItem(ReportSource.ControlsFolder, "*.toml", "controls/{path}",
                "The player's own controller maps, with no profile and of each VR settings profile (not the built-in copies in defaults); "
                + "a longer map keeps its first and last 64 KB",
                headBytes: 64 * 1024, tailBytes: 64 * 1024),
            // The game's files come before the session logs: when every log is at its longest, the oldest session's log is left out, not these.
            new ReportItem(ReportSource.Generated, WindowsEventsFile, WindowsEventsFile,
                "Windows event log entries of the last 7 days: crashes and hangs of the game, the launcher or the layer, and display driver resets and errors (see WindowsEvents)"),
            new ReportItem(ReportSource.GameCrashes, "Crash.*.html", "game-crashes/crash-{number}.html",
                "The game's own crash reports (call stack, registers, exception code, build) written since the oldest session in the report or in the last 7 days, newest first, at most 3"),
            new ReportItem(ReportSource.GameFolder, "qconsole.log", "game/qconsole.log",
                "The game's console log of its latest start; a longer log keeps its first 1 MB and last 3 MB",
                headBytes: 1 * MiB, tailBytes: 3 * MiB),
            new ReportItem(ReportSource.GameFolder, "DOOMEternalConfig.cfg", "game/DOOMEternalConfig.cfg",
                "The game's settings file (graphics settings, binds) as it is at export time; a longer file keeps its first and last 128 KB",
                headBytes: 128 * 1024, tailBytes: 128 * 1024),
            new ReportItem(ReportSource.GameFolder, "DOOMEternalConfig.local", "game/DOOMEternalConfig.local",
                "The game's local settings file (resolution and the like) as it is at export time; a longer file keeps its first and last 16 KB",
                headBytes: 16 * 1024, tailBytes: 16 * 1024),
            new ReportItem(ReportSource.SessionFolder, "LAYER_LOADED", "sessions/{session}/{name}",
                "Whether the layer loaded in that session", sessions: SessionsKept),
            new ReportItem(ReportSource.SessionFolder, "eternalvr-*.log", "sessions/{session}/{name}",
                "The layer's log of each of the newest 5 sessions; a longer log keeps its first 1 MB and last 3 MB",
                headBytes: 1 * MiB, tailBytes: 3 * MiB, sessions: SessionsKept),
            new ReportItem(ReportSource.SessionFolder, "eternalvr-frames-*.csv", "sessions/{session}/{name}",
                "The frame timing table of the newest session only: its header and last 3 MB",
                headBytes: 16 * 1024, tailBytes: 3 * MiB, sessions: 1),
            new ReportItem(ReportSource.SessionCaptures, "capture-*", "sessions/{session}/captures/{name}",
                "The in-headset captures (left Menu held + a trigger: eye L, eye R and UI images with a text file each) of the newest 5 sessions, newest first, up to 48 MB",
                sessions: SessionsKept),
        };

        /// <summary>Shown in the contents file: what is never taken.</summary>
        public const string NeverIncluded =
            "Never included: memory dumps (*.dmp), save games and save backups, settings snapshots, the game's structured.log "
            + "(it holds account IDs), older session folders, and any file not listed above. The capture images are the only files not text.";
    }
}
