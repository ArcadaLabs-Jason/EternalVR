using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Net.Http;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;
using EternalVR.Launcher.Core;
using EternalVR.Launcher.Core.Update;

namespace EternalVR.Launcher
{
    /// <summary>What the player chose in the <see cref="UpdateDialog"/>.</summary>
    internal enum UpdateChoice { Later, Skip, Installed }

    /// <summary>
    /// Offers a newer release (<see cref="UpdateClient"/>): its title and first paragraph, a link to its page, and Download
    /// and install, Skip this version or Later. Installing downloads the zip (checked against GitHub's SHA-256), unpacks and
    /// checks it (<see cref="UpdatePackage.Stage"/>) and installs it over the launcher's folder; the data folder is kept.
    /// When it cannot be installed (<see cref="InstallBlock"/>) the reason is shown in bold next to the buttons.
    /// </summary>
    internal sealed class UpdateDialog : Form
    {
        private readonly AvailableRelease release;
        private readonly string programDir;
        private readonly string updatesDir;
        private readonly Func<InstallBlock> whyNot;
        private readonly Action<string> log;
        // Sizes at 96 DPI: the scaling pass at ResumeLayout (below) scales them, minimum sizes included, to the display's. The
        // buttons grow and shrink to their text (by default they only grow, and kept twice their size after that pass).
        private readonly Button install = Sized(InstallText, 150);
        private readonly Button skip = Sized("Skip this version", 120);
        private readonly Button later = Sized("Later", 90);
        private readonly ProgressBar bar = new ProgressBar { Width = 460, Height = 18, Maximum = 1000, Visible = false };
        /// <summary>Why it cannot be installed now (<see cref="InstallBlock"/>), next to the buttons; hidden when it can.</summary>
        private readonly Label blocked = new Label
        {
            AutoSize = true, MaximumSize = new Size(460, 0), Visible = false, ForeColor = MainForm.WarningText,
            Font = MainForm.UiFont(FontStyle.Bold), Margin = new Padding(0, 3, 0, 0),
        };
        /// <summary>The download's progress and what went wrong; hidden until Download and install.</summary>
        private readonly Label status = new Label { AutoSize = true, MaximumSize = new Size(460, 0), Visible = false, Margin = new Padding(0, 3, 0, 3) };
        private const string InstallText = "Download and install";
        private InstallBlock block;
        private CancellationTokenSource running;
        /// <summary>True while the release is unpacked and its files replaced: not cancelled half way.</summary>
        private bool installing;

        public UpdateChoice Choice { get; private set; } = UpdateChoice.Later;

        /// <param name="whyNot">Why the release cannot be installed now; asked again before installing and, while the game runs, when the dialog is activated.</param>
        public UpdateDialog(AvailableRelease release, string current, string programDir, string updatesDir, Func<InstallBlock> whyNot, Action<string> log)
        {
            this.release = release;
            this.programDir = programDir;
            this.updatesDir = updatesDir;
            this.whyNot = whyNot;
            this.log = log;
            Text = "EternalVR update";
            // Without the suspended layout WinForms never runs the scaling pass here: at 200 % the fixed sizes stayed at 96 DPI.
            SuspendLayout();
            AutoScaleDimensions = new SizeF(96F, 96F);
            AutoScaleMode = AutoScaleMode.Dpi;
            Font = MainForm.UiFont();
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = MinimizeBox = false;
            ShowInTaskbar = false;
            StartPosition = FormStartPosition.CenterParent;
            AutoSize = true;
            AutoSizeMode = AutoSizeMode.GrowAndShrink;
            Padding = new Padding(12);

            var title = new Label { Text = release.Title, AutoSize = true, Font = MainForm.UiFont(FontStyle.Bold), Margin = new Padding(0, 0, 0, 6) };
            var summary = ReleaseFeed.Summary(release.Notes);
            var text = new Label
            {
                AutoSize = true, MaximumSize = new Size(460, 0), Margin = new Padding(0, 0, 0, 8),
                Text = $"You have EternalVR {current}. " + (summary.Length > 0 ? summary + " " : string.Empty)
                    + "Installing replaces the files in the launcher's folder with the new release's; your settings, controls, "
                    + "profiles and backups in EternalVR's data folder stay as they are.",
            };
            var page = new LinkLabel { Text = "What's new (the release page)", AutoSize = true, Margin = new Padding(0, 0, 0, 10) };
            page.LinkClicked += (s, e) => OpenPage();
            var buttons = new FlowLayoutPanel { AutoSize = true, FlowDirection = FlowDirection.RightToLeft, Dock = DockStyle.Fill, Margin = new Padding(0, 8, 0, 0) };
            buttons.Controls.AddRange(new Control[] { later, skip, install });
            var layout = new TableLayoutPanel { AutoSize = true, ColumnCount = 1, Dock = DockStyle.Fill };
            layout.Controls.AddRange(new Control[] { title, text, page, bar, status, blocked, buttons });
            Controls.Add(layout);
            CancelButton = later;
            ShowBlock(whyNot());
            DisplayScale.Follow(this);
            ResumeLayout(false);
            PerformLayout();

            install.Click += (s, e) =>
            {
                if (block != null && block.Lasting) OpenPage();
                else Install();
            };
            skip.Click += (s, e) => { Choice = UpdateChoice.Skip; Close(); };
            later.Click += (s, e) => Close();
            // The game quit (or started) while the dialog was open.
            Activated += (s, e) => { if (running == null && block?.Lasting != true) ShowBlock(whyNot()); };
            FormClosing += (s, e) =>
            {
                // Once the files are being replaced, the dialog stays until that is done (it is quick).
                if (installing) e.Cancel = true;
                else running?.Cancel();
            };
        }

        /// <summary>Shows why it cannot be installed (a lasting reason turns the first button into the release page), or that it can.</summary>
        private void ShowBlock(InstallBlock why)
        {
            block = why;
            blocked.Text = why?.Text ?? string.Empty;
            blocked.Visible = why != null;
            install.Text = why != null && why.Lasting ? "Open release page" : InstallText;
            install.Enabled = why == null || why.Lasting;
        }

        private static Button Sized(string text, int minWidth) =>
            new Button { Text = text, AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, MinimumSize = new Size(minWidth, 30) };

        private void OpenPage()
        {
            try { Process.Start(release.Page.AbsoluteUri); }
            catch (Exception e) when (e is System.ComponentModel.Win32Exception || e is InvalidOperationException) { log("update: could not open " + release.Page + ": " + e.Message); }
        }

        private async void Install()
        {
            // The game may have started since the dialog opened.
            ShowBlock(whyNot());
            if (block != null) return;
            install.Enabled = skip.Enabled = false;
            bar.Visible = true;
            status.ForeColor = SystemColors.ControlText;
            status.Visible = true;
            status.Text = "Downloading " + release.ZipName + "...";
            running = new CancellationTokenSource();
            var staging = Path.Combine(updatesDir, release.Version.ToString(), "staging");
            string zip = null;
            try
            {
                using (var http = UpdateClient.NewHttp(typeof(UpdateDialog).Assembly.GetName().Version))
                {
                    var progress = new Progress<double>(p => bar.Value = (int)Math.Round(Math.Min(1.0, p) * bar.Maximum));
                    zip = await UpdateClient.DownloadAsync(release, updatesDir, http, progress, running.Token);
                }
                log("update: downloaded " + zip + " (SHA-256 checked)");
                // The game may have started during the download: nothing is replaced while it runs.
                var late = whyNot();
                if (late != null)
                {
                    log("update: not installed after the download: " + late.Text);
                    TryClean(Path.Combine(updatesDir, release.Version.ToString()));
                    bar.Visible = false;
                    status.Visible = false;
                    running = null;
                    skip.Enabled = true;
                    ShowBlock(late);
                    return;
                }
                status.Text = "Checking and installing...";
                bar.Style = ProgressBarStyle.Marquee;
                var version = release.Version;
                installing = true;
                // Closing waits for the install: Later does nothing until then.
                later.Enabled = false;
                IReadOnlyList<string> installed, removed = null;
                try
                {
                    installed = await Task.Run(() =>
                    {
                        UpdatePackage.Stage(zip, staging, version);
                        return UpdatePackage.Install(staging, programDir, out removed);
                    });
                }
                finally
                {
                    // Whatever went wrong, the dialog can be closed again (Install puts the old files back itself).
                    installing = false;
                    later.Enabled = true;
                }
                // The files are replaced: from here on the new release is the one on disk, and closing restarts into it.
                Choice = UpdateChoice.Installed;
                log($"update: installed EternalVR {release.Version} over {programDir} ({installed.Count} files)");
                if (removed != null && removed.Count > 0)
                    log("update: removed what the new release no longer ships: " + string.Join(", ", removed));
                TryClean(Path.Combine(updatesDir, release.Version.ToString()));
                DialogResult = DialogResult.OK;
            }
            catch (OperationCanceledException)
            {
                log("update: cancelled");
            }
            catch (Exception e) when (e is HttpRequestException || e is IOException || e is UnauthorizedAccessException || e is InvalidDataException)
            {
                log("update failed: " + e.Message);
                Failed(e.Message);
            }
            catch (Exception e)
            {
                // Anything else (a path or security error) is shown here too: it must not escape this async void.
                log("update failed: " + e);
                Failed(e.GetType().Name + ": " + e.Message);
            }
        }

        private void Failed(string why)
        {
            TryClean(Path.Combine(updatesDir, release.Version.ToString()));
            if (IsDisposed) return;
            bar.Visible = false;
            bar.Style = ProgressBarStyle.Continuous;
            status.Visible = true;
            status.ForeColor = Color.Firebrick;
            if (Choice == UpdateChoice.Installed)
            {
                // The files were replaced before the failure: nothing to try again, and closing restarts into the new release.
                status.Text = $"EternalVR {release.Version} is installed; a step after it failed: {why} The launcher restarts with it when this window closes.";
                install.Visible = skip.Visible = false;
                later.Text = "Close";
                return;
            }
            status.Text = "The update failed: " + why + " Nothing was changed; you can also download it from the release page.";
            install.Text = "Try again";
            install.Enabled = skip.Enabled = true;
        }

        private void TryClean(string dir)
        {
            try { FileUtil.DeleteDirectory(dir); }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException) { log("update: could not remove " + dir + ": " + e.Message); }
        }
    }
}
