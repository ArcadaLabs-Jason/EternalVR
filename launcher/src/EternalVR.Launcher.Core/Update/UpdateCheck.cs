using System;
using System.Net.Http;
using System.Threading;
using System.Threading.Tasks;

namespace EternalVR.Launcher.Core.Update
{
    /// <summary>What the test repository gave one check: the newer test build, or why it could not be read.</summary>
    public sealed class TestFeedOutcome
    {
        public TestFeedOutcome(TestBuild build, string error)
        {
            Build = build;
            Error = error;
        }

        /// <summary>The newest test build newer than the running build; null for none, or when the feed failed.</summary>
        public TestBuild Build { get; }
        /// <summary>Why the test feed could not be read; null when it was.</summary>
        public string Error { get; }
    }

    /// <summary>One update check under way: the release list's answer and, with "Get test builds" on, the test list's, each its own task.</summary>
    public sealed class UpdateCheckRun
    {
        public UpdateCheckRun(Task<AvailableRelease> release, Task<TestFeedOutcome> test)
        {
            Release = release;
            Test = test;
        }

        /// <summary>The newest release after the running one (null when up to date); fails as the release check always did.</summary>
        public Task<AvailableRelease> Release { get; }
        /// <summary>The test list's outcome; null when it is not asked ("Get test builds" off). Never fails: a failure is its <see cref="TestFeedOutcome.Error"/>.</summary>
        public Task<TestFeedOutcome> Test { get; }
    }

    /// <summary>What one update check found, both lists answered (<see cref="UpdateCheck.RunAsync"/>).</summary>
    public sealed class UpdateCheckResult
    {
        public UpdateCheckResult(AvailableRelease release, bool testChecked, TestBuild testBuild, string testError)
        {
            Release = release;
            TestChecked = testChecked;
            TestBuild = testBuild;
            TestError = testError;
        }

        public AvailableRelease Release { get; }
        /// <summary>True when the test repository was asked ("Get test builds" on).</summary>
        public bool TestChecked { get; }
        /// <summary>The test build to show beside the release (<see cref="UpdateCheck.Offered"/>); null for none.</summary>
        public TestBuild TestBuild { get; }
        public string TestError { get; }
    }

    /// <summary>
    /// One update check: the public repository's releases (<see cref="UpdateClient.NewestAsync"/>), and only with "Get test
    /// builds" on, at the same time, the test repository's (<see cref="TestBuildFeed"/>), given <see cref="TestTimeout"/>. With
    /// it off the check sends exactly the one request it always did. The release list's answer never waits for the test list's,
    /// and whatever goes wrong with the test list (DECISIONS T-198) only leaves the test build out, with the reason.
    /// </summary>
    public static class UpdateCheck
    {
        /// <summary>How long the test list is given: shorter than the release list's, as it is the lesser answer.</summary>
        public static readonly TimeSpan TestTimeout = TimeSpan.FromSeconds(8);

        /// <summary>Starts both requests (the test list's only when <paramref name="testBuilds"/>).</summary>
        public static UpdateCheckRun Start(HttpClient http, Version current, Version skipped, RunningBuild running, bool testBuilds,
            CancellationToken cancel, TimeSpan? testTimeout = null)
        {
            var release = UpdateClient.NewestAsync(http, current, skipped, cancel);
            return new UpdateCheckRun(release, testBuilds ? TestAsync(http, running, cancel, testTimeout ?? TestTimeout) : null);
        }

        /// <summary>Both answers: the release list's failure fails it as before; the test list's is <see cref="UpdateCheckResult.TestError"/>.</summary>
        public static async Task<UpdateCheckResult> RunAsync(HttpClient http, Version current, Version skipped, RunningBuild running, bool testBuilds,
            CancellationToken cancel, TimeSpan? testTimeout = null)
        {
            var run = Start(http, current, skipped, running, testBuilds, cancel, testTimeout);
            var release = await run.Release.ConfigureAwait(false);
            if (run.Test == null) return new UpdateCheckResult(release, false, null, null);
            var test = await run.Test.ConfigureAwait(false);
            return new UpdateCheckResult(release, true, Offered(test.Build, release), test.Error);
        }

        /// <summary>
        /// <paramref name="build"/> when it is worth showing beside <paramref name="release"/> (null for none): not when the
        /// release has it already, a lower version, or the same version published no later than the release (a release whose
        /// time GitHub did not give is taken to have every test build of its version).
        /// </summary>
        public static TestBuild Offered(TestBuild build, AvailableRelease release)
        {
            if (build == null || release == null) return build;
            var v = ReleaseFeed.Plain(build.Version);
            var r = ReleaseFeed.Plain(release.Version);
            if (v != r) return v > r ? build : null;
            return release.Published != null && build.Published > release.Published.Value ? build : null;
        }

        /// <summary>
        /// The one message Check now shows when no newer release is out: that, and with test builds asked, that none is newer
        /// or why the test list could not be read.
        /// </summary>
        public static string NewestMessage(Version current, bool testChecked, string testError) =>
            "You have the newest EternalVR (" + ReleaseFeed.Plain(current) + ")."
            + (!testChecked ? string.Empty
                : testError == null ? " No newer test build is out."
                : " The test build check failed: " + testError);

        private static async Task<TestFeedOutcome> TestAsync(HttpClient http, RunningBuild running, CancellationToken cancel, TimeSpan timeout)
        {
            try
            {
                var json = await UpdateClient.ReleasesJsonAsync(http, TestBuildFeed.ApiUrl, cancel, timeout).ConfigureAwait(false);
                return new TestFeedOutcome(TestBuildFeed.Newest(TestBuildFeed.Parse(json), running), null);
            }
            catch (OperationCanceledException) when (!cancel.IsCancellationRequested)
            {
                return new TestFeedOutcome(null, "no answer within " + timeout.TotalSeconds.ToString(System.Globalization.CultureInfo.InvariantCulture) + " s");
            }
            catch (Exception e)
            {
                // Anything at all (the caller's cancel included): the test build is left out, the release never lost.
                return new TestFeedOutcome(null, e.Message);
            }
        }
    }
}
