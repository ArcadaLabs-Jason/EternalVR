using System;
using System.Diagnostics;
using System.Drawing;
using System.Net.Http;
using System.Threading;
using System.Windows.Forms;
using EternalVR.Launcher.Core.Settings;

namespace EternalVR.Launcher
{
    /// <summary>
    /// Downloads NVIDIA's DLSS DLL on the player's request (<see cref="DlssDownloads"/>): says where the file comes from and
    /// that NVIDIA's license applies, links the license, and downloads only once the player accepts it. The file is checked
    /// against its pinned size and SHA-256; <see cref="FilePath"/> is set when it is ready.
    /// </summary>
    internal sealed class DlssDownloadDialog : Form
    {
        private readonly DlssRelease release;
        private readonly string dlssRoot;
        private readonly Action<string> log;
        private readonly Button accept = new Button { Text = "Accept and download", AutoSize = true, MinimumSize = new Size(150, 30) };
        private readonly Button cancel = new Button { Text = "Cancel", Width = 90, Height = 30, DialogResult = DialogResult.Cancel };
        private readonly ProgressBar bar = new ProgressBar { Width = 440, Height = 18, Maximum = 1000, Visible = false };
        private readonly Label status = new Label { AutoSize = true, MaximumSize = new Size(440, 0) };
        private CancellationTokenSource running;

        /// <summary>The downloaded file, checked; null until the download has finished.</summary>
        public string FilePath { get; private set; }

        public DlssDownloadDialog(DlssRelease release, string dlssRoot, Action<string> log)
        {
            this.release = release;
            this.dlssRoot = dlssRoot;
            this.log = log;
            Text = "Download DLSS from NVIDIA";
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

            var text = new Label
            {
                AutoSize = true, MaximumSize = new Size(440, 0), Margin = new Padding(0, 0, 0, 8),
                Text = $"The launcher can download DLSS {release.Version.ToString(3)} ({release.SizeText}) for you, straight from NVIDIA's "
                    + "own GitHub repository (github.com/NVIDIA/DLSS). EternalVR does not ship this file: it is NVIDIA's, and NVIDIA's "
                    + "RTX SDK license applies to it. The file is kept in EternalVR's data folder and used for \"DLSS version: "
                    + "From a file\".",
            };
            var license = new LinkLabel { Text = "Read NVIDIA's license", AutoSize = true, Margin = new Padding(0, 0, 0, 10) };
            license.LinkClicked += (s, e) => Process.Start(release.License.AbsoluteUri);
            var buttons = new FlowLayoutPanel { AutoSize = true, FlowDirection = FlowDirection.RightToLeft, Dock = DockStyle.Fill, Margin = new Padding(0, 8, 0, 0) };
            buttons.Controls.AddRange(new Control[] { cancel, accept });
            var layout = new TableLayoutPanel { AutoSize = true, ColumnCount = 1, Dock = DockStyle.Fill };
            layout.Controls.AddRange(new Control[] { text, license, bar, status, buttons });
            Controls.Add(layout);
            AcceptButton = accept;
            CancelButton = cancel;

            accept.Click += (s, e) => Download();
            FormClosing += (s, e) => running?.Cancel();
        }

        private async void Download()
        {
            accept.Enabled = false;
            bar.Visible = true;
            bar.Value = 0;
            status.ForeColor = SystemColors.ControlText;
            status.Text = "Downloading...";
            running = new CancellationTokenSource();
            log($"DLSS download: {release.Url} (version {release.Version}), NVIDIA's license accepted");
            try
            {
                using (var http = new HttpClient { Timeout = Timeout.InfiniteTimeSpan })
                {
                    http.DefaultRequestHeaders.UserAgent.ParseAdd("EternalVR-Launcher/" + typeof(DlssDownloadDialog).Assembly.GetName().Version.ToString(3));
                    var progress = new Progress<double>(p => bar.Value = (int)Math.Round(Math.Min(1.0, p) * bar.Maximum));
                    FilePath = await DlssDownloads.FetchAsync(release, dlssRoot, http, progress, running.Token);
                }
                log("DLSS download: " + FilePath + " is NVIDIA's file (size and SHA-256 checked)");
                DialogResult = DialogResult.OK;
            }
            catch (OperationCanceledException)
            {
                log("DLSS download: cancelled");
            }
            catch (Exception e) when (e is HttpRequestException || e is System.IO.IOException || e is UnauthorizedAccessException)
            {
                log("DLSS download failed: " + e.Message);
                if (IsDisposed) return;
                bar.Visible = false;
                status.ForeColor = Color.Firebrick;
                status.Text = "The download failed: " + e.Message + " Nothing was kept.";
                accept.Text = "Try again";
                accept.Enabled = true;
            }
        }
    }
}
