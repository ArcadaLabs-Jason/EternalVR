using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using EternalVR.Launcher.Core;
using EternalVR.Launcher.Core.Data;
using EternalVR.Launcher.Core.Game;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Preflight;
using EternalVR.Launcher.Core.Safety;
using EternalVR.Launcher.Core.Settings;
using EternalVR.Launcher.Core.Steam;
using EternalVR.Launcher.Platform;

namespace EternalVR.Launcher
{
    /// <summary>Resolved paths, data and settings for one launcher process, and the facts preflight reads.</summary>
    public sealed class LauncherContext
    {
        private LauncherContext(LauncherOptions options, Log log)
        {
            Options = options;
            Log = log;
            ProgramDir = Path.GetDirectoryName(typeof(LauncherContext).Assembly.Location);
            Paths = new DataPaths(DataRootFor(options));
        }

        public static string DataRootFor(LauncherOptions options) =>
            Path.GetFullPath(options.DataRoot ?? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "EternalVR"));

        public LauncherOptions Options { get; }
        public Log Log { get; }
        public string ProgramDir { get; }
        public DataPaths Paths { get; }
        public LauncherData Data { get; private set; }
        public LauncherSettings Settings { get; set; }

        public bool TestMode => Options.TestExe != null;

        /// <summary>The player's controls in the data folder: the no-profile set and each VR settings profile's own.</summary>
        public ControlSets ControlSets => new ControlSets(Paths.Controls);

        /// <summary>The controls of the VR settings profile in use (<c>ControlSets.InUse</c>: its own, else the no-profile set).</summary>
        public ControlsFolder Controls => ControlSets.InUse(Settings.Profile);

        /// <summary>The built-in controller maps shipped with the launcher, copied into each controls folder's defaults.</summary>
        public string DefaultControlsDir => Path.Combine(ProgramDir, "data", "controllers");

        public static LauncherContext Create(LauncherOptions options, Log log)
        {
            var ctx = new LauncherContext(options, log);
            ctx.Paths.EnsureCreated();
            log.SetFile(ctx.Paths.LauncherLog);
            ctx.Data = LauncherData.Load(Path.Combine(ctx.ProgramDir, "data"));
            ctx.Settings = LauncherSettings.Load(ctx.Paths.SettingsFile);
            return ctx;
        }

        /// <summary>The processes that count as the game (in test mode: only the stand-in exe).</summary>
        public IReadOnlyList<string> GameProcessNames =>
            TestMode ? new[] { Path.GetFileNameWithoutExtension(Options.TestExe) } : GameLayout.GameProcessNames;

        public IReadOnlyList<string> RunningGameProcesses() =>
            GameProcessNames.Where(WindowsSystem.IsProcessRunning).ToList();

        public string SteamRoot => Options.SteamRoot ?? WindowsSystem.SteamRoot();

        public string SavedGamesDir =>
            Options.SavedGamesDir ?? Path.Combine(WindowsSystem.SavedGamesFolder(), "id Software", "DOOMEternal");

        /// <summary>The logged-in account's userdata ID; unknown in test mode (every fake user is used).</summary>
        public string ActiveAccount => TestMode ? null : WindowsSystem.SteamActiveUser();

        /// <summary>The launcher's version: <c>0.1.0+commit</c> (<c>-dev</c> in a Debug build).</summary>
        public static string LauncherVersion =>
            typeof(LauncherContext).Assembly.GetCustomAttributes(typeof(System.Reflection.AssemblyInformationalVersionAttribute), false)
                .OfType<System.Reflection.AssemblyInformationalVersionAttribute>().FirstOrDefault()?.InformationalVersion;

        /// <summary>The layer DLL's product version (its version resource, read without loading it); null when it has none.</summary>
        public string LayerVersion() => WindowsSystem.ProductVersion(Path.Combine(LayerDir, "EternalVR.dll"));

        public string LayerDir =>
            Options.LayerDir ?? (string.IsNullOrWhiteSpace(Settings.LayerDir) ? Path.Combine(ProgramDir, "layer") : Settings.LayerDir);

        /// <summary>
        /// The game: the chosen folder, else the Steam install, else the Game Pass one. A chosen store folder is used as
        /// the Game Pass install: its Content folder (unless it is in WindowsApps), else the one found on the drives, else
        /// the folder itself (preflight then says it is not a Content folder).
        /// </summary>
        public GameInstallLocation ResolveGame()
        {
            if (TestMode)
                return new GameInstallLocation(Options.GameDir ?? Path.GetDirectoryName(Options.TestExe), null, null, "test");
            var manual = Options.GameDir ?? (string.IsNullOrWhiteSpace(Settings.GameDir) ? null : Settings.GameDir);
            if (manual != null)
            {
                var content = GamePassInstall.ContentFolderOf(manual);
                if (content == null && !GameLayout.IsStoreInstall(manual)) return new GameInstallLocation(manual, null, null, "chosen folder");
                if (content != null && !GamePassInstall.IsPackageStorePath(content)) return GamePassInstall.Locate(content, "chosen folder");
                return FindGamePass() ?? GamePassInstall.Locate(content ?? manual, "chosen folder");
            }
            return SteamLibraries.FindApp(SteamRoot, GameLayout.SteamAppId, GameLayout.RetailExe) ?? FindGamePass();
        }

        private static GameInstallLocation FindGamePass() => GamePassInstall.FindInDrives(WindowsSystem.FixedDriveRoots());

        public string GameExePath(GameInstallLocation game) =>
            TestMode ? Options.TestExe : Path.Combine(game.GameRoot, GameLayout.RetailExe);

        /// <summary>The settings locations of the game found now (<see cref="ResolveGame"/>).</summary>
        public IReadOnlyList<SettingsLocation> SettingsLocations() => SettingsLocations(ResolveGame());

        public IReadOnlyList<SettingsLocation> SettingsLocations(GameInstallLocation game) =>
            GameLayout.FindSettingsLocations(game?.Platform ?? GamePlatform.Steam, SavedGamesDir, SteamRoot, ActiveAccount);

        /// <summary>
        /// The locations the save backup covers: the Steam ones among the settings locations, and for Game Pass the
        /// package's save containers when they exist.
        /// </summary>
        public IReadOnlyList<SettingsLocation> SaveLocations(Gathered g)
        {
            if (g.Game?.Platform != GamePlatform.GamePass) return g.Locations;
            var saves = GamePassInstall.SaveLocation(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData));
            if (saves == null) Log.Info("save backup: no Game Pass save folder found");
            return saves == null ? g.Locations : g.Locations.Concat(new[] { saves }).ToList();
        }

        /// <summary>The runtime manifest the game will use: the chosen one, else the system's active one.</summary>
        public string EffectiveRuntime() =>
            LaunchPlanBuilder.IsSystemRuntime(Settings.Runtime) ? WindowsSystem.ActiveOpenXrRuntime() : Settings.Runtime;

        /// <summary>Runtimes to offer (T-110): active, AvailableRuntimes, SteamVR in every Steam library.</summary>
        public IReadOnlyList<string> RuntimeChoices()
        {
            var list = new List<string>();
            var active = WindowsSystem.ActiveOpenXrRuntime();
            if (!string.IsNullOrEmpty(active)) list.Add(active);
            list.AddRange(WindowsSystem.AvailableOpenXrRuntimes());
            var steam = SteamRoot;
            if (steam != null)
            {
                var vdf = Path.Combine(steam, "steamapps", "libraryfolders.vdf");
                try
                {
                    var libs = File.Exists(vdf) ? SteamLibraries.ParseLibraryFolders(File.ReadAllText(vdf), steam) : new[] { steam };
                    list.AddRange(SteamLibraries.FindSteamVrRuntimes(libs));
                }
                catch (Exception e) when (e is IOException || e is Core.Text.VdfFormatException) { Log.Warn("reading Steam libraries: " + e.Message); }
            }
            return list.Distinct(StringComparer.OrdinalIgnoreCase).ToList();
        }

        public sealed class Gathered
        {
            public PreflightFacts Facts;
            public PreflightResult Result;
            public GameInstallLocation Game;
            public IReadOnlyList<SettingsLocation> Locations;
            public IReadOnlyList<LayerDecision> LayerDecisions;
        }

        public Gathered GatherPreflight()
        {
            var g = new Gathered();
            var f = new PreflightFacts();
            f.IsElevated = WindowsSystem.IsElevated();
            f.GameProcessesRunning = RunningGameProcesses();
            f.SessionPending = File.Exists(Paths.SessionMarker);
            f.DataFolderWritable = FileUtil.IsWritableDirectory(Paths.Root, out var dataError);
            f.DataFolderError = dataError;
            f.ProgramFolderUnderProgramFiles = WindowsSystem.IsUnderProgramFiles(ProgramDir);

            f.TestMode = TestMode;
            g.Game = ResolveGame();
            f.Platform = g.Game?.Platform ?? GamePlatform.Steam;
            bool steam = f.Platform == GamePlatform.Steam;
            f.KnownBuildIds = Data.Builds.IdsFor(f.Platform);
            if (steam)
            {
                f.SteamRunning = WindowsSystem.IsProcessRunning("steam");
                var active = WindowsSystem.SteamActiveUser();
                f.SteamLoggedIn = active == null ? (bool?)null : active != "0";
            }
            if (g.Game != null)
            {
                f.GameRoot = g.Game.GameRoot;
                f.SteamBuildId = steam ? g.Game.BuildId : null;
                f.Build = steam ? Data.Builds.Check(GameExePath(g.Game)) : GamePassInstall.Check(Data.Builds, g.Game.GameRoot);
                f.AntiCheatFiles = TestMode ? new string[0] : Data.AntiCheat.Scan(g.Game.GameRoot);
            }
            f.Compatibility = new CompatibilityFacts
            {
                Gpus = WindowsSystem.GpuAdapters(),
                GameFolderDlls = g.Game == null ? new string[0] : FileUtil.FileNames(g.Game.GameRoot, "*.dll"),
                VulkanLoaderVersion = WindowsSystem.SystemVulkanLoaderVersion(),
                Paths = new[] { g.Game?.GameRoot, ProgramDir, Paths.Root },
                DataRoot = Paths.Root,
            };

            f.LayerDir = LayerDir;
            f.LayerManifestExists = File.Exists(Path.Combine(LayerDir, "VK_LAYER_ETERNALVR.json"));
            f.LayerLibraryExists = File.Exists(Path.Combine(LayerDir, "EternalVR.dll"));
            f.LauncherVersion = LauncherVersion;
            f.LayerVersion = f.LayerLibraryExists ? LayerVersion() : null;
            f.DisableLayerInherited = Environment.GetEnvironmentVariable("ETERNALVR_DISABLE_LAYER") == "1";

            f.RuntimeChosen = !LaunchPlanBuilder.IsSystemRuntime(Settings.Runtime);
            f.RuntimeManifest = EffectiveRuntime();
            f.RuntimeManifestExists = !string.IsNullOrEmpty(f.RuntimeManifest) && File.Exists(f.RuntimeManifest);

            g.LayerDecisions = Data.KnownLayers.Evaluate(WindowsSystem.ImplicitLayers(), LaunchPlanBuilder.IsVdxr(f.RuntimeManifest));
            f.Layers = g.LayerDecisions;
            f.HagsMode = WindowsSystem.HagsMode();

            g.Locations = SettingsLocations(g.Game);
            f.SettingsLocationCount = g.Locations.Count;
            // Game Pass has no Steam Cloud: its saves go through XGameSave.
            if (steam) CheckCloudRecord(f, g.Locations);
            f.ArgumentRefusal = Data.ArgumentPolicy.Check(Settings.ExtraArguments);

            g.Facts = f;
            g.Result = PreflightEvaluator.Evaluate(f);
            return g;
        }

        private void CheckCloudRecord(PreflightFacts f, IReadOnlyList<SettingsLocation> locations)
        {
            try
            {
                var cloud = SteamCloudCache.Check(locations);
                f.CloudRecordStale = cloud.Stale.Select(x => x.Key).ToList();
                f.CloudRecordUnreadable = cloud.Unreadable;
                foreach (var x in cloud.Stale) Log.Warn("Steam's cloud record is stale: " + x);
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException)
            {
                f.CloudRecordUnreadable = new[] { e.Message };
            }
        }

        /// <summary>The resync step's machine access: the game's process names, and Steam.</summary>
        public ICloudResyncHost ResyncHost() => new ProcessResyncHost(GameProcessNames);

        /// <summary>The resync launch (T-115): the game exe (the stand-in in test mode), from the game folder.</summary>
        public CloudResyncOptions ResyncOptions()
        {
            var game = ResolveGame();
            var o = new CloudResyncOptions
            {
                Exe = game == null ? null : GameExePath(game),
                WorkingDirectory = game?.GameRoot,
            };
            // The stand-in game never syncs through Steam; a failing test should not wait a minute.
            if (TestMode) o.SyncTimeoutMs = 10000;
            return o;
        }

        public LaunchPlan BuildPlan(Gathered g, string sessionId)
        {
            var plan = LaunchPlanBuilder.Build(new LaunchInputs
            {
                GameRoot = g.Game.GameRoot,
                LayerDir = LayerDir,
                LogDir = Paths.SessionLogDir(sessionId),
                Settings = Settings,
                ForcedCvars = Data.ForcedCvars,
                CpuSaver = Data.CpuSaver,
                ForceCvars = g.Locations.Count > 0,
                LayerDecisions = g.LayerDecisions,
                Route = Options.RegisterHkcu ? LayerRoute.HkcuRegistration : LayerRoute.Environment,
                Displays = WindowsSystem.Displays(),
                RuntimeProbe = ProbeRuntime(g.LayerDecisions),
                Controls = Controls,
                NewestDlss = NewestDlss(verify: true),
            });
            if (TestMode) plan.ExePath = Options.TestExe;
            if (plan.RenderSize?.Note != null) Log.Warn(plan.RenderSize.Note + " (" + plan.RenderSize.Reason + ")");
            return plan;
        }

        /// <summary>
        /// NVIDIA's newest listed DLSS (data\dlss-downloads.txt) when it is in the data folder's dlss folder, else null. With
        /// <paramref name="verify"/> (at launch) its SHA-256 is checked too; without (the window's line) only its size.
        /// </summary>
        public string NewestDlss(bool verify)
        {
            var release = Data.DlssDownloads?.Newest;
            if (release == null) return null;
            var path = DlssDownloads.PathFor(Paths.DlssFiles, release);
            try
            {
                return (verify ? DlssDownloads.Matches(path, release) : DlssDownloads.HasSize(path, release)) ? path : null;
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException)
            {
                Log.Warn("the downloaded DLSS cannot be read: " + e.Message);
                return null;
            }
        }

        /// <summary>
        /// Stereo with the render size on: asks the runtime the game will use (with the game's OpenXR environment) for
        /// its recommended eye size, through the layer's own OpenXR loader, for at most <see cref="OpenXrProbe.DefaultTimeoutMs"/>.
        /// </summary>
        private OpenXrProbeResult ProbeRuntime(IReadOnlyList<LayerDecision> decisions)
        {
            if (!LaunchPlanBuilder.WantsRenderSize(Settings)) return null;
            var env = LaunchPlanBuilder.OpenXrEnvironment(Settings, decisions);
            Log.Info($"waiting for the headset runtime (up to {OpenXrProbe.DefaultTimeoutMs / 1000} s)");
            var probe = OpenXrProbe.Run(Path.Combine(LayerDir, "openxr_loader.dll"), env);
            Log.Info("openxr probe (" + (LaunchPlanBuilder.IsSystemRuntime(Settings.Runtime) ? "system runtime" : Settings.Runtime) + "): " + probe);
            return probe;
        }
    }
}
