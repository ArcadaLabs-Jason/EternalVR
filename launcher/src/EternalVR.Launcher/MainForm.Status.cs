using System;
using System.Drawing;
using System.Windows.Forms;
using EternalVR.Launcher.Core.Launch;

namespace EternalVR.Launcher
{
    /// <summary>The window's status line: the session's outcomes (refusals, the layer's state, the end), with a dialog for problems.</summary>
    public sealed partial class MainForm
    {
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
                : s.Kind == StatusKind.Warning ? Color.DarkOrange
                : s.Kind == StatusKind.Good ? Color.ForestGreen
                : SystemColors.ControlText;
            if (LayerWatch.NeedsDialog(s) && !closing.IsCancellationRequested)
                BeginInvoke(new Action(() => MessageBox.Show(this, s.Text, "EternalVR", MessageBoxButtons.OK, MessageBoxIcon.Warning)));
        }

        /// <summary>Shows a status that did not come from a session (restore problems, the finish helper).</summary>
        private void ShowStatus(StatusKind kind, string text) => ShowStatus(new SessionStatus(kind, text));
    }
}
