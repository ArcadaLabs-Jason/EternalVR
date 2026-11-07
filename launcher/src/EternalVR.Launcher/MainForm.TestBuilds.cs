using System.Drawing;
using System.Net.Http;
using System.Threading.Tasks;
using System.Windows.Forms;
using EternalVR.Launcher.Core.Settings;
using EternalVR.Launcher.Core.Update;

namespace EternalVR.Launcher
{
    /// <summary>
    /// "Get test builds" (Advanced tab, Updates; this PC's, DECISIONS T-198): with it on, the update check also looks for a test
    /// build newer than this one (<see cref="UpdateCheck"/>, <see cref="TestBuildFeed"/>) and shows it on a button of its own,
    /// "Test build b189...", apart from "Update to x.y.z...". The button opens <see cref="TestBuildDialog"/>, which links its
    /// page: nothing is downloaded or installed. Off, the check is the release check alone and the button never shows.
    /// </summary>
    public sealed partial class MainForm
    {
        private readonly CheckBox testBuilds = new CheckBox { AutoSize = true };
        /// <summary>As wide as its text needs (a batch number can be long), at least as wide as the update button.</summary>
        private readonly Button testBuildButton = new Button
        {
            AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, MinimumSize = new Size(130, BarButtonHeight), Height = BarButtonHeight, Visible = false,
        };
        /// <summary>The test build the button offers; null while it is hidden.</summary>
        private TestBuild testBuild;

        /// <summary>The Advanced tab's Updates group: Get test builds.</summary>
        private GroupBox UpdatesGroup() =>
            Group("Updates", Row(Setting.TestBuilds, testBuilds, s => testBuilds.Checked = s.TestBuilds, s => s.TestBuilds = testBuilds.Checked));

        private void WireTestBuilds()
        {
            testBuildButton.Click += (s, e) => OfferTestBuild();
            tips.SetToolTip(testBuildButton, "A newer EternalVR test build is out: what it is, and a link to its page. Nothing is downloaded by itself.");
            testBuildButton.VisibleChanged += (s, e) => { if (testBuildButton.Parent is FlowLayoutPanel bar) QueueFitReportLinks(bar); };
            // Turned off, its button goes at once; turned on, the next check (at start, or Check now) looks.
            testBuilds.CheckedChanged += (s, e) => { if (!testBuilds.Checked) ShowTestBuild(null); };
        }

        /// <summary>The test build the last check found, while it is still newer than this build (the check was less than an hour ago).</summary>
        private void ShowFoundTestBuild(UpdateState state)
        {
            if (ctx.Settings.TestBuilds && state.TestFound != null && TestBuildFeed.IsNewer(state.TestFound, RunningBuild.Read(ctx.ProgramDir, Current)))
                ShowTestBuild(state.TestFound);
        }

        /// <summary>The test list's answer, once it has come (after the release's was handled); then the check's client goes.</summary>
        private async void FinishTestCheck(Task<TestFeedOutcome> pending, AvailableRelease release, HttpClient http)
        {
            try
            {
                // Never fails: a failure is the outcome's Error.
                var outcome = await pending;
                if (!IsDisposed) TakeTestBuild(outcome, release);
            }
            finally
            {
                http.Dispose();
            }
        }

        /// <summary>
        /// What a check found on the test repository: kept in the update state and shown, unless the release this check offers
        /// has it already (<see cref="UpdateCheck.Offered"/>). A failure is logged and leaves the button as the last check left it.
        /// </summary>
        private void TakeTestBuild(TestFeedOutcome outcome, AvailableRelease release)
        {
            // Turned off while the answer was on its way.
            if (!ctx.Settings.TestBuilds) return;
            if (outcome.Error != null)
            {
                ctx.Log.Info("update: the test build check failed: " + outcome.Error);
                return;
            }
            var found = UpdateCheck.Offered(outcome.Build, release);
            if (outcome.Build != null && found == null) ctx.Log.Info($"update: test build {outcome.Build.Name} is in the release offered");
            ctx.Log.Info(found == null ? "update: no newer test build"
                : $"update: test build {found.Name} ({found.Sha}, {found.Version}) is out ({found.Page})");
            var state = LoadUpdateState();
            state.TestFound = found;
            SaveUpdateState(state);
            ShowTestBuild(found);
        }

        /// <summary>Shows the button for <paramref name="build"/>; hides it for null.</summary>
        private void ShowTestBuild(TestBuild build)
        {
            testBuild = build;
            if (build != null) testBuildButton.Text = "Test build " + build.Name + "...";
            testBuildButton.Visible = build != null;
        }

        private void OfferTestBuild()
        {
            if (testBuild == null) return;
            using (var dlg = new TestBuildDialog(testBuild, Current.ToString(), ctx.Log.Info)) dlg.ShowDialog(this);
        }
    }
}
