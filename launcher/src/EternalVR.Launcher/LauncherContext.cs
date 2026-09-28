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

        /// <summary>The player's controls folder in the data folder.</summary>
        public ControlsFolder Controls => new ControlsFolder(Paths.Controls);

        /// <summary>The built-in controller maps shipped with the launcher, copied into the controls folder's defaults.</summary>
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

        public GameInstallLocation ResolveGame()
        {
            if (TestMode)
                return new GameInstallLocation(Options.GameDir ?? Path.GetDirectoryName(Options.TestExe), null, null, "test");
            var manual = Options.GameDir ?? (string.IsNullOrWhiteSpace(Settings.GameDir) ? null : Settings.GameDir);
            if (manual != null) return new GameInstallLocation(manual, null, null, "chosen folder");
            return SteamLibraries.FindApp(SteamRoot, GameLayout.SteamAppId, GameLayout.RetailExe);
        }

        public string GameExePath(GameInstallLocation game) =>
            TestMode ? Options.TestExe : Path.Combine(game.GameRoot, GameLayout.RetailExe);

        public IReadOnlyList<SettingsLocation> SettingsLocations() =>
            GameLayout.FindSettingsLocations(SavedGamesDir, SteamRoot, ActiveAccount);

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
            f.SteamRunning = WindowsSystem.IsProcessRunning("steam");
            var active = WindowsSystem.SteamActiveUser();
            f.SteamLoggedIn = active == null ? (bool?)null : active != "0";
            f.GameProcessesRunning = RunningGameProcesses();
            f.SessionPending = File.Exists(Paths.SessionMarker);
            f.DataFolderWritable = FileUtil.IsWritableDirectory(Paths.Root, out var dataError);
            f.DataFolderError = dataError;
            f.ProgramFolderUnderProgramFiles = WindowsSystem.IsUnderProgramFiles(ProgramDir);

            f.TestMode = TestMode;
            f.KnownBuildIds = Data.Builds.All.Select(b => b.BuildId).Distinct().ToList();
            g.Game = ResolveGame();
            if (g.Game != null)
            {
                f.GameRoot = g.Game.GameRoot;
                f.SteamBuildId = g.Game.BuildId;
                f.StoreInstall = !TestMode && GameLayout.IsStoreInstall(g.Game.GameRoot);
                f.Build = Data.Builds.Check(GameExePath(g.Game));
                f.AntiCheatFiles = TestMode ? new string[0] : Data.AntiCheat.Scan(g.Game.GameRoot);
            }
            f.Compatibility = new CompatibilityFacts
            {
                Gpus = WindowsSystem.GpuAdapters(),
                GameFolderDlls = g.Game == null ? new string[0] : FileUtil.FileNames(g.Game.GameRoot, "*.dll"),
                VulkanLoaderVersion = WindowsSystem.SystemVulkanLoaderVersion(),
                Paths = new[] { g.Game?.GameRoot, ProgramDir, Paths.Root },
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

            g.Locations = SettingsLocations();
            f.SettingsLocationCount = g.Locations.Count;
            try
            {
                var cloud = SteamCloudCache.Check(g.Locations);
                f.CloudRecordStale = cloud.Stale.Select(x => x.Key).ToList();
                f.CloudRecordUnreadable = cloud.Unreadable;
                foreach (var x in cloud.Stale) Log.Warn("Steam's cloud record is stale: " + x);
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException)
            {
                f.CloudRecordUnreadable = new[] { e.Message };
            }
            f.ArgumentRefusal = Data.ArgumentPolicy.Check(Settings.ExtraArguments);

            g.Facts = f;
            g.Result = PreflightEvaluator.Evaluate(f);
            return g;
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
                ForceCvars = g.Locations.Count > 0,
                LayerDecisions = g.LayerDecisions,
                Route = Options.RegisterHkcu ? LayerRoute.HkcuRegistration : LayerRoute.Environment,
                Displays = WindowsSystem.Displays(),
                RuntimeProbe = ProbeRuntime(g.LayerDecisions),
                Controls = Controls,
            });
            if (TestMode) plan.ExePath = Options.TestExe;
            if (plan.RenderSize?.Note != null) Log.Warn(plan.RenderSize.Note + " (" + plan.RenderSize.Reason + ")");
            return plan;
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
