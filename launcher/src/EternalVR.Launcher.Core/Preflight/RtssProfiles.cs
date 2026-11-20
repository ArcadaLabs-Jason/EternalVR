using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using EternalVR.Launcher.Core.Game;

namespace EternalVR.Launcher.Core.Preflight
{
    /// <summary>What RTSS's profiles set for DOOM Eternal's frame rate, or why they could not be read.</summary>
    public sealed class RtssLimit
    {
        private RtssLimit(IReadOnlyList<string> limits, string problem)
        {
            Limits = limits;
            Problem = problem;
        }

        /// <summary>Each setting that limits the game's frame rate, in words, with the profile it comes from.</summary>
        public IReadOnlyList<string> Limits { get; }
        /// <summary>Why the profiles could not be read; null when they were.</summary>
        public string Problem { get; }
        public bool Readable => Problem == null;
        public bool LimitsTheGame => Readable && Limits.Count > 0;

        public static RtssLimit None() => new RtssLimit(new string[0], null);
        public static RtssLimit Found(IEnumerable<string> limits) => new RtssLimit(limits.ToList(), null);
        public static RtssLimit Unreadable(string problem) => new RtssLimit(new string[0], problem);
    }

    /// <summary>
    /// Reads RivaTuner Statistics Server's profiles for a frame rate limit on DOOM Eternal (the coexist run of
    /// RELEASE-CHECKLIST.md item 7: an RTSS limit halves the headset's frame rate, since each headset frame is two of the
    /// game's presents). The profiles are INI files in the install folder's <c>Profiles</c>: <c>Global</c>, and
    /// <c>DOOMEternalx64vk.exe.cfg</c> when the player made one for the game, whose keys override the global ones. The keys
    /// are those of the <c>[Framerate]</c> section of RTSS 7.3.7's own <c>ProfileTemplates\Global</c>, with their meaning
    /// from its ReadMe and help texts: <c>Limit</c> / <c>LimitDenominator</c> (frames per second, fractional limits as
    /// a fraction, 0 for none), <c>LimitTime</c> / <c>LimitTimeDenominator</c> (the same as a frame time in microseconds),
    /// <c>IdleLimitTime</c> (a frame time limit for a game that is not the active window), <c>SyncScanline0</c> and
    /// <c>SyncScanline1</c> (scanline sync, which ties frames to the display's refresh; negative values count from the
    /// bottom) and <c>ThrottleTime</c>, which RTSS does not document and which counts as a limit when set.
    /// Whatever cannot be read for certain is reported as unreadable, never as "no limit", so the launcher warns: a
    /// missing or unreadable file, a malformed section header, a key given twice with different values (Windows' INI
    /// functions, which RTSS reads with, take the first and this reader does not guess), a value that is not a number.
    /// </summary>
    public static class RtssProfiles
    {
        public const string GlobalProfile = "Global";
        public const string GameProfile = GameLayout.RetailExe + ".cfg";
        private const string Section = "Framerate";

        /// <summary>
        /// RTSS's install folder from its Vulkan layer's manifest, which its installer puts in the <c>Vulkan</c> folder
        /// (<c>&lt;install&gt;\Vulkan\RTSSVkLayer64.json</c>); a manifest elsewhere counts its own folder as the install.
        /// </summary>
        public static string InstallDir(string manifestPath)
        {
            var dir = string.IsNullOrEmpty(manifestPath) ? null : Path.GetDirectoryName(manifestPath);
            if (string.IsNullOrEmpty(dir)) return null;
            return string.Equals(Path.GetFileName(dir), "Vulkan", StringComparison.OrdinalIgnoreCase) ? Path.GetDirectoryName(dir) : dir;
        }

        /// <summary>Reads the profiles of the RTSS whose layer manifest is <paramref name="manifestPath"/>; never throws.</summary>
        public static RtssLimit Read(string manifestPath) => Read(manifestPath, File.Exists, File.ReadAllText);

        /// <summary><see cref="Read(string)"/> with the file system passed in (the tests make it fail).</summary>
        internal static RtssLimit Read(string manifestPath, Func<string, bool> exists, Func<string, string> readText)
        {
            try
            {
                var install = InstallDir(manifestPath);
                if (install == null) return RtssLimit.Unreadable("its install folder is not known");
                var profiles = Path.Combine(install, "Profiles");
                var global = Path.Combine(profiles, GlobalProfile);
                var game = Path.Combine(profiles, GameProfile);
                if (!exists(global)) return RtssLimit.Unreadable($"there is no {global}");
                var globalText = readText(global);
                return Evaluate(globalText, exists(game) ? readText(game) : null);
            }
            catch (Exception e)
            {
                // Any failure (a path Windows refuses, a file in use, access denied) leaves the question open: warn.
                return RtssLimit.Unreadable(e.Message);
            }
        }

        /// <summary>The limits the global profile and the game's own (null when it has none) set together; never throws.</summary>
        public static RtssLimit Evaluate(string globalText, string gameText)
        {
            try
            {
                var global = FramerateSection(globalText, GlobalProfile, out var problem);
                if (problem != null) return RtssLimit.Unreadable(problem);
                if (!global.ContainsKey("Limit")) return RtssLimit.Unreadable("its Global profile has no [Framerate] Limit");
                var game = gameText == null ? new Dictionary<string, string>() : FramerateSection(gameText, GameProfile, out problem);
                if (problem != null) return RtssLimit.Unreadable(problem);
                return Limits(global, game);
            }
            catch (Exception e)
            {
                return RtssLimit.Unreadable(e.Message);
            }
        }

        private static RtssLimit Limits(Dictionary<string, string> global, Dictionary<string, string> game)
        {
            var limits = new List<string>();
            string problem = null;

            double Value(string key, double fallback, out string from)
            {
                from = game.ContainsKey(key) ? GameProfile : GlobalProfile;
                var raw = game.ContainsKey(key) ? game[key] : global.ContainsKey(key) ? global[key] : null;
                if (raw == null) return fallback;
                if (double.TryParse(raw, NumberStyles.Float, CultureInfo.InvariantCulture, out var v)) return v;
                problem = problem ?? $"{key} = {raw} in its {from} profile is not a number";
                return fallback;
            }

            string Where(string from) => "in its " + from + " profile";

            var limit = Value("Limit", 0, out var limitFrom);
            var limitDen = Value("LimitDenominator", 1, out _);
            if (limit > 0) limits.Add($"a limit of {Number(limit / (limitDen > 0 ? limitDen : 1))} frames per second {Where(limitFrom)}");

            var time = Value("LimitTime", 0, out var timeFrom);
            var timeDen = Value("LimitTimeDenominator", 1, out _);
            if (time > 0)
            {
                var us = time / (timeDen > 0 ? timeDen : 1);
                limits.Add($"a frame time limit of {Number(us)} microseconds (about {Rounded(1e6 / us)} frames per second) {Where(timeFrom)}");
            }

            var idle = Value("IdleLimitTime", 0, out var idleFrom);
            if (idle > 0)
                limits.Add($"a limit of {Number(idle)} microseconds a frame while the game is not the active window {Where(idleFrom)}");

            var scan0 = Value("SyncScanline0", 0, out var scan0From);
            var scan1 = Value("SyncScanline1", 0, out var scan1From);
            if (scan0 != 0) limits.Add($"scanline sync, which ties frames to the display's refresh, {Where(scan0From)}");
            else if (scan1 != 0) limits.Add($"scanline sync, which ties frames to the display's refresh, {Where(scan1From)}");

            var throttle = Value("ThrottleTime", 0, out var throttleFrom);
            if (throttle != 0) limits.Add($"ThrottleTime {Number(throttle)}, a setting RTSS does not document, {Where(throttleFrom)}");

            return problem != null ? RtssLimit.Unreadable(problem) : RtssLimit.Found(limits);
        }

        /// <summary>
        /// The keys of a profile's <c>[Framerate]</c> section, section and key names case-insensitive. Lines starting with
        /// ';' or '#' are comments; lines without '=' are skipped, as Windows does. <paramref name="problem"/> is set (and
        /// the keys are not to be used) for a section header that is not a clean <c>[name]</c>, or a key given twice in
        /// the section (or in a second <c>[Framerate]</c>) with different values.
        /// </summary>
        internal static Dictionary<string, string> FramerateSection(string text, string profile, out string problem)
        {
            problem = null;
            var keys = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            var inSection = false;
            foreach (var raw in (text ?? string.Empty).Split('\n'))
            {
                var line = raw.Trim().TrimStart('﻿').Trim();
                if (line.Length == 0 || line[0] == ';' || line[0] == '#') continue;
                if (line[0] == '[')
                {
                    var close = line.IndexOf(']');
                    if (close < 0 || line.Substring(close + 1).Trim().Length > 0)
                    {
                        problem = $"its {profile} profile has a section header it cannot read: {line}";
                        return keys;
                    }
                    inSection = string.Equals(line.Substring(1, close - 1).Trim(), Section, StringComparison.OrdinalIgnoreCase);
                    continue;
                }
                var eq = line.IndexOf('=');
                if (!inSection || eq <= 0) continue;
                var key = line.Substring(0, eq).Trim();
                var value = line.Substring(eq + 1).Trim();
                if (keys.TryGetValue(key, out var earlier) && earlier != value)
                {
                    problem = $"its {profile} profile has conflicting duplicate values for {key} ({earlier} and {value})";
                    return keys;
                }
                keys[key] = value;
            }
            return keys;
        }

        private static string Number(double v) => v.ToString("0.###", CultureInfo.InvariantCulture);

        private static string Rounded(double v) => v.ToString("0.#", CultureInfo.InvariantCulture);
    }
}
