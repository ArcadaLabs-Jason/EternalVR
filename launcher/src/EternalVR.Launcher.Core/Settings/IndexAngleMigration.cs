using System;
using System.Collections.Generic;
using System.IO;
using EternalVR.Launcher.Core.Launch;

namespace EternalVR.Launcher.Core.Settings
{
    /// <summary>
    /// The one-time reset of the hand angles 0.1.87 told Valve Index players to set (Weapon hand angle -20, Off hand angle -40).
    /// The layer now gives Index controllers a fit of their own and adds the launcher's angles on top (DECISIONS T-225), so a
    /// player who kept them would have the hands turned twice. On the first start of a launcher that has this, launcher.ini
    /// and each VR settings profile whose pair is exactly (-20, -40) are set to (0, 0); any other pair is left alone.
    /// launcher.ini's <see cref="LauncherSettings.IndexAngleReset"/> marks it done, whatever it found, so it never runs again
    /// and a player who sets -20 / -40 later keeps them. The window tells the player once (<see cref="Notice"/>).
    /// </summary>
    public static class IndexAngleMigration
    {
        public const double OldWeaponHandAngle = -20.0;
        public const double OldOffHandAngle = -40.0;

        public sealed class Result
        {
            public Result(bool ran, bool launcherIni, IReadOnlyList<string> profiles)
            {
                Ran = ran;
                LauncherIni = launcherIni;
                Profiles = profiles;
            }

            /// <summary>The reset ran this time (launcher.ini had no marker).</summary>
            public bool Ran { get; }
            /// <summary>launcher.ini's angles were the old pair and are now 0: the caller saves it.</summary>
            public bool LauncherIni { get; }
            /// <summary>The profiles whose angles were the old pair, saved with 0.</summary>
            public IReadOnlyList<string> Profiles { get; }
            public bool AnyReset => LauncherIni || Profiles.Count > 0;
        }

        private static readonly Result NotRun = new Result(false, false, new string[0]);

        /// <summary>True when both angles are exactly 0.1.87's Index advice.</summary>
        public static bool IsOldIndexPair(LauncherSettings s) =>
            Math.Abs(s.WeaponHandAngle - OldWeaponHandAngle) < 1e-6 && Math.Abs(s.OffHandAngle - OldOffHandAngle) < 1e-6;

        /// <summary>
        /// The reset, when <paramref name="ini"/> (launcher.ini as read at start) has no marker: its angles and those of each
        /// profile in <paramref name="profiles"/> (none when null) that hold the old pair are set to 0, the profiles saved
        /// (CPU Saver items compared by <paramref name="saver"/>'s), and the marker set in <paramref name="ini"/>. A list of
        /// changes not saved into the active profile that held still holds: the reset moves launcher.ini and the profile alike,
        /// and the check value is made again for the new lines. A profile that cannot be read or saved is reported to
        /// <paramref name="warn"/> and keeps its angles. The caller saves launcher.ini when <see cref="Result.LauncherIni"/>.
        /// </summary>
        public static Result Run(LauncherSettings ini, ProfileStore profiles, CpuSaver saver, Action<string> warn)
        {
            if (ini.IndexAngleReset) return NotRun;
            bool held = ProfileEdits.ListHolds(ini);
            ini.IndexAngleReset = true;
            bool iniReset = IsOldIndexPair(ini);
            if (iniReset) ini.WeaponHandAngle = ini.OffHandAngle = 0.0;
            var reset = new List<string>();
            foreach (var name in profiles?.Names() ?? new string[0])
            {
                try
                {
                    var p = profiles.Load(name, ini);
                    if (p == null || !IsOldIndexPair(p)) continue;
                    p.WeaponHandAngle = p.OffHandAngle = 0.0;
                    profiles.Save(name, p, saver);
                    reset.Add(name);
                }
                catch (Exception e) when (e is SettingsException || e is IOException || e is UnauthorizedAccessException)
                {
                    warn?.Invoke("VR settings profile '" + name + "': its hand angles were not checked for the Index reset: " + e.Message);
                }
            }
            if (held) ini.ProfileCheck = ini.FileCheck = ProfileEdits.Fingerprint(LauncherSettings.ReadMap(ini.Serialize()));
            return new Result(true, iniReset, reset);
        }

        /// <summary>The window's one-time notice for a reset that changed something; null for none.</summary>
        public static string Notice(Result r)
        {
            if (r == null || !r.AnyReset) return null;
            var where = r.LauncherIni && r.Profiles.Count == 0 ? string.Empty
                : " (in " + (r.LauncherIni ? "your current settings and " : string.Empty)
                    + (r.Profiles.Count == 1 ? "the profile " : "the profiles ") + string.Join(", ", r.Profiles) + ")";
            return "EternalVR now fits Valve Index controllers by itself, so Weapon hand angle and Off hand angle (Play tab), which "
                + "were set to -20 and -40 as the last version suggested for Index controllers, are back at 0" + where + ". If you "
                + "set controller offsets for DOOM Eternal in SteamVR, set those back to 0 too. You can set the angles again any "
                + "time: they now add to the mod's own fit for your controller.";
        }
    }
}
