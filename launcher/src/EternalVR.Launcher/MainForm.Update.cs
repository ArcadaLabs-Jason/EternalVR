using System;
using System.IO;
using System.Net.Http;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;
using EternalVR.Launcher.Core;
using EternalVR.Launcher.Core.Update;

namespace EternalVR.Launcher
{
    /// <summary>
    /// Update alerts: on start (at most once an hour) the launcher asks GitHub whether a newer EternalVR release is out;
    /// if so, "Update to x.y.z..." appears next to the other buttons and offers to download and install it
    /// (<see cref="UpdateDialog"/>). The check can be turned off, and run by hand, on the Checks and log tab. A launcher
    /// that is not an unpacked release (a build or test folder: no BUILD-INFO.txt) is not checked at start; Check now
    /// shows what is out and why it cannot install it. With "Get test builds" on, the same check also looks for a newer test
    /// build (MainForm.TestBuilds.cs).
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
            WireTestBuilds();
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
            if (!asked && !InstallBlock.IsRelease(ctx.ProgramDir))
            {
                ctx.Log.Info("update: not checked at start: this build is not a release (no " + InstallBlock.BuildInfoFile + ")");
                return;
            }
            var state = LoadUpdateState();
            if (!asked && !state.Due(DateTime.UtcNow))
            {
                // Asked less than an hour ago: what that check found is still offered.
                if (state.Found != null && state.Found > Current && state.Found != state.Skipped) ShowUpdateButton(state.Found);
                ShowFoundTestBuild(state);
                return;
            }
            checkingUpdate = true;
            checkUpdateNow.Enabled = false;
            // The test list's answer, while it is still to come: handled after the release's, which never waits for it.
            Task<TestFeedOutcome> pendingTest = null;
            AvailableRelease found = null;
            var http = UpdateClient.NewHttp(Current);
            try
            {
                bool tests = ctx.Settings.TestBuilds;
                var run = UpdateCheck.Start(http, Current, asked ? null : state.Skipped, tests ? RunningBuild.Read(ctx.ProgramDir, Current) : null, tests,
                    CancellationToken.None);
                pendingTest = run.Test;
                available = found = await run.Release;
                state = LoadUpdateState();
                state.LastCheck = DateTime.UtcNow;
                state.Found = available?.Version;
                SaveUpdateState(state);
                if (available == null)
                {
                    updateButton.Visible = false;
                    ctx.Log.Info("update: EternalVR " + Current + " is the newest release");
                    if (!asked) return;
                    // Nothing to offer: Check now waits for the test list (at most UpdateCheck.TestTimeout) to say it all in one.
                    var test = pendingTest == null ? null : await pendingTest;
                    pendingTest = null;
                    if (test != null) TakeTestBuild(test, null);
                    if (test?.Build != null) OfferTestBuild();
                    else
                        MessageBox.Show(this, UpdateCheck.NewestMessage(Current, test != null, test?.Error), "EternalVR update", MessageBoxButtons.OK,
                            test?.Error != null ? MessageBoxIcon.Warning : MessageBoxIcon.Information);
                    return;
                }
                ctx.Log.Info($"update: EternalVR {available.Version} is out ({available.Page})");
                ShowUpdateButton(available.Version);
                checkingUpdate = false;
                checkUpdateNow.Enabled = true;
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
                if (pendingTest != null) FinishTestCheck(pendingTest, found, http);
                else http.Dispose();
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
            using (var dlg = new UpdateDialog(available, Current.ToString(), ctx.ProgramDir, ctx.Paths.Updates, WhyNoInstall, ctx.Log.Info))
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
        private InstallBlock WhyNoInstall()
        {
            bool release = InstallBlock.IsRelease(ctx.ProgramDir);
            string writeError = null;
            if (release && !FileUtil.IsWritableDirectory(ctx.ProgramDir, out var error)) writeError = error;
            return InstallBlock.For(release, writeError, session != null || saveRestore != null || ctx.RunningGameProcesses().Count > 0);
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
