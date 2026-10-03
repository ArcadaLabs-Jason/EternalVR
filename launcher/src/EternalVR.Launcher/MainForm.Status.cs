using System;
using System.Drawing;
using System.Windows.Forms;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Report;

namespace EternalVR.Launcher
{
    /// <summary>The window's status line: the session's outcomes (refusals, the layer's state, the end), with a dialog for problems.</summary>
    public sealed partial class MainForm
    {
        /// <summary>Status colours dark enough to read on the window's grey (about 6:1): amber for warnings, green for good news.</summary>
        internal static readonly Color WarningText = Color.FromArgb(150, 80, 0), GoodText = Color.FromArgb(0, 110, 40);

        /// <summary>The Export report button while the status line asks for a report (ReportHint): amber, black text.</summary>
        internal static readonly Color ExportMark = Color.FromArgb(255, 200, 70);

        private readonly Label statusLabel = new Label
        {
            AutoSize = true, MaximumSize = new Size(700, 0), Padding = new Padding(0, 4, 0, 4),
            Font = UiFont(FontStyle.Bold),
        };

        private void ShowStatus(SessionStatus s)
        {
            if (s == null || IsDisposed) return;
            if (InvokeRequired)
            {
                try { BeginInvoke(new Action<SessionStatus>(ShowStatus), s); }
                catch (InvalidOperationException) { }
                return;
            }
            statusLabel.Text = s.Text;
            statusLabel.ForeColor = s.Kind == StatusKind.Problem ? Color.Firebrick
                : s.Kind == StatusKind.Warning ? WarningText
                : s.Kind == StatusKind.Good ? GoodText
                : SystemColors.ControlText;
            // A warning or a problem that asks for a report marks the button it names; a good outcome unmarks it.
            if ((s.Kind == StatusKind.Problem || s.Kind == StatusKind.Warning) && ReportHint.In(s.Text)) MarkExport(true);
            else if (s.Kind == StatusKind.Good) MarkExport(false);
            if (LayerWatch.NeedsDialog(s) && !closing.IsCancellationRequested)
                BeginInvoke(new Action(() => MessageBox.Show(this, s.Text, "EternalVR", MessageBoxButtons.OK, MessageBoxIcon.Warning)));
        }

        private void MarkExport(bool on)
        {
            exportReport.UseVisualStyleBackColor = !on;
            exportReport.BackColor = on ? ExportMark : SystemColors.Control;
            exportReport.ForeColor = on ? Color.Black : SystemColors.ControlText;
        }

        /// <summary>Shows a status that did not come from a session (restore problems, the finish helper).</summary>
        private void ShowStatus(StatusKind kind, string text) => ShowStatus(new SessionStatus(kind, text));
    }
}
