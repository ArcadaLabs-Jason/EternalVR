using System;
using System.Linq;

namespace EternalVR.Launcher.Core.Preflight
{
    /// <summary>
    /// The status line's reason when the checks keep Launch VR off: the first failed check in plain words, the full message
    /// staying on the Checks and log tab.
    /// </summary>
    public static class LaunchBlock
    {
        /// <summary>"Launch VR is off: Steam is not running (see Checks and log)."; null when the checks let it launch.</summary>
        public static string Status(PreflightResult result)
        {
            var first = result?.Failures.FirstOrDefault();
            return first == null ? null : "Launch VR is off: " + Reason(first) + " (see Checks and log).";
        }

        /// <summary>A failed check in a few plain words; for a check without its own, the first sentence of its message.</summary>
        public static string Reason(Check check)
        {
            var m = check.Message ?? string.Empty;
            switch (check.Id)
            {
                case "elevation": return "the launcher runs as administrator";
                case "steam": return m.IndexOf("not running", StringComparison.OrdinalIgnoreCase) >= 0 ? "Steam is not running" : "no one is logged in to Steam";
                case "game-running": return m.StartsWith("The DOOM Eternal Launcher", StringComparison.Ordinal) ? "close the DOOM Eternal Launcher first" : "DOOM Eternal is already running";
                case "pending-restore": return "your game settings from the last session are not put back yet";
                case "data-folder": return "the launcher's data folder cannot be written";
                case "game": return "DOOM Eternal was not found";
                case "game-build": return "this version of DOOM Eternal is not supported yet";
                case "anti-cheat": return "anti-cheat files are in the game folder";
                case "layer": return "the EternalVR layer's files are missing";
                case "version": return "the launcher and the EternalVR layer are different versions";
                case "openxr":
                    return m.StartsWith("No active", StringComparison.Ordinal) ? "no OpenXR runtime is set" : "the OpenXR runtime's file is missing";
                case "settings": return "DOOM Eternal has not been started on this PC yet";
                case "single-player": return "an extra game argument is not allowed";
                default: return FirstSentence(m);
            }
        }

        private static string FirstSentence(string message)
        {
            var text = message.Trim();
            int end = text.IndexOf(". ", StringComparison.Ordinal);
            if (end > 0) text = text.Substring(0, end);
            text = text.TrimEnd('.');
            if (text.Length == 0) return "a check failed";
            // "The game..." reads on after the colon as "the game...", but "DOOM" and "OpenXR" keep their capitals.
            bool word = text.Length > 1 && char.IsLower(text[1]);
            return word ? char.ToLowerInvariant(text[0]) + text.Substring(1) : text;
        }
    }
}
