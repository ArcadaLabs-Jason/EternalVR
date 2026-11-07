using System;
using System.Diagnostics;
using System.Drawing;
using System.Globalization;
using System.Windows.Forms;
using EternalVR.Launcher.Core.Update;

namespace EternalVR.Launcher
{
    /// <summary>
    /// A newer test build ("Get test builds" on, <see cref="TestBuildFeed"/>): its title and first paragraph, what a test build
    /// is, and its page. Nothing is downloaded or installed from here: the player gets the zip from the page and unzips it into
    /// a folder of its own.
    /// </summary>
    internal sealed class TestBuildDialog : Form
    {
        private readonly TestBuild build;
        private readonly Action<string> log;
        // Sizes at 96 DPI, scaled by the pass at ResumeLayout; the buttons grow and shrink to their text.
        private readonly Button open = Sized("Open its page", 120);
        private readonly Button close = Sized("Close", 90);

        public TestBuildDialog(TestBuild build, string current, Action<string> log)
        {
            this.build = build;
            this.log = log;
            Text = "EternalVR test build";
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

            // The title and notes are the release's own text: an & in them is shown, not taken for a keyboard shortcut.
            var title = new Label
            {
                Text = build.Title, AutoSize = true, MaximumSize = new Size(460, 0), Font = MainForm.UiFont(FontStyle.Bold), Margin = new Padding(0, 0, 0, 6),
                UseMnemonic = false,
            };
            var summary = ReleaseFeed.Summary(build.Notes);
            var facts = new Label
            {
                AutoSize = true, MaximumSize = new Size(460, 0), Margin = new Padding(0, 0, 0, 8), UseMnemonic = false,
                Text = $"Test build {build.Name}, published {build.Published.ToLocalTime().ToString("yyyy-MM-dd HH:mm", CultureInfo.InvariantCulture)}, "
                    + $"built from commit {build.Sha}. You have EternalVR {current}."
                    + (summary.Length > 0 ? " " + summary : string.Empty),
            };
            var what = new Label
            {
                AutoSize = true, MaximumSize = new Size(460, 0), Margin = new Padding(0, 0, 0, 8),
                Text = "A test build is an early version of the next release, for players who want to try new fixes and features "
                    + "first. It is not installed over your EternalVR: download its zip from its page and unzip it into a folder of "
                    + "its own; your EternalVR folder stays as it is. Its notes say what to try; post how it went in the Discord "
                    + "thread they link.",
            };
            var page = new LinkLabel { Text = "Its page on GitHub (notes and download)", AutoSize = true, Margin = new Padding(0, 0, 0, 10) };
            page.LinkClicked += (s, e) => OpenPage();
            var buttons = new FlowLayoutPanel { AutoSize = true, FlowDirection = FlowDirection.RightToLeft, Dock = DockStyle.Fill, Margin = new Padding(0, 8, 0, 0) };
            buttons.Controls.AddRange(new Control[] { close, open });
            var layout = new TableLayoutPanel { AutoSize = true, ColumnCount = 1, Dock = DockStyle.Fill };
            layout.Controls.AddRange(new Control[] { title, facts, what, page, buttons });
            Controls.Add(layout);
            CancelButton = close;
            DisplayScale.Follow(this);
            ResumeLayout(false);
            PerformLayout();

            open.Click += (s, e) =>
            {
                OpenPage();
                Close();
            };
            close.Click += (s, e) => Close();
        }

        private static Button Sized(string text, int minWidth) =>
            new Button { Text = text, AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, MinimumSize = new Size(minWidth, 30) };

        private void OpenPage()
        {
            // Only a page of the test repository is opened (the feed checks it too, TestBuildFeed.IsTestPage).
            var page = TestBuildFeed.IsTestPage(build.Page?.AbsoluteUri, out var p) ? p : TestBuildFeed.PageOf(build.Tag);
            try { Process.Start(page.AbsoluteUri); }
            catch (Exception e) when (e is System.ComponentModel.Win32Exception || e is InvalidOperationException) { log("update: could not open " + page + ": " + e.Message); }
        }
    }
}
