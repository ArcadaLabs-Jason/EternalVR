using System;
using System.Collections.Generic;
using System.IO;

namespace EternalVR.Launcher.Core.Launch
{
    /// <summary>
    /// Command-line options of <c>EternalVR.Launcher.exe</c>. Without options the window opens. The
    /// path overrides exist for development and tests: with <c>--test-exe</c> a stand-in program is
    /// started instead of the game, and it is only allowed together with <c>--data-root</c>,
    /// <c>--saved-games</c> and <c>--steam-root</c>, so a test never reads or restores real settings.
    /// </summary>
    public sealed class LauncherOptions
    {
        public bool DryRun { get; private set; }
        public bool Help { get; private set; }
        public bool RestoreSaves { get; private set; }
        public bool RegisterHkcu { get; private set; }
        public string DataRoot { get; private set; }
        public string GameDir { get; private set; }
        public string LayerDir { get; private set; }
        public string SavedGamesDir { get; private set; }
        public string SteamRoot { get; private set; }
        public string TestExe { get; private set; }
        /// <summary>With <c>--launch</c> the command-line mode runs the whole session headless (tests only).</summary>
        public bool Launch { get; private set; }
        /// <summary>With <c>--export-report</c> the command-line mode writes a report zip there and exits.</summary>
        public string ExportReport { get; private set; }
        /// <summary><c>--finish-session</c>: the windowless helper the window starts with each session (it restores the settings
        /// after the game exits if the window was closed or killed meanwhile).</summary>
        public bool FinishSession { get; private set; }

        public bool Headless => DryRun || Help || RestoreSaves || Launch || ExportReport != null;

        public const string Usage =
@"EternalVR.Launcher.exe [options]
  (no options)          open the launcher window
  --dry-run             run preflight, snapshot the settings, back up the saves and print the launch
                        plan without starting the game
  --restore-saves       restore the newest save backup (the game closed, Steam running); if Steam's
                        cloud record is then stale, the game is started briefly so Steam takes them.
                        Exit 0 restored, 2 refused, 4 restored but the record is still stale
  --export-report <zip> write the report zip (logs, versions, checks; redacted) to <zip> and exit
  --data-root <dir>     user data folder (default %LOCALAPPDATA%\EternalVR)
  --game-dir <dir>      DOOM Eternal folder (default: found through Steam, else Game Pass)
  --layer-dir <dir>     folder holding VK_LAYER_ETERNALVR.json and EternalVR.dll (default: layer\ next to the launcher)
  --saved-games <dir>   the game's Saved Games folder (default: Saved Games\id Software\DOOMEternal)
  --steam-root <dir>    Steam folder for userdata and libraries (default: from the registry)
  --register-hkcu       register the layer under HKCU for the session instead of the environment route
  --test-exe <exe>      start <exe> instead of the game (tests; needs --data-root, --saved-games, --steam-root)
  --launch              with --test-exe: run a whole session from the command line
  --finish-session      (started by the window) wait for the game to exit and restore the settings if
                        the window is no longer open";

        public static LauncherOptions Parse(IReadOnlyList<string> args)
        {
            var o = new LauncherOptions();
            for (int i = 0; i < args.Count; i++)
            {
                string Value()
                {
                    if (i + 1 >= args.Count) throw new ArgumentException(args[i] + " needs a value");
                    return args[++i];
                }

                switch (args[i].ToLowerInvariant())
                {
                    case "--dry-run": o.DryRun = true; break;
                    case "--help": case "-h": case "/?": o.Help = true; break;
                    case "--restore-saves": o.RestoreSaves = true; break;
                    case "--register-hkcu": o.RegisterHkcu = true; break;
                    case "--launch": o.Launch = true; break;
                    case "--data-root": o.DataRoot = Full(Value()); break;
                    case "--game-dir": o.GameDir = Full(Value()); break;
                    case "--layer-dir": o.LayerDir = Full(Value()); break;
                    case "--saved-games": o.SavedGamesDir = Full(Value()); break;
                    case "--steam-root": o.SteamRoot = Full(Value()); break;
                    case "--test-exe": o.TestExe = Full(Value()); break;
                    case "--export-report": o.ExportReport = Full(Value()); break;
                    case "--finish-session": o.FinishSession = true; break;
                    default: throw new ArgumentException("unknown option: " + args[i]);
                }
            }

            if (o.TestExe != null && (o.DataRoot == null || o.SavedGamesDir == null || o.SteamRoot == null))
                throw new ArgumentException("--test-exe needs --data-root, --saved-games and --steam-root");
            if (o.Launch && o.TestExe == null)
                throw new ArgumentException("--launch is for tests and needs --test-exe; use the window to play");
            if (o.FinishSession && (o.Headless))
                throw new ArgumentException("--finish-session cannot be combined with another command");
            if (o.TestExe != null && o.RegisterHkcu)
                throw new ArgumentException("--register-hkcu cannot be combined with --test-exe");
            return o;
        }

        private static string Full(string path) => Path.GetFullPath(path);
    }
}
