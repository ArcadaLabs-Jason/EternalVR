using System;
using System.Drawing;
using System.Windows.Forms;
using EternalVR.Launcher.Core.Report;

namespace EternalVR.Launcher
{
    /// <summary>
    /// After Export report saved its zip: where it is, the two places to post it (<see cref="ReportLinks"/>, the same links
    /// as beside the button) and how to capture what the headset shows before the next report.
    /// </summary>
    internal sealed class ReportSavedDialog : Form
    {
        private const int TextWidth = 460;
        // Sizes at 96 DPI, scaled by the pass at ResumeLayout; the buttons grow and shrink to their text.
        private readonly Button close = new Button
        {
            Text = "Close", AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, MinimumSize = new Size(90, 30), DialogResult = DialogResult.OK,
        };

        public ReportSavedDialog(string zipPath, Action<string> logError)
        {
            Text = "Export report";
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

            var saved = new Label
            {
                AutoSize = true, MaximumSize = new Size(TextWidth, 0), Margin = new Padding(0, 0, 0, 8),
                Text = "Saved, personal info removed. Attach it to your GitHub issue or Discord message.\n\n" + zipPath,
            };
            var capture = new Label
            {
                AutoSize = true, MaximumSize = new Size(TextWidth, 0), Margin = new Padding(0, 8, 0, 8), Text = ReportLinks.CaptureHint,
            };
            var tips = new ToolTip { AutoPopDelay = 30000, InitialDelay = 400, ReshowDelay = 100 };
            Disposed += (s, e) => tips.Dispose();
            var links = new FlowLayoutPanel { AutoSize = true, Dock = DockStyle.Fill, Margin = new Padding(0, 4, 0, 0), WrapContents = false };
            links.Controls.Add(ReportLinkButtons.GitHub(30, tips, logError));
            links.Controls.Add(ReportLinkButtons.Discord(30, tips, logError));
            var buttons = new FlowLayoutPanel { AutoSize = true, FlowDirection = FlowDirection.RightToLeft, Dock = DockStyle.Fill, Margin = new Padding(0, 8, 0, 0) };
            buttons.Controls.Add(close);
            var layout = new TableLayoutPanel { AutoSize = true, ColumnCount = 1, Dock = DockStyle.Fill };
            layout.Controls.AddRange(new Control[] { saved, links, capture, buttons });
            Controls.Add(layout);
            AcceptButton = close;
            CancelButton = close;
            // Enter or Esc closes it; a link opens only when clicked or chosen.
            ActiveControl = close;
            DisplayScale.Follow(this);
            ResumeLayout(false);
            PerformLayout();
        }
    }
}
