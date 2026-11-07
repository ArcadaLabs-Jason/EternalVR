using System;
using EternalVR.Launcher.Core.Update;

namespace EternalVR.Launcher.UiShots
{
    /// <summary>
    /// A stand-in for the test repository's answer (one test build, trimmed from the real release list) and a v0.1.84 release
    /// running: the test build the check would offer, for the "Get test builds" shots. Nothing is asked of GitHub.
    /// </summary>
    internal static class TestBuildStub
    {
        private const string Feed = """
            [{"html_url": "https://github.com/ArcadaLabs-Jason/EternalVR-test/releases/tag/b189-ce6a983", "tag_name": "b189-ce6a983",
              "name": "Test build: 0.1.85 + 0.1.86 + next batch (b189, ce6a983)", "draft": false, "prerelease": true,
              "published_at": "2026-10-28T22:51:00Z",
              "assets": [{"name": "EternalVR-alpha-0.1.84-ce6a983.zip", "size": 3711249,
                          "browser_download_url": "https://github.com/ArcadaLabs-Jason/EternalVR-test/releases/download/b189-ce6a983/EternalVR-alpha-0.1.84-ce6a983.zip"}],
              "body": "Test build for 0.1.85 + 0.1.86 + the next batch (about 25 minutes, do what you can). It replaces b188: if you tried that one, only the NEW items below are new.\n\nReply in the Discord thread with the item number and OK / not OK / didn't try, plus an Export report."}]
            """;

        private const string Running = "EternalVR alpha 0.1.84\ncommit: 8258af39bd4ad97821e11c4e0414c2051fe279e7\ncommit time: 2026-10-25 18:29:07 -06:00\n";

        public const string RunningVersion = "0.1.84";

        /// <summary>The test build the check offers a v0.1.84 player from <see cref="Feed"/>.</summary>
        public static TestBuild Newest() =>
            TestBuildFeed.Newest(TestBuildFeed.Parse(Feed), RunningBuild.FromBuildInfo(new Version(RunningVersion), Running))
            ?? throw new InvalidOperationException("the stand-in feed offers no test build");

        /// <summary>The same build with another title and batch: an ampersand in the title, a batch number as long as a tag allows.</summary>
        public static TestBuild Variant(string title, int batch)
        {
            var b = Newest();
            return new TestBuild("b" + batch + "-" + b.Sha, batch, b.Sha, b.Version, title, b.Page, b.Published, b.Notes);
        }
    }
}
