using System;
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
    /// </summary>
    internal sealed class UpdateDialog : Form
    {
        private readonly AvailableRelease release;
        private readonly string programDir;
        private readonly string updatesDir;
        private readonly Action<string> log;
        private readonly Button install = new Button { Text = "Download and install", AutoSize = true, MinimumSize = new Size(150, 30) };
        private readonly Button skip = new Button { Text = "Skip this version", AutoSize = true, MinimumSize = new Size(120, 30) };
        private readonly Button later = new Button { Text = "Later", Width = 90, Height = 30 };
        private readonly ProgressBar bar = new ProgressBar { Width = 460, Height = 18, Maximum = 1000, Visible = false };
        private readonly Label status = new Label { AutoSize = true, MaximumSize = new Size(460, 0) };
        private CancellationTokenSource running;
        /// <summary>True while the release is unpacked and its files replaced: not cancelled half way.</summary>
        private bool installing;

        public UpdateChoice Choice { get; private set; } = UpdateChoice.Later;

        public UpdateDialog(AvailableRelease release, string current, string programDir, string updatesDir, string cannotInstall, Action<string> log)
        {
            this.release = release;
            this.programDir = programDir;
            this.updatesDir = updatesDir;
            this.log = log;
            Text = "EternalVR update";
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
            page.LinkClicked += (s, e) => Process.Start(release.Page.AbsoluteUri);
            if (cannotInstall != null)
            {
                install.Enabled = false;
                status.Text = cannotInstall;
            }
            var buttons = new FlowLayoutPanel { AutoSize = true, FlowDirection = FlowDirection.RightToLeft, Dock = DockStyle.Fill, Margin = new Padding(0, 8, 0, 0) };
            buttons.Controls.AddRange(new Control[] { later, skip, install });
            var layout = new TableLayoutPanel { AutoSize = true, ColumnCount = 1, Dock = DockStyle.Fill };
            layout.Controls.AddRange(new Control[] { title, text, page, bar, status, buttons });
            Controls.Add(layout);
            CancelButton = later;

            install.Click += (s, e) => Install();
            skip.Click += (s, e) => { Choice = UpdateChoice.Skip; Close(); };
            later.Click += (s, e) => Close();
            FormClosing += (s, e) =>
            {
                // Once the files are being replaced, the dialog stays until that is done (it is quick).
                if (installing) e.Cancel = true;
                else running?.Cancel();
            };
        }

        private async void Install()
        {
            install.Enabled = skip.Enabled = false;
            bar.Visible = true;
            status.ForeColor = SystemColors.ControlText;
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
                status.Text = "Checking and installing...";
                bar.Style = ProgressBarStyle.Marquee;
                var version = release.Version;
                installing = true;
                var installed = await Task.Run(() =>
                {
                    UpdatePackage.Stage(zip, staging, version);
                    return UpdatePackage.Install(staging, programDir);
                });
                installing = false;
                log($"update: installed EternalVR {release.Version} over {programDir} ({installed.Count} files)");
                Choice = UpdateChoice.Installed;
                TryClean(Path.Combine(updatesDir, release.Version.ToString()));
                DialogResult = DialogResult.OK;
            }
            catch (OperationCanceledException)
            {
                log("update: cancelled");
            }
            catch (Exception e) when (e is HttpRequestException || e is IOException || e is UnauthorizedAccessException || e is InvalidDataException)
            {
                installing = false;
                log("update failed: " + e.Message);
                TryClean(Path.Combine(updatesDir, release.Version.ToString()));
                if (IsDisposed) return;
                bar.Visible = false;
                bar.Style = ProgressBarStyle.Continuous;
                status.ForeColor = Color.Firebrick;
                status.Text = "The update failed: " + e.Message + " Nothing was changed; you can also download it from the release page.";
                install.Text = "Try again";
                install.Enabled = skip.Enabled = true;
            }
        }

        private void TryClean(string dir)
        {
            try { FileUtil.DeleteDirectory(dir); }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException) { log("update: could not remove " + dir + ": " + e.Message); }
        }
    }
}
