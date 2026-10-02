using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Game;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Report;

namespace EternalVR.Launcher.Core.Preflight
{
    public enum Severity { Pass, Warn, Fail }

    public sealed class Check
    {
        public Check(string id, Severity severity, string message)
        {
            Id = id;
            Severity = severity;
            Message = message;
        }

        public string Id { get; }
        public Severity Severity { get; }
        public string Message { get; }

        public override string ToString() => $"[{Severity.ToString().ToUpperInvariant()}] {Id}: {Message}";
    }

    /// <summary>Everything preflight looks at, gathered by the Windows side (registry and processes read only).</summary>
    public sealed class PreflightFacts
    {
        public bool IsElevated { get; set; }
        public bool SteamRunning { get; set; }
        /// <summary>Null when it cannot be told.</summary>
        public bool? SteamLoggedIn { get; set; }
        public IReadOnlyList<string> GameProcessesRunning { get; set; } = new string[0];
        public bool DataFolderWritable { get; set; } = true;
        public string DataFolderError { get; set; }
        public bool ProgramFolderUnderProgramFiles { get; set; }
        public string GameRoot { get; set; }
        public BuildCheck Build { get; set; }
        /// <summary>Steam's build ID of the installed game (its app manifest); null when unknown.</summary>
        public string SteamBuildId { get; set; }
        /// <summary>The build IDs (Steam) or package versions (Game Pass) of <c>known-builds.txt</c> for the platform, for the refusal message.</summary>
        public IReadOnlyList<string> KnownBuildIds { get; set; } = new string[0];
        /// <summary>The store of the install found; the Steam facts (Steam running, logged in, cloud record) count only for Steam.</summary>
        public GamePlatform Platform { get; set; }
        /// <summary>A stand-in program replaces the game (<c>--test-exe</c>): its build is never a known one.</summary>
        public bool TestMode { get; set; }
        /// <summary>Compatibility facts that only warn (<see cref="CompatibilityChecks"/>).</summary>
        public CompatibilityFacts Compatibility { get; set; } = new CompatibilityFacts();
        public string LayerDir { get; set; }
        public bool LayerManifestExists { get; set; }
        public bool LayerLibraryExists { get; set; }
        /// <summary>The launcher's informational version (<c>0.1.0+commit</c>).</summary>
        public string LauncherVersion { get; set; }
        /// <summary>The product version in <c>EternalVR.dll</c>'s version resource; null when it has none.</summary>
        public string LayerVersion { get; set; }
        /// <summary>The runtime manifest the game will use: the chosen one, else the system's active one.</summary>
        public string RuntimeManifest { get; set; }
        public bool RuntimeChosen { get; set; }
        public bool RuntimeManifestExists { get; set; }
        public IReadOnlyList<LayerDecision> Layers { get; set; } = new LayerDecision[0];
        /// <summary>Controller bindings chosen in SteamVR for the game's app key (<see cref="SteamVrSummary.ReadCustomBindings"/>).</summary>
        public IReadOnlyList<SteamVrBinding> SteamVrBindings { get; set; } = new SteamVrBinding[0];
        /// <summary>HwSchMode from the registry; 2 means hardware-accelerated GPU scheduling is on.</summary>
        public int? HagsMode { get; set; }
        public int SettingsLocationCount { get; set; }
        public string ArgumentRefusal { get; set; }
        public bool DisableLayerInherited { get; set; }
        public IReadOnlyList<string> AntiCheatFiles { get; set; } = new string[0];
        public bool SessionPending { get; set; }
        /// <summary>Steam-Cloud files whose size or SHA-1 differs from Steam's record (remotecache.vdf, T-115).</summary>
        public IReadOnlyList<string> CloudRecordStale { get; set; } = new string[0];
        /// <summary>Steam records that exist but could not be read.</summary>
        public IReadOnlyList<string> CloudRecordUnreadable { get; set; } = new string[0];
        /// <summary>The last session's eye size when it was below the plan (<see cref="RenderCap"/>); null otherwise.</summary>
        public RenderCap LastRenderCap { get; set; }
    }

    public sealed class PreflightResult
    {
        public PreflightResult(IReadOnlyList<Check> checks) { Checks = checks; }

        public IReadOnlyList<Check> Checks { get; }
        public bool CanLaunch => Checks.All(c => c.Severity != Severity.Fail);
        public IEnumerable<Check> Failures => Checks.Where(c => c.Severity == Severity.Fail);
    }

    /// <summary>Preflight v0 decisions (ARCHITECTURE section 4, ROADMAP M4.5, T-093, T-106, T-109, T-110).</summary>
    public static class PreflightEvaluator
    {
        public const string HagsHelp = "Settings > System > Display > Graphics > Change default graphics settings > Hardware-accelerated GPU scheduling";
        public const string SteamVrBindingsHelp = "SteamVR > Settings > Controllers > Manage Controller Bindings";

        public static PreflightResult Evaluate(PreflightFacts f)
        {
            var c = new List<Check>();
            void Add(string id, Severity s, string m) => c.Add(new Check(id, s, m));

            Add("elevation", f.IsElevated ? Severity.Fail : Severity.Pass,
                f.IsElevated ? "The launcher is running as administrator. Close it and start it normally: Windows ignores the layer settings in elevated processes."
                             : "Not elevated");

            bool steam = f.Platform == GamePlatform.Steam;
            if (!steam) Add("platform", Severity.Pass, "Game Pass version of DOOM Eternal: Steam is not needed");
            else if (!f.SteamRunning) Add("steam", Severity.Fail, "Steam is not running. Start Steam and log in, then launch again.");
            else if (f.SteamLoggedIn == false) Add("steam", Severity.Fail, "Steam is running but no user is logged in. Log in to Steam first.");
            else if (f.SteamLoggedIn == null) Add("steam", Severity.Warn, "Steam is running; whether a user is logged in could not be determined.");
            else Add("steam", Severity.Pass, "Steam is running and logged in");

            if (f.GameProcessesRunning.Count > 0)
                Add("game-running", Severity.Fail, "Already running: " + string.Join(", ", f.GameProcessesRunning) + ". Quit the game (and its launcher) first.");
            else Add("game-running", Severity.Pass, "The game is not running");

            if (f.SessionPending)
                Add("pending-restore", Severity.Fail, "The previous VR session's settings restore has not completed; it runs once the game has exited.");

            if (!f.DataFolderWritable) Add("data-folder", Severity.Fail, "The data folder cannot be written: " + f.DataFolderError);
            else Add("data-folder", Severity.Pass, "The data folder is writable");
            if (f.ProgramFolderUnderProgramFiles)
                Add("program-folder", Severity.Warn, "The launcher is inside Program Files. It works, but extracting it to a folder of your own is recommended.");

            if (string.IsNullOrEmpty(f.GameRoot))
                Add("game", Severity.Fail, "DOOM Eternal was not found in the Steam libraries or the Game Pass folders. Choose the game folder manually.");
            else if ((f.Build == null || f.Build.Status == BuildStatus.Missing) && !steam)
                Add("game", Severity.Fail, $"{f.GameRoot} is not the Content folder of the Game Pass DOOM Eternal (it holds {GamePassInstall.ConfigFile} "
                    + $"and {GameLayout.RetailExe}). Choose that folder, usually XboxGames\\<game name>\\Content on the drive the game is on.");
            else if (f.Build == null || f.Build.Status == BuildStatus.Missing)
                Add("game", Severity.Fail, $"{GameLayout.RetailExe} was not found in {f.GameRoot}.");
            else if (f.Build.Status == BuildStatus.Unknown && f.TestMode)
                Add("game-build", Severity.Warn, $"Unknown game build (SHA-256 {f.Build.Sha256}): a test stand-in.");
            else if (f.Build.Status == BuildStatus.Unknown)
                Add("game-build", Severity.Fail, UnknownBuildMessage(f));
            else Add("game-build", Severity.Pass, $"Known game build {f.Build.Build.BuildId} ({f.Build.Build.Description})");

            if (f.AntiCheatFiles.Count > 0)
                Add("anti-cheat", Severity.Fail, "Anti-cheat components found in the game folder: " + string.Join(", ", f.AntiCheatFiles) + ". VR is refused.");

            if (!f.LayerManifestExists || !f.LayerLibraryExists)
                Add("layer", Severity.Fail, $"The EternalVR layer is incomplete in {f.LayerDir} (VK_LAYER_ETERNALVR.json and EternalVR.dll are both needed).");
            else
            {
                Add("layer", Severity.Pass, "Layer found in " + f.LayerDir);
                c.Add(VersionCheck.Evaluate(f.LauncherVersion, f.LayerVersion, f.LayerDir));
            }
            if (f.DisableLayerInherited)
                Add("layer-disabled", Severity.Warn, "ETERNALVR_DISABLE_LAYER=1 is set in your environment; the layer will stay off.");

            if (string.IsNullOrEmpty(f.RuntimeManifest))
                Add("openxr", Severity.Fail, "No active OpenXR runtime is set. Start your headset software (SteamVR, Virtual Desktop, Meta Horizon) and make it the OpenXR runtime, or choose a runtime here.");
            else if (!f.RuntimeManifestExists)
                Add("openxr", Severity.Fail, $"The {(f.RuntimeChosen ? "chosen" : "system's active")} OpenXR runtime manifest does not exist: {f.RuntimeManifest}. Choose another runtime here, or switch the active runtime in your headset software.");
            else Add("openxr", Severity.Pass, $"OpenXR runtime: {f.RuntimeManifest}{(f.RuntimeChosen ? " (chosen for this launch)" : " (system active)")}");

            foreach (var d in f.Layers.Where(l => l.Action != LayerAction.Ignore))
                Add("layers", d.Action == LayerAction.Warn ? Severity.Warn : Severity.Pass, d.Message);

            // Only SteamVR reads its own settings file; the bindings there mean nothing to another runtime.
            if (LaunchPlanBuilder.IsSteamVr(f.RuntimeManifest) && f.SteamVrBindings.Count > 0)
                Add("steamvr-binding", Severity.Warn, "SteamVR uses a custom controller binding for DOOM Eternal ("
                    + string.Join(", ", f.SteamVrBindings) + "). If your controllers do nothing in game, open " + SteamVrBindingsHelp
                    + ", pick DOOM Eternal and choose the default binding.");

            if (f.LastRenderCap != null && f.LastRenderCap.Capped)
                Add("render-size", Severity.Warn, "Last session: " + f.LastRenderCap.Warning());

            if (f.HagsMode == 2)
                Add("hags", Severity.Warn, "Hardware-accelerated GPU scheduling is on; it caused frame hitching in VR on the development PC. To turn it off: " + HagsHelp + " (restart needed).");

            if (f.SettingsLocationCount == 0 && !f.TestMode)
                Add("settings", Severity.Fail, "DOOM Eternal has not been started on this PC yet (no settings folder). Start it once through "
                    + (steam ? "Steam" : "the Xbox app") + ", "
                    + "go past the first screens to the main menu, quit, and launch VR again.");
            else if (f.SettingsLocationCount == 0)
                Add("settings", Severity.Warn, "No DOOM Eternal settings folder was found, so no settings are forced for VR (they could not be restored afterwards).");
            else Add("settings", Severity.Pass, f.SettingsLocationCount + " settings location(s) will be snapshotted and restored");

            if (steam && f.CloudRecordStale.Count > 0)
                Add("cloud-record", Severity.Warn, "Steam's cloud record is stale for " + string.Join(", ", f.CloudRecordStale)
                    + ": the file was changed behind Steam's back, and the game may reset your profile (\"Profile corrupt\") at its next start. "
                    + "Start DOOM Eternal once through Steam and quit at the main menu to let Steam take the files on disk.");
            foreach (var u in steam ? f.CloudRecordUnreadable : new string[0])
                Add("cloud-record", Severity.Warn, "Steam's cloud record could not be read (" + u + "); the cloud files are not checked.");

            if (f.ArgumentRefusal != null) Add("single-player", Severity.Fail, f.ArgumentRefusal);

            c.AddRange(CompatibilityChecks.Evaluate(f.Compatibility, f.GameRoot));
            return new PreflightResult(c);
        }

        /// <summary>The refusal for a game build the layer does not know: its engine hooks are pinned to the supported builds.</summary>
        public static string UnknownBuildMessage(PreflightFacts f)
        {
            var supported = string.Join(", ", f.KnownBuildIds);
            if (f.Platform == GamePlatform.GamePass)
                return $"DOOM Eternal (Game Pass) was updated (version {f.Build.Version ?? "unknown"}). This EternalVR supports Game Pass version {supported}; "
                    + "VR would stay off (the game would run flat). Wait for an EternalVR update for this game version.";
            var build = string.IsNullOrEmpty(f.SteamBuildId) ? "an unknown build" : "build " + f.SteamBuildId;
            return $"DOOM Eternal was updated ({build}, SHA-256 {f.Build.Sha256}). This EternalVR supports build {supported}; "
                + "VR would stay off (the game would run flat). Wait for an EternalVR update for this game version.";
        }
    }
}
