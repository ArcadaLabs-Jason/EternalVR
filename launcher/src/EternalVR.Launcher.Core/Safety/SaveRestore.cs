using System;
using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Game;
using EternalVR.Launcher.Core.Steam;

namespace EternalVR.Launcher.Core.Safety
{
    public enum SaveRestoreOutcome
    {
        /// <summary>Nothing was written: the game runs, Steam is not running or is syncing.</summary>
        Refused,
        /// <summary>Restored, and Steam's record matches the files (already, or after the resync).</summary>
        Restored,
        /// <summary>Restored; there is no Steam record to compare with, so it was not verified.</summary>
        RestoredUnverified,
        /// <summary>Restored, but Steam's record is still stale: the user must launch through Steam once.</summary>
        RestoredRecordStale,
    }

    public sealed class SaveRestoreResult
    {
        public SaveRestoreOutcome Outcome { get; internal set; }
        /// <summary>The backup of the saves found before the restore; null when there were none.</summary>
        public string PreRestoreDir { get; internal set; }
        public bool Resynced { get; internal set; }
        /// <summary>What happened, in words for the user.</summary>
        public string Summary { get; internal set; }
    }

    /// <summary>
    /// "Restore saves" made Steam-safe (T-115). The save slots are Steam-Cloud files, so the restore is
    /// refused while the game runs, while Steam is not running and while Steam is syncing the game's cloud
    /// folder. After writing, Steam's record (<c>remotecache.vdf</c>) is compared with the files; when it
    /// is stale the resync step (<see cref="CloudResync"/>) makes Steam take the restored files, and when
    /// that fails the user is told to launch the game once through Steam and quit at the main menu.
    /// </summary>
    public static class SaveRestore
    {
        public const string LaunchThroughSteamAdvice =
            "Start DOOM Eternal once through Steam and quit at the main menu, so Steam takes the restored saves as the current version. Until then the game may reset your profile (\"Profile corrupt\").";

        public static SaveRestoreResult Run(string backupsRoot, string backupDir, DateTime now,
                                            IReadOnlyList<SettingsLocation> localLocations, ICloudResyncHost host,
                                            CloudResyncOptions resync, Action<string> log)
        {
            var result = new SaveRestoreResult();
            if (SaveBackups.HoldsGamePassSaves(backupDir)) return Refuse(result, GamePassBackupAdvice(backupDir), log);
            var games = host.GameProcessIds();
            if (games.Count > 0) return Refuse(result, "Quit the game first (a game process is running).", log);
            if (!host.SteamRunning())
                return Refuse(result, "Start Steam and log in first: the saves are Steam Cloud files, and Steam must take the restored ones.", log);

            var cloud = SaveBackups.LocationsOf(backupDir);
            var first = SteamCloudCache.SyncSignature(cloud);
            host.Wait(resync.SyncQuietMs);
            if (SteamCloudCache.SyncSignature(cloud) != first)
                return Refuse(result, "Steam is syncing DOOM Eternal's cloud files right now. Wait until it has finished, then try again.", log);

            result.PreRestoreDir = SaveBackups.Restore(backupsRoot, backupDir, now);
            log($"restored saves from {backupDir}; " + (result.PreRestoreDir == null ? "there were no saves before the restore" : "the saves found before the restore are in " + result.PreRestoreDir));

            var check = SteamCloudCache.Check(cloud);
            if (check.Checked == 0)
            {
                foreach (var u in check.Unreadable) log("Steam's cloud record could not be read: " + u);
                result.Outcome = SaveRestoreOutcome.RestoredUnverified;
                result.Summary = "The saves were restored. Steam's cloud record (remotecache.vdf) was not found, so it could not be checked. " + LaunchThroughSteamAdvice;
                log(result.Summary);
                return result;
            }
            if (check.Consistent)
            {
                result.Outcome = SaveRestoreOutcome.Restored;
                result.Summary = "The saves were restored, and Steam's cloud record already matches them.";
                log(result.Summary);
                return result;
            }

            foreach (var s in check.Stale) log("Steam's cloud record is stale after the restore: " + s);
            log("making Steam take the restored saves: the game is started briefly (windowed, muted) and closed before it loads the profile");
            var all = localLocations.Where(l => l.Kind == SettingsLocationKind.SavedGames).Concat(cloud).ToList();
            var r = CloudResync.Run(resync, host, all, log);
            result.Resynced = r.Ok;
            if (r.Ok)
            {
                result.Outcome = SaveRestoreOutcome.Restored;
                result.Summary = "The saves were restored. The game was started briefly and closed so that Steam took them as the current version; Steam's cloud record now matches.";
            }
            else
            {
                result.Outcome = SaveRestoreOutcome.RestoredRecordStale;
                result.Summary = "The saves were restored, but Steam's cloud record does not match them yet (" + r.Note + "). " + LaunchThroughSteamAdvice;
            }
            log(result.Summary);
            return result;
        }

        /// <summary>Why a Game Pass save backup is not restored, and where its copy is.</summary>
        public static string GamePassBackupAdvice(string backupDir) =>
            "This backup holds Game Pass saves. The launcher keeps them as a copy only and does not write them back: "
            + "Windows syncs Game Pass saves with the Xbox cloud. The copy is in " + backupDir + ".";

        private static SaveRestoreResult Refuse(SaveRestoreResult result, string why, Action<string> log)
        {
            result.Outcome = SaveRestoreOutcome.Refused;
            result.Summary = "Nothing was restored. " + why;
            log(result.Summary);
            return result;
        }
    }
}
