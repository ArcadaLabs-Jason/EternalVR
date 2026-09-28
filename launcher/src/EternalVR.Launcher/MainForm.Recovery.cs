using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Windows.Forms;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Safety;

namespace EternalVR.Launcher
{
    /// <summary>
    /// A pending settings restore that keeps failing (retried with a growing interval, with a way to give it up), and the
    /// choice of save backup for "Restore saves...".
    /// </summary>
    public sealed partial class MainForm
    {
        private const int RecoveryFirstMs = 5000;
        private const int RecoveryMaxMs = 120000;
        /// <summary>Failed restores before the window offers to discard the pending one.</summary>
        private const int FailuresBeforeDiscard = 3;
        private int recoveryFailures;
        private readonly Button discardRestore = new Button { Text = "Discard pending restore...", Width = 180, Height = 36, Visible = false };

        /// <summary>After a recovery attempt: the next retry interval, the status line and the discard button.</summary>
        private void AfterRecovery(bool clear)
        {
            if (clear || runner.LastRestoreError == null)
            {
                // Done, or only waiting for the game to exit: no failure.
                if (clear && recoveryFailures > 0) ShowStatus(StatusKind.Good, "Your game settings have been put back.");
                recoveryFailures = 0;
                recoveryTimer.Interval = RecoveryFirstMs;
                discardRestore.Visible = false;
                return;
            }
            recoveryFailures++;
            recoveryTimer.Interval = RetryInterval(recoveryFailures);
            ShowStatus(StatusKind.Warning, "Your game settings could not be put back yet: " + runner.LastRestoreError
                + $" The launcher tries again in {recoveryTimer.Interval / 1000} s.");
            discardRestore.Visible = recoveryFailures >= FailuresBeforeDiscard;
        }

        /// <summary>5 s, doubled after each failure, at most 2 minutes.</summary>
        internal static int RetryInterval(int failures) =>
            (int)Math.Min(RecoveryMaxMs, RecoveryFirstMs * Math.Pow(2, Math.Max(0, failures - 1)));

        private void DiscardPendingRestore()
        {
            var marker = SessionMarker.Read(ctx.Paths.SessionMarker);
            if (marker == null)
            {
                discardRestore.Visible = false;
                return;
            }
            var answer = MessageBox.Show(this,
                "Give up putting back the settings of the session of " + marker.SessionId + "?\n\n"
                + "The game keeps the settings the VR session used (resolution, window mode and the other VR settings); you can change them "
                + "back in the game's options. The saved copy of your settings stays in " + (marker.SnapshotDir ?? ctx.Paths.Snapshots) + ".",
                "Discard pending restore", MessageBoxButtons.YesNo, MessageBoxIcon.Warning, MessageBoxDefaultButton.Button2);
            if (answer != DialogResult.Yes) return;
            if (SessionMarker.DeleteIfOwned(ctx.Paths.SessionMarker, marker.SessionId))
                ctx.Log.Warn($"the pending restore of session {marker.SessionId} was discarded by the user; the snapshot is kept in {marker.SnapshotDir}");
            recoveryTimer.Enabled = false;
            AfterRecovery(true);
            ShowStatus(StatusKind.Warning, "The pending restore was discarded; the game keeps its VR settings.");
            RunPreflight();
        }

        /// <summary>The save backup to restore, chosen by the player (newest first); null when cancelled.</summary>
        private string ChooseBackup(IReadOnlyList<string> backups)
        {
            using (var dlg = new Form
            {
                Text = "Restore saves", Width = 420, Height = 340, FormBorderStyle = FormBorderStyle.FixedDialog,
                StartPosition = FormStartPosition.CenterParent, MinimizeBox = false, MaximizeBox = false, ShowInTaskbar = false,
                Font = UiFont(),
            })
            {
                var label = new Label { Text = "Choose the backup to restore (one is made before every VR launch):", Dock = DockStyle.Top, Height = 36, Padding = new Padding(8, 8, 8, 0) };
                var list = new ListBox { Dock = DockStyle.Fill, IntegralHeight = false };
                foreach (var b in backups) list.Items.Add(new BackupItem(b));
                list.SelectedIndex = 0;
                var ok = new Button { Text = "Restore...", DialogResult = DialogResult.OK, Width = 100 };
                var cancel = new Button { Text = "Cancel", DialogResult = DialogResult.Cancel, Width = 100 };
                var buttons = new FlowLayoutPanel { Dock = DockStyle.Bottom, FlowDirection = FlowDirection.RightToLeft, Height = 40, Padding = new Padding(4) };
                buttons.Controls.AddRange(new Control[] { cancel, ok });
                list.DoubleClick += (s, e) => { dlg.DialogResult = DialogResult.OK; };
                dlg.Controls.Add(list);
                dlg.Controls.Add(label);
                dlg.Controls.Add(buttons);
                dlg.AcceptButton = ok;
                dlg.CancelButton = cancel;
                return dlg.ShowDialog(this) == DialogResult.OK ? (list.SelectedItem as BackupItem)?.Path : null;
            }
        }

        /// <summary>"Saturday, 27 September 2026 14:15" from a backup folder named yyyyMMdd-HHmmss[-n].</summary>
        internal static string BackupLabel(string dir)
        {
            var name = Path.GetFileName(dir);
            var stamp = name.Length >= 15 ? name.Substring(0, 15) : name;
            return DateTime.TryParseExact(stamp, "yyyyMMdd-HHmmss", CultureInfo.InvariantCulture, DateTimeStyles.None, out var t)
                ? t.ToString("f") : name;
        }

        private sealed class BackupItem
        {
            public BackupItem(string path) { Path = path; }
            public string Path { get; }
            public override string ToString() => BackupLabel(Path);
        }
    }
}
