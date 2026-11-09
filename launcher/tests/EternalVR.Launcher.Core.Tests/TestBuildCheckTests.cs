using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using EternalVR.Launcher.Core.Update;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>
    /// One update check (<see cref="UpdateCheck"/>) against a stand-in for GitHub: with "Get test builds" off it sends only the
    /// release list's request, as before; on, it also reads the test repository, and that list's failures (offline, rate
    /// limited, not a list) leave the release found and say why.
    /// </summary>
    public class TestBuildCheckTests
    {
        /// <summary>GitHub as the check sees it: an answer per URL (after <see cref="MainWait"/> or <see cref="TestWait"/>), every request noted.</summary>
        private sealed class Feeds : HttpMessageHandler
        {
            private readonly List<Uri> asked = new List<Uri>();
            public Func<HttpResponseMessage> Main = () => Json(TestBuildSamples.MainFeed);
            public Func<HttpResponseMessage> Test = () => Json(TestBuildSamples.TestFeed);
            /// <summary>Awaited before the release list answers; the request's token ends it.</summary>
            public Func<CancellationToken, Task> MainWait = _ => Task.CompletedTask;
            /// <summary>Awaited before the test list answers; the request's token ends it.</summary>
            public Func<CancellationToken, Task> TestWait = _ => Task.CompletedTask;
            /// <summary>Set once the test list has been asked.</summary>
            public readonly TaskCompletionSource<bool> TestAsked = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);

            /// <summary>The URLs asked, the release list's first (the two requests run at the same time).</summary>
            public Uri[] Asked
            {
                get { lock (asked) return asked.OrderBy(u => u == ReleaseFeed.ApiUrl ? 0 : 1).ToArray(); }
            }

            protected override async Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancel)
            {
                lock (asked) asked.Add(request.RequestUri);
                if (request.RequestUri == ReleaseFeed.ApiUrl)
                {
                    await MainWait(cancel);
                    return Main();
                }
                if (request.RequestUri == TestBuildFeed.ApiUrl)
                {
                    TestAsked.TrySetResult(true);
                    await TestWait(cancel);
                    return Test();
                }
                return new HttpResponseMessage(HttpStatusCode.NotFound);
            }
        }

        private static HttpResponseMessage Json(string text, HttpStatusCode status = HttpStatusCode.OK) =>
            new HttpResponseMessage(status) { Content = new StringContent(text, Encoding.UTF8, "application/json") };

        private static readonly Version V083 = new Version(0, 1, 83, 0);
        private static readonly RunningBuild On083 = RunningBuild.FromBuildInfo(V083,
            "EternalVR alpha 0.1.83\ncommit: 32e86ba1eb02d660b764a25fd0decc216f03b2e3\ncommit time: 2026-10-25 01:07:06 -06:00\n");
        private static readonly RunningBuild On084 = RunningBuild.FromBuildInfo(TestBuildSamples.V084, TestBuildSamples.Release084);

        private static Task<UpdateCheckResult> Check(Feeds feeds, Version current, RunningBuild running, bool testBuilds)
        {
            var http = new HttpClient(feeds) { Timeout = Timeout.InfiniteTimeSpan };
            return UpdateCheck.RunAsync(http, current, null, running, testBuilds, CancellationToken.None);
        }

        [Fact]
        public async Task OffTheCheckSendsOnlyTheReleaseRequest()
        {
            var feeds = new Feeds();
            var result = await Check(feeds, V083, On083, testBuilds: false);
            Assert.Equal(new[] { ReleaseFeed.ApiUrl }, feeds.Asked);
            Assert.Equal(new Version(0, 1, 84), result.Release.Version);
            Assert.False(result.TestChecked);
            Assert.Null(result.TestBuild);
            Assert.Null(result.TestError);
            // Not even asked when the test repository would fail: nothing about it reaches the window.
            feeds = new Feeds { Test = () => throw new HttpRequestException("No such host is known.") };
            result = await Check(feeds, TestBuildSamples.V084, On084, testBuilds: false);
            Assert.Equal(new[] { ReleaseFeed.ApiUrl }, feeds.Asked);
            Assert.Null(result.Release);
            Assert.Null(result.TestError);
        }

        [Fact]
        public async Task OnTheCheckAlsoReadsTheTestRepository()
        {
            var feeds = new Feeds();
            var result = await Check(feeds, TestBuildSamples.V084, On084, testBuilds: true);
            Assert.Equal(new[] { ReleaseFeed.ApiUrl, TestBuildFeed.ApiUrl }, feeds.Asked);
            Assert.Equal("https://api.github.com/repos/ArcadaLabs-Jason/EternalVR-test/releases?per_page=20", TestBuildFeed.ApiUrl.AbsoluteUri);
            Assert.Null(result.Release);
            Assert.True(result.TestChecked);
            Assert.Equal("b189-ce6a983", result.TestBuild.Tag);
            // A release and a test build are both found, apart.
            result = await Check(new Feeds(), V083, On083, testBuilds: true);
            Assert.Equal(new Version(0, 1, 84), result.Release.Version);
            Assert.Equal("b189-ce6a983", result.TestBuild.Tag);
            // The test build itself is offered nothing.
            result = await Check(new Feeds(), TestBuildSamples.V084, RunningBuild.FromBuildInfo(TestBuildSamples.V084, TestBuildSamples.TestB189), testBuilds: true);
            Assert.Null(result.TestBuild);
            Assert.Null(result.TestError);
        }

        [Fact]
        public async Task AnEmptyTestRepositoryHasNoTestBuild()
        {
            var result = await Check(new Feeds { Test = () => Json("[]") }, V083, On083, testBuilds: true);
            Assert.True(result.TestChecked);
            Assert.Null(result.TestBuild);
            Assert.Null(result.TestError);
            Assert.Equal(new Version(0, 1, 84), result.Release.Version);
        }

        [Fact]
        public async Task ATestRepositoryThatFailsLeavesTheReleaseAndSaysWhy()
        {
            var failures = new Func<HttpResponseMessage>[]
            {
                // GitHub's rate limit for anonymous requests: 403 with its message.
                () => Json("{\"message\":\"API rate limit exceeded for 203.0.113.7.\",\"documentation_url\":\"https://docs.github.com/rest\"}", HttpStatusCode.Forbidden),
                () => Json("{\"message\":\"rate limited\"}", (HttpStatusCode)429),
                () => Json("<html>Service unavailable</html>", HttpStatusCode.ServiceUnavailable),
                // Offline, and an answer that is not a list.
                () => throw new HttpRequestException("No such host is known. (api.github.com:443)"),
                () => Json("{\"message\":\"Not Found\"}"),
                () => Json("[{\"tag_name\":"),
                () => throw new TaskCanceledException("timed out"),
            };
            foreach (var fail in failures)
            {
                var feeds = new Feeds { Test = fail };
                var result = await Check(feeds, V083, On083, testBuilds: true);
                Assert.Equal(new[] { ReleaseFeed.ApiUrl, TestBuildFeed.ApiUrl }, feeds.Asked);
                Assert.Equal(new Version(0, 1, 84), result.Release.Version);
                Assert.True(result.TestChecked);
                Assert.Null(result.TestBuild);
                Assert.False(string.IsNullOrEmpty(result.TestError));
            }
        }

        [Fact]
        public async Task AReleaseListThatFailsFailsTheCheckAsBeforeAndTheTestListStillAnswersAlone()
        {
            var feeds = new Feeds { Main = () => Json("{\"message\":\"API rate limit exceeded\"}", HttpStatusCode.Forbidden) };
            await Assert.ThrowsAsync<HttpRequestException>(() => Check(feeds, V083, On083, testBuilds: true));
            feeds = new Feeds { Main = () => Json("{}") };
            await Assert.ThrowsAsync<FormatException>(() => Check(feeds, V083, On083, testBuilds: true));
            // Asked at the same time, the test list's answer is its own task, which never fails.
            var run = UpdateCheck.Start(new HttpClient(new Feeds { Main = () => throw new HttpRequestException("offline") }) { Timeout = Timeout.InfiniteTimeSpan },
                V083, null, On083, true, CancellationToken.None);
            await Assert.ThrowsAsync<HttpRequestException>(() => run.Release);
            Assert.Equal("b189-ce6a983", (await run.Test).Build.Tag);
        }

        [Fact]
        public async Task BothListsAreAskedAtOnceAndTheReleaseNeverWaitsForTheTestList()
        {
            var testHeld = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
            bool testAskedFirst = false;
            var feeds = new Feeds { TestWait = c => testHeld.Task };
            // The release list answers only once the test list has been asked (or after 8 s): asked one after the other, it
            // waits and testAskedFirst stays false. That proves both are asked at once without timing the answer, so a
            // busy PC can't fail it.
            feeds.MainWait = async c =>
            {
                var asked = feeds.TestAsked.Task;
                testAskedFirst = await Task.WhenAny(asked, Task.Delay(8000, c)) == asked;
            };
            var run = UpdateCheck.Start(new HttpClient(feeds) { Timeout = Timeout.InfiniteTimeSpan }, V083, null, On083, true, CancellationToken.None);
            Assert.Same(run.Release, await Task.WhenAny(run.Release, Task.Delay(20000)));
            Assert.True(testAskedFirst, "the release list answered before the test list was asked");
            Assert.Equal(new Version(0, 1, 84), (await run.Release).Version);
            // The test list has not answered: the release is there all the same.
            Assert.False(run.Test.IsCompleted);
            testHeld.SetResult(true);
            Assert.Equal("b189-ce6a983", (await run.Test).Build.Tag);
        }

        [Fact]
        public async Task ASlowTestListIsGivenItsOwnShorterTime()
        {
            Assert.True(UpdateCheck.TestTimeout <= TimeSpan.FromSeconds(8));
            Assert.True(UpdateCheck.TestTimeout < UpdateClient.FeedTimeout);
            var feeds = new Feeds { TestWait = c => Task.Delay(Timeout.Infinite, c) };
            var watch = Stopwatch.StartNew();
            var result = await UpdateCheck.RunAsync(new HttpClient(feeds) { Timeout = Timeout.InfiniteTimeSpan }, V083, null, On083, true, CancellationToken.None,
                TimeSpan.FromMilliseconds(300));
            Assert.True(watch.Elapsed < TimeSpan.FromSeconds(5), "took " + watch.Elapsed);
            Assert.Equal(new Version(0, 1, 84), result.Release.Version);
            Assert.Null(result.TestBuild);
            Assert.Contains("no answer within", result.TestError);
        }

        [Fact]
        public async Task AnyFailureOfTheTestListIsReportedAndTheReleaseKept()
        {
            foreach (var fail in new Func<HttpResponseMessage>[]
            {
                () => throw new InvalidOperationException("the handler broke"),
                () => throw new ArgumentException("a strange answer"),
                () => throw new UriFormatException("a strange address"),
            })
            {
                var result = await Check(new Feeds { Test = fail }, V083, On083, testBuilds: true);
                Assert.Equal(new Version(0, 1, 84), result.Release.Version);
                Assert.Null(result.TestBuild);
                Assert.False(string.IsNullOrEmpty(result.TestError));
            }
        }

        private static AvailableRelease ReleaseOf(Version v, DateTimeOffset? published) =>
            new AvailableRelease(v, "EternalVR v" + v, new Uri("https://github.com/ArcadaLabs-Jason/EternalVR/releases/tag/v" + v), "EternalVR-alpha-" + v + "-1234567.zip",
                new Uri("https://github.com/ArcadaLabs-Jason/EternalVR/releases/download/v" + v + "/z.zip"), 10, new string('a', 64), null, string.Empty, published);

        [Fact]
        public void ATestBuildTheOfferedReleaseHasIsNotShownBesideIt()
        {
            var b189 = TestBuildFeed.Parse(TestBuildSamples.TestFeed)[0];
            var at = b189.Published;
            Assert.Same(b189, UpdateCheck.Offered(b189, null));
            Assert.Null(UpdateCheck.Offered(null, ReleaseOf(new Version(0, 1, 84), at)));
            // A release of a higher version has it; a lower one does not.
            Assert.Null(UpdateCheck.Offered(b189, ReleaseOf(new Version(0, 1, 87), at.AddDays(-30))));
            Assert.Same(b189, UpdateCheck.Offered(b189, ReleaseOf(new Version(0, 1, 83), at.AddDays(30))));
            // The same version: only a test build published after the release is more than it.
            Assert.Null(UpdateCheck.Offered(b189, ReleaseOf(new Version(0, 1, 84), at.AddMinutes(1))));
            Assert.Null(UpdateCheck.Offered(b189, ReleaseOf(new Version(0, 1, 84), at)));
            Assert.Same(b189, UpdateCheck.Offered(b189, ReleaseOf(new Version(0, 1, 84), at.AddMinutes(-1))));
            Assert.Null(UpdateCheck.Offered(b189, ReleaseOf(new Version(0, 1, 84), null)));
            // The real v0.1.84 release (published 2026-10-26) and b189 (2026-10-28): both shown.
            Assert.Equal(new DateTimeOffset(2026, 10, 26, 0, 47, 38, TimeSpan.Zero), ReleaseFeed.Parse(TestBuildSamples.MainFeed)[0].Published);
            Assert.Same(b189, UpdateCheck.Offered(b189, ReleaseFeed.Parse(TestBuildSamples.MainFeed)[0]));
        }

        [Fact]
        public async Task TheReleaseTheCheckOffersHidesTheTestBuildsItHas()
        {
            var feeds = new Feeds
            {
                Main = () => Json(TestBuildSamples.MainFeed.Replace("0.1.84", "0.1.87").Replace("2026-10-26T00:47:38Z", "2026-10-30T18:00:00Z")),
            };
            var result = await Check(feeds, TestBuildSamples.V084, RunningBuild.FromBuildInfo(TestBuildSamples.V084, TestBuildSamples.TestB188), testBuilds: true);
            Assert.Equal(new Version(0, 1, 87), result.Release.Version);
            Assert.Null(result.TestBuild);
            Assert.Null(result.TestError);
        }

        [Fact]
        public void CheckNowSaysItAllInOneMessage()
        {
            var v = new Version(0, 1, 84, 0);
            Assert.Equal("You have the newest EternalVR (0.1.84).", UpdateCheck.NewestMessage(v, false, null));
            Assert.Equal("You have the newest EternalVR (0.1.84). No newer test build is out.", UpdateCheck.NewestMessage(v, true, null));
            Assert.Equal("You have the newest EternalVR (0.1.84). The test build check failed: Response status code does not indicate success: 403 (rate limit exceeded).",
                UpdateCheck.NewestMessage(v, true, "Response status code does not indicate success: 403 (rate limit exceeded)."));
        }
    }
}
