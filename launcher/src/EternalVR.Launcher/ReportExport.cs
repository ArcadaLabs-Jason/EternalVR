using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Windows.Forms;
using EternalVR.Launcher.Core.Game;
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
            var adapters = WindowsSystem.DisplayAdapters();
            if (adapters.Count == 0) Add("gpu", null);
            foreach (var a in adapters) Add("gpu", a);
            Add("openxr active runtime", active == null ? "none set" : active + RuntimeName(active));
            Add("openxr runtime for launches", LaunchPlanBuilder.IsSystemRuntime(ctx.Settings.Runtime) ? "system active" : effective + RuntimeName(effective));
            Add("hardware-accelerated GPU scheduling", hags == 2 ? "on (HwSchMode 2)" : hags.HasValue ? $"off (HwSchMode {hags.Value})" : "not set (off)");
            Add("game build", g.Facts.Build == null ? "game not found"
                : g.Facts.Platform == GamePlatform.GamePass ? g.Facts.Build.Status + " Game Pass " + g.Facts.Build.Version
                : g.Facts.Build.Status + " " + g.Facts.Build.Sha256);
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
                Now = DateTime.Now,
            };
        }

        /// <summary>The window's button: build, confirm with the file list and size, choose where to save, write.</summary>
        public static void Run(IWin32Window owner, LauncherContext ctx)
        {
            ReportResult report;
            try { report = ReportBuilder.Build(Gather(ctx)); }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException)
            {
                ctx.Log.Error("building the report failed: " + e.Message);
                MessageBox.Show(owner, "The report could not be built: " + e.Message, "Export report", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return;
            }

            var answer = MessageBox.Show(owner,
                "The report holds these files. Your user folder, user name, computer name and Steam account ID are replaced by placeholders; "
                + "no saves, memory dumps or game settings files are included.\n\n" + report.Describe() + "\nSave it?",
                "Export report", MessageBoxButtons.OKCancel, MessageBoxIcon.Information);
            if (answer != DialogResult.OK) return;

            using (var dlg = new SaveFileDialog
            {
                Title = "Save the EternalVR report",
                Filter = "Zip file (*.zip)|*.zip",
                FileName = ReportBuilder.DefaultFileName(DateTime.Now),
                InitialDirectory = Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory),
                OverwritePrompt = true,
            })
            {
                if (dlg.ShowDialog(owner) != DialogResult.OK) return;
                try
                {
                    File.WriteAllBytes(dlg.FileName, report.Zip);
                    ctx.Log.Info($"report saved: {dlg.FileName} ({report.Files.Count} files, {report.Zip.Length} bytes)");
                    MessageBox.Show(owner, "Saved " + dlg.FileName + ".\n\nAttach it to your GitHub issue.", "Export report", MessageBoxButtons.OK, MessageBoxIcon.Information);
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
            Console.WriteLine(report.Describe());
            ctx.Log.Info($"report saved: {path} ({report.Files.Count} files, {report.Zip.Length} bytes)");
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
