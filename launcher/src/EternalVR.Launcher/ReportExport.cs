using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using System.Windows.Forms;
using EternalVR.Launcher.Core.Game;
using EternalVR.Launcher.Core.Headsets;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Preflight;
using EternalVR.Launcher.Core.Report;
using EternalVR.Launcher.Core.Text;
using EternalVR.Launcher.Platform;

namespace EternalVR.Launcher
{
    /// <summary>
    /// "Export report..." (ROADMAP M4.5, T-095): gathers the machine facts for <see cref="ReportBuilder"/>, shows the
    /// file list and size, and saves the zip where the user chooses. Collection, redaction and the manifest are Core's.
    /// </summary>
    public static class ReportExport
    {
        /// <summary>Everything the report needs from this machine (registry and files read only).</summary>
        public static ReportInputs Gather(LauncherContext ctx)
        {
            var g = ctx.GatherPreflight();
            var layerVersion = ctx.LayerVersion();
            var active = WindowsSystem.ActiveOpenXrRuntime();
            var effective = ctx.EffectiveRuntime();
            var hags = WindowsSystem.HagsMode();
            var system = new List<KeyValuePair<string, string>>();
            void Add(string key, string value) => system.Add(new KeyValuePair<string, string>(key, value));

            Add("launcher version", LauncherContext.LauncherVersion);
            Add("layer version", layerVersion ?? "none (EternalVR.dll missing or without a version resource)");
            Add("version check", VersionCheck.Evaluate(LauncherContext.LauncherVersion, layerVersion, ctx.LayerDir).ToString());
            Add("windows", WindowsSystem.OsDescription());
            Add("cpu", WindowsSystem.CpuDescription());
            Add("memory", WindowsSystem.MemoryDescription());
            var adapters = WindowsSystem.DisplayAdapters();
            if (adapters.Count == 0) Add("gpu", null);
            foreach (var a in adapters) Add("gpu", a);
            // Where the desktop window can go: with no present scaling each eye is held to it.
            foreach (var d in WindowsSystem.Displays()) Add("display", d.ToString());
            Add("openxr active runtime", active == null ? "none set" : active + RuntimeName(active));
            Add("openxr runtime for launches", !LaunchPlanBuilder.IsSystemRuntime(ctx.Settings.Runtime) ? effective + RuntimeName(effective)
                : LauncherContext.InheritedRuntime != null ? LaunchPlanBuilder.RuntimeVariable + " in the environment: " + effective + RuntimeName(effective)
                : "system active");
            // The Play tab's Headset box as read (the last probe that answered) and the last session's refresh rate and summary.
            system.AddRange(HeadsetView.ReportLines(ctx.Headset, ctx.Identify(ctx.Headset.RuntimeName, ctx.Headset.SystemName)));
            system.AddRange(SteamVrSummary.Read(ctx.SteamRoot)); // chosen keys only, never the headset's serial number
            Add("last session eye size", ctx.LastRenderCap == null ? "not below the planned size (or no session yet)" : ctx.LastRenderCap.Describe());
            Add("hardware-accelerated GPU scheduling", hags == 2 ? "on (HwSchMode 2)" : hags.HasValue ? $"off (HwSchMode {hags.Value})" : "not set (off)");
            Add("game build", g.Facts.Build == null ? "game not found"
                : g.Facts.Platform == GamePlatform.GamePass ? g.Facts.Build.Status + " Game Pass " + g.Facts.Build.Version
                : g.Facts.Build.Status + " " + g.Facts.Build.Sha256);
            // A Game Pass or Microsoft Store game: its package as Windows has it (version, status) and the Xbox pieces it starts through.
            if (g.Facts.Platform == GamePlatform.GamePass) system.AddRange(StorePackageQuery.Read());
            system.AddRange(GameMods.Describe(g.Game?.GameRoot));
            Add("program folder", ctx.ProgramDir);
            Add("layer folder", ctx.LayerDir);
            Add("data folder", ctx.Paths.Root);

            return new ReportInputs
            {
                DataRoot = ctx.Paths.Root,
                ProgramDir = ctx.ProgramDir,
                LayerDir = ctx.LayerDir,
                System = system,
                Preflight = g.Result.Checks,
                UserProfile = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile),
                UserName = Environment.UserName,
                ComputerName = Environment.MachineName,
                SteamAccountIds = ctx.ActiveAccount == null ? new string[0] : new[] { ctx.ActiveAccount },
                // The game's Saved Games folder as the settings snapshot knows it (Steam and Game Pass alike).
                GameSavedGamesDirs = (g.Locations ?? new SettingsLocation[0])
                    .Where(l => l.Kind == SettingsLocationKind.SavedGames).Select(l => l.Path).ToList(),
                WindowsEvents = WindowsEventLogs.Read(),
                WindowsErrorReportDirs = new[] { Environment.SpecialFolder.CommonApplicationData, Environment.SpecialFolder.LocalApplicationData }
                    .Select(f => Path.Combine(Environment.GetFolderPath(f), "Microsoft", "Windows", "WER")).ToList(),
                Now = DateTime.Now,
                Id = ReportBuilder.NewId(),
            };
        }

        /// <summary>
        /// The window's button: build (on a worker thread: the checks, the event logs and up to about 70 MB of files to
        /// zip), confirm with the file list and size, choose where to save, write.
        /// </summary>
        public static async Task Run(Control owner, LauncherContext ctx)
        {
            ReportResult report;
            try { report = await Task.Run(() => ReportBuilder.Build(Gather(ctx))); }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException)
            {
                ctx.Log.Error("building the report failed: " + e.Message);
                if (!owner.IsDisposed)
                    MessageBox.Show(owner, "The report could not be built: " + e.Message, "Export report", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return;
            }
            if (owner.IsDisposed) return;

            using (var dlg = new SaveFileDialog
            {
                Title = "Save the EternalVR report (personal info removed)",
                Filter = "Zip file (*.zip)|*.zip",
                FileName = ReportBuilder.DefaultFileName(DateTime.Now, report.Id, LauncherContext.LauncherVersion),
                InitialDirectory = Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory),
                OverwritePrompt = true,
            })
            {
                if (dlg.ShowDialog(owner) != DialogResult.OK) return;
                try
                {
                    File.WriteAllBytes(dlg.FileName, report.Zip);
                    ctx.Log.Info($"report {report.Id} saved: {dlg.FileName} ({report.Files.Count} files, {report.Zip.Length} bytes)");
                    // The folder opens with the zip selected: a player who clicked through the dialog could not find it.
                    try { System.Diagnostics.Process.Start("explorer.exe", "/select,\"" + dlg.FileName + "\""); }
                    catch (Exception e) when (e is System.ComponentModel.Win32Exception || e is InvalidOperationException)
                    {
                        ctx.Log.Warn("could not open the report's folder: " + e.Message);
                    }
                    MessageBox.Show(owner, "Saved, personal info removed. Attach it to your GitHub issue or Discord message.\n\n" + dlg.FileName,
                        "Export report", MessageBoxButtons.OK, MessageBoxIcon.Information);
                }
                catch (Exception e) when (e is IOException || e is UnauthorizedAccessException)
                {
                    ctx.Log.Error("saving the report failed: " + e.Message);
                    MessageBox.Show(owner, "Saving the report failed: " + e.Message, "Export report", MessageBoxButtons.OK, MessageBoxIcon.Error);
                }
            }
        }

        /// <summary>The command-line form: writes the zip to <paramref name="path"/> and prints its contents.</summary>
        public static int RunHeadless(LauncherContext ctx, string path)
        {
            var report = ReportBuilder.Build(Gather(ctx));
            Directory.CreateDirectory(Path.GetDirectoryName(path));
            File.WriteAllBytes(path, report.Zip);
            Console.WriteLine("report ID: " + report.Id);
            Console.WriteLine(report.Describe());
            ctx.Log.Info($"report {report.Id} saved: {path} ({report.Files.Count} files, {report.Zip.Length} bytes)");
            return 0;
        }

        /// <summary>" (name)" from a runtime manifest's <c>runtime.name</c>, or empty.</summary>
        private static string RuntimeName(string manifest)
        {
            try
            {
                if (string.IsNullOrEmpty(manifest) || !File.Exists(manifest)) return " (manifest missing)";
                var name = MiniJson.Get(MiniJson.Parse(File.ReadAllText(manifest)), "runtime", "name") as string;
                return string.IsNullOrEmpty(name) ? string.Empty : " (" + name + ")";
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException || e is FormatException)
            {
                return " (manifest unreadable)";
            }
        }
    }
}
