using System;
using System.IO;
using System.Net.Http;
using System.Threading;
using System.Windows.Forms;
using EternalVR.Launcher.Core;
using EternalVR.Launcher.Core.Update;

namespace EternalVR.Launcher
{
    /// <summary>
    /// Update alerts: on start (at most once an hour) the launcher asks GitHub whether a newer EternalVR release is out;
    /// if so, "Update to x.y.z..." appears next to the other buttons and offers to download and install it
    /// (<see cref="UpdateDialog"/>). The check can be turned off, and run by hand, on the Checks and log tab.
    /// </summary>
    public sealed partial class MainForm
    {
        private readonly Button updateButton = new Button { Width = 130, Height = BarButtonHeight, Visible = false };
        private readonly CheckBox updateCheck = new CheckBox { Text = "Check for new EternalVR versions", AutoSize = true, Margin = new Padding(12, 3, 3, 3), Anchor = AnchorStyles.Left };
        private readonly Button checkUpdateNow = new Button { Text = "Check now", Width = 90, Height = ButtonHeight };
        private AvailableRelease available;
        private bool checkingUpdate;

        /// <summary>The launcher to start once this window has closed and its lock is released (after an update); null for none.</summary>
        internal static string RestartExe { get; private set; }

        private static readonly Version Current = ReleaseFeed.Plain(typeof(MainForm).Assembly.GetName().Version);

        private void WireUpdates()
        {
            updateCheck.Checked = LoadUpdateState().Check;
            updateCheck.CheckedChanged += (s, e) =>
            {
                var state = LoadUpdateState();
                state.Check = updateCheck.Checked;
                SaveUpdateState(state);
            };
            checkUpdateNow.Click += (s, e) => CheckForUpdate(asked: true);
            updateButton.Click += (s, e) => OfferUpdate();
            tips.SetToolTip(updateButton, "A newer EternalVR is out: see what is new, and download and install it.");
            tips.SetToolTip(updateCheck, "When the launcher starts, it asks GitHub (at most once an hour) whether a newer EternalVR release is out.");
            try
            {
                if (UpdatePackage.RemoveOldFiles(ctx.ProgramDir) > 0) ctx.Log.Info("update: some files an update replaced are still in use; they are removed next time");
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException)
            {
                ctx.Log.Info("update: could not clean up after an update: " + e.Message);
            }
        }

        /// <summary>The Checks and log tab's line: the data folder button, the update check and Check now.</summary>
        private Control UpdateRow()
        {
            var row = new FlowLayoutPanel { AutoSize = true, WrapContents = false, Margin = Padding.Empty };
            row.Controls.AddRange(new Control[] { openData, updateCheck, checkUpdateNow });
            return row;
        }

        private async void CheckForUpdate(bool asked)
        {
            if (checkingUpdate || ctx.TestMode) return;
            var state = LoadUpdateState();
            if (!asked && !state.Due(DateTime.UtcNow))
            {
                // Asked less than an hour ago: what that check found is still offered.
                if (state.Found != null && state.Found > Current && state.Found != state.Skipped) ShowUpdateButton(state.Found);
                return;
            }
            checkingUpdate = true;
            checkUpdateNow.Enabled = false;
            try
            {
                using (var http = UpdateClient.NewHttp(Current))
                    available = await UpdateClient.NewestAsync(http, Current, asked ? null : state.Skipped, CancellationToken.None);
                state = LoadUpdateState();
                state.LastCheck = DateTime.UtcNow;
                state.Found = available?.Version;
                SaveUpdateState(state);
                if (available == null)
                {
                    updateButton.Visible = false;
                    ctx.Log.Info("update: EternalVR " + Current + " is the newest release");
                    if (asked) MessageBox.Show(this, "You have the newest EternalVR (" + Current + ").", "EternalVR update", MessageBoxButtons.OK, MessageBoxIcon.Information);
                    return;
                }
                ctx.Log.Info($"update: EternalVR {available.Version} is out ({available.Page})");
                ShowUpdateButton(available.Version);
                if (asked) OfferUpdate();
            }
            catch (Exception e) when (e is HttpRequestException || e is OperationCanceledException || e is FormatException || e is IOException)
            {
                ctx.Log.Info("update: the check failed: " + e.Message);
                if (asked) MessageBox.Show(this, "The update check failed: " + e.Message, "EternalVR update", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            }
            finally
            {
                checkingUpdate = false;
                checkUpdateNow.Enabled = true;
            }
        }

        private void ShowUpdateButton(Version version)
        {
            updateButton.Text = "Update to " + version + "...";
            updateButton.Visible = true;
        }

        private void OfferUpdate()
        {
            if (available == null)
            {
                // Found by an earlier check: ask again for its files.
                CheckForUpdate(asked: true);
                return;
            }
            UpdateChoice choice;
            using (var dlg = new UpdateDialog(available, Current.ToString(), ctx.ProgramDir, ctx.Paths.Updates, WhyNoInstall(), ctx.Log.Info))
            {
                dlg.ShowDialog(this);
                choice = dlg.Choice;
            }
            if (choice == UpdateChoice.Skip)
            {
                var state = LoadUpdateState();
                state.Skipped = available.Version;
                SaveUpdateState(state);
                ctx.Log.Info("update: skipping EternalVR " + available.Version);
                updateButton.Visible = false;
            }
            else if (choice == UpdateChoice.Installed)
            {
                MessageBox.Show(this, "EternalVR " + available.Version + " is installed. The launcher starts again now.", "EternalVR update",
                    MessageBoxButtons.OK, MessageBoxIcon.Information);
                RestartExe = Path.Combine(ctx.ProgramDir, "EternalVR.Launcher.exe");
                Close();
            }
        }

        /// <summary>Why the release cannot be installed in place now; null when it can.</summary>
        private string WhyNoInstall()
        {
            if (session != null || saveRestore != null || ctx.RunningGameProcesses().Count > 0)
                return "Quit DOOM Eternal first: the update replaces the layer the game loads.";
            if (!File.Exists(Path.Combine(ctx.ProgramDir, "BUILD-INFO.txt")))
                return "This launcher is not an unpacked release (it has no BUILD-INFO.txt), so it is not updated in place: download the new release from its page.";
            if (!FileUtil.IsWritableDirectory(ctx.ProgramDir, out var error))
                return "The launcher's folder cannot be written (" + error + "): download the new release from its page, or move EternalVR to a folder of your own.";
            return null;
        }

        private UpdateState LoadUpdateState()
        {
            try { return UpdateState.Load(ctx.Paths.UpdateStateFile); }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException) { return new UpdateState(); }
        }

        private void SaveUpdateState(UpdateState state)
        {
            try { state.Save(ctx.Paths.UpdateStateFile); }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException) { ctx.Log.Error("saving the update state failed: " + e.Message); }
        }
    }
}
