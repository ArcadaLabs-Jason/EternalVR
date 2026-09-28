using System;
using System.Diagnostics;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;
using EternalVR.Launcher.Core.Game;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Preflight;
using EternalVR.Launcher.Core.Safety;
using EternalVR.Launcher.Core.Settings;

namespace EternalVR.Launcher
{
    /// <summary>The launcher window: game folder, a few VR settings, preflight, Launch VR and the log.</summary>
    public sealed partial class MainForm : Form
    {
        private readonly LauncherContext ctx;
        private readonly SessionRunner runner;
        private readonly CancellationTokenSource closing = new CancellationTokenSource();
        private readonly System.Windows.Forms.Timer recoveryTimer = new System.Windows.Forms.Timer { Interval = 5000 };
        private Task session;
        /// <summary>A running "Restore saves" (it may start and close the game briefly).</summary>
        private Task<SaveRestoreResult> saveRestore;
        /// <summary>The settings tabs: locked while a session runs, whose plan reads the same settings.</summary>
        private TabPage playPage, advancedPage;
        /// <summary>The branding strip across the top, with the game's state and the version.</summary>
        private readonly HeaderStrip header = new HeaderStrip(Branding.HeaderStrip()) { Dock = DockStyle.Top, Height = 120 };
        private readonly TabControl tabs = new TabControl { Dock = DockStyle.Fill, SizeMode = TabSizeMode.Fixed };

        private readonly ListBox checks = new ListBox { Height = 120, Dock = DockStyle.Fill, HorizontalScrollbar = true };
        private readonly TextBox logBox = new TextBox
        {
            Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Both, WordWrap = false, Dock = DockStyle.Fill,
            Font = new Font(FontFamily.GenericMonospace, 8.5f),
        };
        private readonly Button launch = new Button { Text = "Launch VR", Width = 140, Height = 36, Font = UiFont(FontStyle.Bold) };
        private readonly Button check = new Button { Text = "Check", Width = 90, Height = 36 };
        private readonly Button restoreSaves = new Button { Text = "Restore saves...", Width = 120, Height = 36 };
        private readonly Button openData = new Button { Text = "Open data folder", Width = 120, Height = 36 };
        private readonly Button exportReport = new Button { Text = "Export report...", Width = 120, Height = 36 };

        public MainForm(LauncherContext ctx)
        {
            this.ctx = ctx;
            runner = new SessionRunner(ctx);
            Text = "EternalVR Launcher " + typeof(MainForm).Assembly.GetName().Version.ToString(3);
            // Sizes below are at 96 DPI; the form scales them to the display's (the manifest is per-monitor DPI aware).
            SuspendLayout();
            AutoScaleDimensions = new SizeF(96F, 96F);
            AutoScaleMode = AutoScaleMode.Dpi;
            // Fits a 1080p display at 150 %.
            Width = 720;
            Height = 660;
            StartPosition = FormStartPosition.CenterScreen;
            Icon = Branding.WindowIcon() ?? Icon;
            BuildLayout();
            // After the layout: the change reaches every control in the window, and the sizes they measured with the default
            // font are measured again (at 100 % no scaling pass does it).
            Font = UiFont();
            FitListHeights(this);
            LoadSettingsIntoControls();
            ResumeLayout(false);
            PerformLayout();
            Load += (s, e) => { FitTabs(); FitToScreen(); };
            DpiChanged += (s, e) => BeginInvoke(new Action(FitTabs));

            ctx.Log.Line += AppendLog;
            runner.Status += ShowStatus;
            runner.GameStarted = () => FinishSession.Start(ctx);
            runner.ConfirmWithoutHeadset = problem => (bool)Invoke(new Func<bool>(() =>
                MessageBox.Show(this, problem + "\n\nLaunch anyway? VR starts once the headset is found, and the late size change "
                    + "can fail on graphics cards with 12 GB or less.",
                    "Headset not found", MessageBoxButtons.YesNo, MessageBoxIcon.Warning, MessageBoxDefaultButton.Button2) == DialogResult.Yes));
            launch.Click += (s, e) => StartSession();
            check.Click += (s, e) => RunPreflight();
            restoreSaves.Click += (s, e) => RestoreSaves();
            openData.Click += (s, e) => Process.Start("explorer.exe", "\"" + ctx.Paths.Root + "\"");
            exportReport.Click += (s, e) => ReportExport.Run(this, ctx);
            discardRestore.Click += (s, e) => DiscardPendingRestore();
            recoveryTimer.Tick += (s, e) => TryRecover();
            FormClosing += OnClosing;
            Shown += (s, e) =>
            {
                ctx.Log.Info($"EternalVR launcher started; data folder {ctx.Paths.Root}");
                TryRecover();
                RunPreflight();
            };
        }

        /// <summary>
        /// The window's font: Segoe UI 9 in points. Not <see cref="Control.DefaultFont"/>, whose pixel size is the one of the
        /// display scale Windows had at sign-in: after a scale change without signing out, every text was that much too big
        /// or too small. Points follow the display the window opens on; WinForms rescales them when it moves.
        /// </summary>
        private static Font UiFont(FontStyle style = FontStyle.Regular) => new Font("Segoe UI", 9f, style);

        /// <summary>
        /// A list keeps the height it got with the font it was made with (the default font, too tall here), and a layout
        /// takes that height: each list is set to its height in the window's font. The rows hold lists in flow panels, which
        /// follow the new height (a table row keeps the first one).
        /// </summary>
        private static void FitListHeights(Control parent)
        {
            foreach (Control c in parent.Controls)
            {
                if (c is ComboBox list) list.Height = list.PreferredHeight;
                FitListHeights(c);
            }
        }

        /// <summary>
        /// The tab headers at the width of their titles in the scaled font (measured by the tab control itself, the headers
        /// keep the size of the unscaled font and the titles overlap).
        /// </summary>
        private void FitTabs()
        {
            int width = 0;
            foreach (TabPage page in tabs.TabPages)
                width = Math.Max(width, TextRenderer.MeasureText(page.Text, tabs.Font).Width);
            int pad = tabs.Font.Height;
            tabs.ItemSize = new Size(width + pad * 2, tabs.Font.Height + pad / 2 + 4);
        }

        /// <summary>A scaled window taller or wider than the screen's working area is shrunk to it (the log box takes the loss).</summary>
        private void FitToScreen()
        {
            var area = Screen.FromControl(this).WorkingArea;
            Width = Math.Min(Width, area.Width);
            Height = Math.Min(Height, area.Height);
            Left = Math.Max(area.Left, area.Left + (area.Width - Width) / 2);
            Top = Math.Max(area.Top, area.Top + (area.Height - Height) / 2);
        }

        private void BuildLayout()
        {
            var root = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, Padding = new Padding(8) };
            // The one column is the window's width (an auto-sized column grows past it with the longest line).
            root.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));

            playPage = PlayTab();
            advancedPage = AdvancedTab();
            tabs.TabPages.AddRange(new[] { playPage, advancedPage, ChecksTab() });

            var buttons = new FlowLayoutPanel { AutoSize = true, Dock = DockStyle.Fill };
            buttons.Controls.AddRange(new Control[] { launch, check, restoreSaves, exportReport, discardRestore, statusLabel });
            buttons.SetFlowBreak(discardRestore, true);
            tips.SetToolTip(launch, "Starts DOOM Eternal in VR with these settings. Your game settings are put back when it exits.");
            tips.SetToolTip(check, "Checks the game, the mod and the headset runtime again.");
            tips.SetToolTip(restoreSaves, "Puts back your save slots from a backup (one is made before every VR launch).");
            tips.SetToolTip(exportReport, "Saves a report (logs and checks, personal paths removed) to attach to a bug report.");

            root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            root.Controls.Add(ProfileStrip());
            root.Controls.Add(tabs);
            root.Controls.Add(buttons);
            // The header docks first (the last added docks first), the rest fills below it.
            Controls.Add(root);
            Controls.Add(header);
        }

        /// <summary>The Checks and log tab: the preflight checks, the launcher's log and its data folder.</summary>
        private TabPage ChecksTab()
        {
            var page = new TabPage("Checks and log") { Padding = new Padding(6) };
            var grid = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1 };
            grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            grid.RowStyles.Add(new RowStyle(SizeType.Absolute, 130));
            grid.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            grid.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            grid.Controls.Add(checks);
            grid.Controls.Add(logBox);
            grid.Controls.Add(openData);
            page.Controls.Add(grid);
            return page;
        }

        private void FillRuntimes()
        {
            bool wasLoading = loading;
            loading = true;
            try
            {
                runtime.Items.Clear();
                runtime.Items.Add(new RuntimeItem(LauncherSettings.SystemRuntime, "System default (" + (Platform.WindowsSystem.ActiveOpenXrRuntime() ?? "none set") + ")"));
                foreach (var r in ctx.RuntimeChoices()) runtime.Items.Add(new RuntimeItem(r, r));
                if (!LaunchPlanBuilder.IsSystemRuntime(ctx.Settings.Runtime) && !runtime.Items.Cast<RuntimeItem>().Any(i => i.Value.Equals(ctx.Settings.Runtime, StringComparison.OrdinalIgnoreCase)))
                    runtime.Items.Add(new RuntimeItem(ctx.Settings.Runtime, ctx.Settings.Runtime));
                var selected = runtime.Items.Cast<RuntimeItem>().FirstOrDefault(i => i.Value.Equals(ctx.Settings.Runtime, StringComparison.OrdinalIgnoreCase));
                runtime.SelectedItem = selected ?? runtime.Items[0];
            }
            finally
            {
                loading = wasLoading;
            }
        }

        private void RunPreflight()
        {
            ReadControlsIntoSettings();
            var g = ctx.GatherPreflight();
            gameFolder.Text = g.Game == null ? "Not found: choose the DOOM Eternal folder." : g.Game.GameRoot;
            tips.SetToolTip(gameFolder, g.Game == null ? gameFolder.Text : g.Game.GameRoot + Environment.NewLine + $"(found from {g.Game.Source})");
            var build = g.Facts.Build;
            header.SetText(
                g.Game == null ? "DOOM Eternal not found" : "DOOM Eternal",
                build == null ? "Choose the game folder (Advanced)"
                : build.Status == BuildStatus.Known ? $"Build {build.Build.BuildId}: supported"
                : build.Status == BuildStatus.Unknown ? "Unknown game build: not supported"
                : "The game's exe is missing",
                build != null && build.Status == BuildStatus.Known,
                "EternalVR " + typeof(MainForm).Assembly.GetName().Version.ToString(3));
            checks.Items.Clear();
            foreach (var c in g.Result.Checks) checks.Items.Add(c.ToString());
            launch.Enabled = session == null && saveRestore == null && g.Result.CanLaunch;
        }

        private void StartSession()
        {
            SaveSettings();
            launch.Enabled = false;
            check.Enabled = false;
            restoreSaves.Enabled = false;
            playPage.Enabled = false;
            profileStrip.Enabled = false;
            advancedPage.Enabled = false;
            var token = closing.Token;
            session = Task.Run(() => runner.Run(token));
            session.ContinueWith(t =>
            {
                session = null;
                if (IsDisposed) return;
                if (t.IsFaulted)
                {
                    ctx.Log.Error("session failed: " + t.Exception?.GetBaseException());
                    ShowStatus(StatusKind.Problem, "The session stopped with an error: " + t.Exception?.GetBaseException().Message);
                }
                check.Enabled = true;
                restoreSaves.Enabled = true;
                playPage.Enabled = true;
                profileStrip.Enabled = true;
                advancedPage.Enabled = true;
                // A restore deferred because a game process was still running is retried from here.
                TryRecover();
                RunPreflight();
            }, TaskScheduler.FromCurrentSynchronizationContext());
        }

        private void TryRecover()
        {
            if (session != null) return;
            bool clear;
            try { clear = runner.Recover(); }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException)
            {
                ctx.Log.Error("the start-up clean-up failed, it is retried: " + e.Message);
                clear = false;
            }
            AfterRecovery(clear);
            recoveryTimer.Enabled = !clear;
            if (clear && checks.Items.Count > 0) RunPreflight();
        }

        private void RestoreSaves()
        {
            var running = ctx.RunningGameProcesses();
            if (running.Count > 0)
            {
                MessageBox.Show(this, "Quit the game first.", "Restore saves", MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }
            var backups = SaveBackups.List(ctx.Paths.SaveBackups);
            if (backups.Count == 0)
            {
                MessageBox.Show(this, "There are no save backups yet. One is made before every VR launch.", "Restore saves");
                return;
            }
            var chosen = ChooseBackup(backups);
            if (chosen == null) return;
            var when = BackupLabel(chosen);
            var answer = MessageBox.Show(this,
                $"Restore the save slots backed up on {when}?\n\nThe current saves are backed up first. Your saves are Steam Cloud files, so Steam must "
                + "take the restored ones: if its cloud record does not match them afterwards, the launcher starts the game briefly "
                + "(windowed, muted) and closes it after a few seconds, before it loads your profile. Steam must be running.",
                "Restore saves", MessageBoxButtons.OKCancel, MessageBoxIcon.Question);
            if (answer != DialogResult.OK) return;

            launch.Enabled = false;
            check.Enabled = false;
            restoreSaves.Enabled = false;
            var locations = ctx.SettingsLocations();
            var host = ctx.ResyncHost();
            var options = ctx.ResyncOptions();
            saveRestore = Task.Run(() => SaveRestore.Run(ctx.Paths.SaveBackups, chosen, DateTime.Now, locations, host, options, ctx.Log.Info));
            saveRestore.ContinueWith(task =>
            {
                saveRestore = null;
                if (IsDisposed) return;
                check.Enabled = true;
                restoreSaves.Enabled = true;
                RunPreflight();
                if (task.IsFaulted)
                {
                    var why = task.Exception?.GetBaseException().Message;
                    ctx.Log.Error("restoring saves failed: " + why);
                    MessageBox.Show(this, "Restoring the saves failed: " + why, "Restore saves", MessageBoxButtons.OK, MessageBoxIcon.Error);
                    return;
                }
                var r = task.Result;
                var icon = r.Outcome == SaveRestoreOutcome.Restored ? MessageBoxIcon.Information : MessageBoxIcon.Warning;
                var text = r.Summary + (r.PreRestoreDir == null ? string.Empty : "\n\nThe saves found before the restore are in " + r.PreRestoreDir);
                MessageBox.Show(this, text, "Restore saves", MessageBoxButtons.OK, icon);
            }, TaskScheduler.FromCurrentSynchronizationContext());
        }

        private void ChooseGameFolder()
        {
            using (var dlg = new FolderBrowserDialog { Description = "Choose the DOOM Eternal folder (the one containing " + GameLayout.RetailExe + ")" })
            {
                if (dlg.ShowDialog(this) != DialogResult.OK) return;
                if (!File.Exists(Path.Combine(dlg.SelectedPath, GameLayout.RetailExe)))
                {
                    MessageBox.Show(this, GameLayout.RetailExe + " is not in that folder.", "EternalVR");
                    return;
                }
                ctx.Settings.GameDir = dlg.SelectedPath;
                SaveSettings();
                RunPreflight();
            }
        }

        private void BrowseRuntime()
        {
            using (var dlg = new OpenFileDialog { Filter = "OpenXR runtime manifest (*.json)|*.json", Title = "Choose an OpenXR runtime manifest" })
            {
                if (dlg.ShowDialog(this) != DialogResult.OK) return;
                ctx.Settings.Runtime = dlg.FileName;
                FillRuntimes();
                SaveSettings();
                RunPreflight();
            }
        }

        private void AppendLog(string line)
        {
            if (IsDisposed) return;
            if (InvokeRequired)
            {
                try { BeginInvoke(new Action<string>(AppendLog), line); }
                catch (InvalidOperationException) { }
                return;
            }
            logBox.AppendText(line + Environment.NewLine);
        }

        private void OnClosing(object sender, FormClosingEventArgs e)
        {
            SaveSettings();
            if (saveRestore != null)
            {
                MessageBox.Show(this, "The saves are being restored. Wait until it has finished.", "EternalVR", MessageBoxButtons.OK, MessageBoxIcon.Information);
                e.Cancel = true;
                return;
            }
            if (session == null) return;
            var answer = MessageBox.Show(this,
                "The game is still running. If you close the launcher now, your settings are still restored automatically when the game exits.\n\nClose the launcher?",
                "EternalVR", MessageBoxButtons.YesNo, MessageBoxIcon.Warning);
            if (answer != DialogResult.Yes) { e.Cancel = true; return; }
            closing.Cancel();
        }

        private sealed class RuntimeItem
        {
            public RuntimeItem(string value, string label)
            {
                Value = value;
                Label = label;
            }

            public string Value { get; }
            public string Label { get; }
            public override string ToString() => Label;
        }
    }
}
