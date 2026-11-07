using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using EternalVR.Launcher.Core.Update;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>
    /// The test repository's feed (<see cref="TestBuildFeed"/>): which releases count as test builds, which one is newer than
    /// the running build (DECISIONS T-198), the running build read from its BUILD-INFO.txt, and the update state's copy.
    /// </summary>
    public class TestBuildFeedTests
    {
        private static readonly DateTimeOffset B189Published = new DateTimeOffset(2026, 10, 28, 22, 51, 0, TimeSpan.Zero);

        private static RunningBuild Running(string buildInfo, Version version = null) =>
            RunningBuild.FromBuildInfo(version ?? TestBuildSamples.V084, buildInfo);

        private static string Release(string tag, string zip, string published = "\"2026-10-29T10:00:00Z\"", bool draft = false, bool prerelease = true) =>
            $"{{\"tag_name\":\"{tag}\",\"name\":\"Test build ({tag})\",\"draft\":{(draft ? "true" : "false")},\"prerelease\":{(prerelease ? "true" : "false")},"
            + $"\"html_url\":\"https://github.com/ArcadaLabs-Jason/EternalVR-test/releases/tag/{tag}\""
            + (published == null ? string.Empty : ",\"published_at\":" + published)
            + ",\"body\":\"Test build.\",\"assets\":["
            + (zip == null ? string.Empty : $"{{\"name\":\"{zip}\",\"size\":10,\"browser_download_url\":\"https://github.com/ArcadaLabs-Jason/EternalVR-test/releases/download/{tag}/{zip}\"}}")
            + "]}";

        private static TestBuild Build(string tag, Version version, DateTimeOffset published) =>
            TestBuildFeed.FromState(tag, version.ToString(), TestBuildFeed.TimeText(published));

        [Fact]
        public void ThePublishedFeedGivesBothTestBuilds()
        {
            var list = TestBuildFeed.Parse(TestBuildSamples.TestFeed);
            Assert.Equal(new[] { "b189-ce6a983", "b188-ce70bbd" }, list.Select(b => b.Tag));
            var b189 = list[0];
            Assert.Equal(189, b189.Batch);
            Assert.Equal("b189", b189.Name);
            Assert.Equal("ce6a983", b189.Sha);
            // The zip's version is the code's: test builds carry the last release's until the next one.
            Assert.Equal(new Version(0, 1, 84), b189.Version);
            Assert.Equal(B189Published, b189.Published);
            Assert.Equal("https://github.com/ArcadaLabs-Jason/EternalVR-test/releases/tag/b189-ce6a983", b189.Page.AbsoluteUri);
            Assert.Equal("Test build: 0.1.85 + 0.1.86 + next batch (b189, ce6a983)", b189.Title);
            Assert.StartsWith("Test build for 0.1.85 + 0.1.86 + the next batch", ReleaseFeed.Summary(b189.Notes));
            // The public repository's releases are no test builds (their tags are versions).
            Assert.Empty(TestBuildFeed.Parse(TestBuildSamples.MainFeed));
        }

        [Theory]
        [InlineData("v0.1.84", "EternalVR-alpha-0.1.84-ce6a983.zip")]            // a release tag
        [InlineData("b189", "EternalVR-alpha-0.1.84-ce6a983.zip")]               // no commit
        [InlineData("b-ce6a983", "EternalVR-alpha-0.1.84-ce6a983.zip")]          // no batch
        [InlineData("b189-ce6a98", "EternalVR-alpha-0.1.84-ce6a98.zip")]         // six hex digits
        [InlineData("b189-ce6a98z", "EternalVR-alpha-0.1.84-ce6a98z.zip")]       // not hex
        [InlineData("b189-ce6a983-fix", "EternalVR-alpha-0.1.84-ce6a983.zip")]   // more after the commit
        [InlineData("b189-ce6a983", "EternalVR-alpha-0.1.84-ce70bbd.zip")]       // the zip of another commit
        [InlineData("b189-ce6a983", "EternalVR-alpha-0.1.84-ce6a983-symbols.zip")] // symbols only
        [InlineData("b189-ce6a983", "EternalVR-alpha-0.1-ce6a983.zip")]          // no x.y.z
        [InlineData("b189-ce6a983", "EternalVR-alpha-0.1.9999999999-ce6a983.zip")] // a part too big for a version
        [InlineData("b189-ce6a983", "EternalVR-0.1.84-ce6a983.zip")]             // no channel
        [InlineData("b189-ce6a983", null)]                                        // no zip at all
        [InlineData("b9999999999-ce6a983", "EternalVR-alpha-0.1.84-ce6a983.zip")] // a batch too big
        public void AMalformedTagOrZipIsLeftOut(string tag, string zip)
        {
            var json = "[" + Release(tag, zip) + "," + Release("b190-abcdef1", "EternalVR-alpha-0.1.84-abcdef1.zip") + "]";
            Assert.Equal(new[] { "b190-abcdef1" }, TestBuildFeed.Parse(json).Select(b => b.Tag));
        }

        [Fact]
        public void DraftsAndReleasesWithoutAPublicationTimeAreLeftOutAndPreReleasesOrNotCount()
        {
            var json = "[" + string.Join(",",
                Release("b191-1111111", "EternalVR-alpha-0.1.84-1111111.zip", draft: true),
                Release("b192-2222222", "EternalVR-alpha-0.1.84-2222222.zip", published: null),
                Release("b193-3333333", "EternalVR-alpha-0.1.84-3333333.zip", published: "\"not a time\""),
                Release("b194-4444444", "EternalVR-alpha-0.1.84-4444444.zip", published: "null"),
                Release("b195-5555555", "EternalVR-alpha-0.1.84-5555555.zip", prerelease: false),
                Release("B196-ABCDEF0", "EternalVR-Alpha-0.1.84-abcdef0123.zip")) + "]";
            var list = TestBuildFeed.Parse(json);
            Assert.Equal(new[] { "b195-5555555", "B196-ABCDEF0" }, list.Select(b => b.Tag));
            Assert.Equal("abcdef0", list[1].Sha);
        }

        [Fact]
        public void AnAnswerThatIsNotAReleaseListThrowsAndAnEmptyOneHasNone()
        {
            Assert.Throws<FormatException>(() => TestBuildFeed.Parse("{\"message\":\"API rate limit exceeded for 203.0.113.7.\"}"));
            Assert.Throws<FormatException>(() => TestBuildFeed.Parse("<html>Service unavailable</html>"));
            Assert.Throws<FormatException>(() => TestBuildFeed.Parse(string.Empty));
            Assert.Empty(TestBuildFeed.Parse("[]"));
            Assert.Null(TestBuildFeed.Newest(TestBuildFeed.Parse("[]"), Running(TestBuildSamples.Release084)));
        }

        [Fact]
        public void TheRunningBuildIsReadFromItsBuildInfo()
        {
            var r = Running(TestBuildSamples.TestB189);
            Assert.Equal(new Version(0, 1, 84), r.Version);
            Assert.Equal("ce6a98336d6eadde4144d977ecd6f6e03fe38b24", r.Commit);
            Assert.Equal(new DateTimeOffset(2026, 10, 28, 14, 44, 18, TimeSpan.FromHours(-6)), r.CommitTime);
            Assert.True(r.IsCommit("ce6a983"));
            Assert.False(r.IsCommit("ce70bbd"));
            Assert.False(r.IsCommit("ce6a"));
            // A build of a changed tree says so after its commit.
            var dirty = Running("EternalVR alpha 0.1.84\ncommit: ce6a98336d6eadde4144d977ecd6f6e03fe38b24 (with uncommitted changes: not for sharing)\n");
            Assert.Equal("ce6a98336d6eadde4144d977ecd6f6e03fe38b24", dirty.Commit);
            Assert.Null(dirty.CommitTime);
            // None, or lines that are not what make-release writes: nothing known beyond the version.
            foreach (var text in new[] { null, string.Empty, "commit: not-a-sha\ncommit time: yesterday\n" })
            {
                var none = Running(text);
                Assert.Null(none.Commit);
                Assert.Null(none.CommitTime);
                Assert.False(none.IsCommit("ce6a983"));
            }
            using (var t = new TempDir())
            {
                Assert.Null(RunningBuild.Read(t.Path, TestBuildSamples.V084).Commit);
                File.WriteAllText(Path.Combine(t.Path, "BUILD-INFO.txt"), TestBuildSamples.Release084);
                Assert.Equal("8258af39bd4ad97821e11c4e0414c2051fe279e7", RunningBuild.Read(t.Path, TestBuildSamples.V084).Commit);
            }
        }

        [Fact]
        public void APlayerOnTheReleaseIsOfferedTheNewestTestBuild()
        {
            var feed = TestBuildFeed.Parse(TestBuildSamples.TestFeed);
            // v0.1.84 (committed 2026-10-25): both test builds came after it at the same version; b189 is the newest.
            Assert.Equal("b189-ce6a983", TestBuildFeed.Newest(feed, Running(TestBuildSamples.Release084))?.Tag);
            // On b188: b189 is newer.
            Assert.Equal("b189-ce6a983", TestBuildFeed.Newest(feed, Running(TestBuildSamples.TestB188))?.Tag);
            // On b189 itself: nothing newer (it is its own commit, and b188 was published before it was made).
            Assert.Null(TestBuildFeed.Newest(feed, Running(TestBuildSamples.TestB189)));
        }

        [Fact]
        public void AReleaseOfAHigherVersionHasEveryEarlierTestBuildInIt()
        {
            var feed = TestBuildFeed.Parse(TestBuildSamples.TestFeed);
            var release087 = "EternalVR alpha 0.1.87\ncommit: 0123456789abcdef0123456789abcdef01234567\ncommit time: 2026-10-30 10:00:00 -06:00\n";
            Assert.Null(TestBuildFeed.Newest(feed, Running(release087, new Version(0, 1, 87, 0))));
            // A lower version is older whatever its date; a higher one is newer whatever its date.
            var on087 = Running(release087, new Version(0, 1, 87, 0));
            var late084 = Build("b200-aaaaaaa", new Version(0, 1, 84), new DateTimeOffset(2026, 12, 1, 0, 0, 0, TimeSpan.Zero));
            var early088 = Build("b201-bbbbbbb", new Version(0, 1, 88), new DateTimeOffset(2026, 1, 1, 0, 0, 0, TimeSpan.Zero));
            Assert.False(TestBuildFeed.IsNewer(late084, on087));
            Assert.True(TestBuildFeed.IsNewer(early088, on087));
            // A test build made after the version moved on (0.1.87 on main, not yet released) is newer for a 0.1.84 player.
            Assert.True(TestBuildFeed.IsNewer(Build("b202-ccccccc", new Version(0, 1, 87), B189Published), Running(TestBuildSamples.Release084)));
        }

        [Fact]
        public void AtTheSameVersionTheDateDecidesAndAnUnknownDateOffersNothing()
        {
            var on084 = Running(TestBuildSamples.Release084);
            var made = on084.CommitTime.Value;
            Assert.True(TestBuildFeed.IsNewer(Build("b203-ddddddd", new Version(0, 1, 84), made.AddMinutes(1)), on084));
            Assert.False(TestBuildFeed.IsNewer(Build("b203-ddddddd", new Version(0, 1, 84), made), on084));
            Assert.False(TestBuildFeed.IsNewer(Build("b203-ddddddd", new Version(0, 1, 84), made.AddDays(-1)), on084));
            // Its own commit is never newer, even published after it was made.
            Assert.False(TestBuildFeed.IsNewer(Build("b204-8258af3", new Version(0, 1, 84), made.AddDays(1)), on084));
            // A build folder (no BUILD-INFO.txt): only a higher version counts.
            var folder = Running(null);
            Assert.False(TestBuildFeed.IsNewer(Build("b205-eeeeeee", new Version(0, 1, 84), made.AddDays(30)), folder));
            Assert.True(TestBuildFeed.IsNewer(Build("b206-fffffff", new Version(0, 1, 85), made.AddDays(-30)), folder));
        }

        [Fact]
        public void ARunningTestBuildCountsAsMadeWhenItWasPublished()
        {
            // Two builds a few hours apart: b211 was committed first, b210 published while b211 was being packaged.
            var running = Running("EternalVR alpha 0.1.84\ncommit: 2111111aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\ncommit time: 2026-11-01 10:00:00 +00:00\n");
            var b210 = Build("b210-1000000", new Version(0, 1, 84), new DateTimeOffset(2026, 11, 1, 11, 0, 0, TimeSpan.Zero));
            var b211 = Build("b211-2111111", new Version(0, 1, 84), new DateTimeOffset(2026, 11, 1, 12, 0, 0, TimeSpan.Zero));
            var b212 = Build("b212-3000000", new Version(0, 1, 84), new DateTimeOffset(2026, 11, 1, 13, 0, 0, TimeSpan.Zero));
            Assert.Null(TestBuildFeed.Newest(new[] { b210, b211 }, running));
            Assert.Same(b212, TestBuildFeed.Newest(new[] { b210, b211, b212 }, running));
            // Without its own release in the feed, its commit time is all there is.
            Assert.Same(b210, TestBuildFeed.Newest(new[] { b210 }, running));
        }

        [Fact]
        public void TheNewestIsTheHighestVersionThenTheLatest()
        {
            var on084 = Running(TestBuildSamples.Release084);
            var a = Build("b220-aaaaaaa", new Version(0, 1, 85), new DateTimeOffset(2026, 11, 2, 0, 0, 0, TimeSpan.Zero));
            var b = Build("b221-bbbbbbb", new Version(0, 1, 86), new DateTimeOffset(2026, 11, 1, 0, 0, 0, TimeSpan.Zero));
            var c = Build("b222-ccccccc", new Version(0, 1, 86), new DateTimeOffset(2026, 11, 3, 0, 0, 0, TimeSpan.Zero));
            var old = Build("b223-ddddddd", new Version(0, 1, 83), new DateTimeOffset(2026, 12, 1, 0, 0, 0, TimeSpan.Zero));
            Assert.Same(c, TestBuildFeed.Newest(new[] { a, c, b, old }, on084));
            Assert.Same(c, TestBuildFeed.Newest(new[] { old, b, a, c }, on084));
            Assert.Same(a, TestBuildFeed.Newest(new[] { a, old }, on084));
            Assert.Null(TestBuildFeed.Newest(new[] { old }, on084));
        }

        [Fact]
        public void TheStateKeepsTheTestBuildTheLastCheckFound()
        {
            using (var t = new TempDir())
            {
                var path = t.Combine("updates", "state.txt");
                var found = TestBuildFeed.Parse(TestBuildSamples.TestFeed)[0];
                var now = new DateTime(2026, 10, 30, 15, 0, 0, DateTimeKind.Utc);
                new UpdateState { LastCheck = now, TestFound = found }.Save(path);
                Assert.Contains("test_found = b189-ce6a983 | 0.1.84 | 2026-10-28T22:51:00Z\n", File.ReadAllText(path));
                var back = UpdateState.Load(path).TestFound;
                Assert.Equal("b189-ce6a983", back.Tag);
                Assert.Equal("ce6a983", back.Sha);
                Assert.Equal(new Version(0, 1, 84), back.Version);
                Assert.Equal(B189Published, back.Published);
                Assert.Equal(found.Page, back.Page);
                // Judged again as at the check: newer than the release, not than itself.
                Assert.True(TestBuildFeed.IsNewer(back, Running(TestBuildSamples.Release084)));
                Assert.False(TestBuildFeed.IsNewer(back, Running(TestBuildSamples.TestB189)));

                // Without a test build the file is what it was before the setting existed.
                new UpdateState { LastCheck = now, Found = new Version(0, 1, 87) }.Save(path);
                Assert.Equal("# EternalVR launcher: the update check (Checks and log tab)\ncheck = on\nfound = 0.1.87\nlast_check = 2026-10-30T15:00:00Z\n",
                    File.ReadAllText(path));
                Assert.Null(UpdateState.Load(path).TestFound);
                foreach (var bad in new[] { "test_found = b189-ce6a983", "test_found = v0.1.84 | 0.1.84 | 2026-10-28T22:51:00Z",
                             "test_found = b189-ce6a983 | x | 2026-10-28T22:51:00Z", "test_found = b189-ce6a983 | 0.1.84 | soon" })
                {
                    File.WriteAllText(path, "check = on\n" + bad + "\n");
                    Assert.Null(UpdateState.Load(path).TestFound);
                }
            }
        }

        [Theory]
        [InlineData("https://evil.example/ArcadaLabs-Jason/EternalVR-test/releases/tag/b189-ce6a983")]
        [InlineData("http://github.com/ArcadaLabs-Jason/EternalVR-test/releases/tag/b189-ce6a983")]
        [InlineData("https://github.com.evil.example/ArcadaLabs-Jason/EternalVR-test/releases/tag/b189-ce6a983")]
        [InlineData("https://evil.example@github.com/ArcadaLabs-Jason/EternalVR-test/releases/tag/b189-ce6a983")]
        [InlineData("https://github.com:8443/ArcadaLabs-Jason/EternalVR-test/releases/tag/b189-ce6a983")]
        [InlineData("https://github.com/someone-else/EternalVR-test/releases/tag/b189-ce6a983")]
        [InlineData("https://github.com/ArcadaLabs-Jason/EternalVR-testing/releases/tag/b189-ce6a983")]
        [InlineData("https://github.com/ArcadaLabs-Jason/EternalVR/releases/tag/v0.1.84")]
        [InlineData("https://github.com/ArcadaLabs-Jason/EternalVR-test/../../evil/download")]
        [InlineData("file:///C:/Windows/System32/calc.exe")]
        [InlineData("not a url")]
        public void APageOutsideTheTestRepositoryIsNeverOpened(string htmlUrl)
        {
            // b189's html_url is the first occurrence (b188's notes link it too).
            const string real = "https://github.com/ArcadaLabs-Jason/EternalVR-test/releases/tag/b189-ce6a983";
            int at = TestBuildSamples.TestFeed.IndexOf(real, StringComparison.Ordinal);
            var json = TestBuildSamples.TestFeed.Substring(0, at) + htmlUrl + TestBuildSamples.TestFeed.Substring(at + real.Length);
            var b189 = TestBuildFeed.Parse(json)[0];
            Assert.Equal("https://github.com/ArcadaLabs-Jason/EternalVR-test/releases/tag/b189-ce6a983", b189.Page.AbsoluteUri);
            Assert.False(TestBuildFeed.IsTestPage(htmlUrl, out _));
            Assert.True(TestBuildFeed.IsTestPage("https://GitHub.com/ArcadaLabs-Jason/EternalVR-test/releases/tag/b189-ce6a983", out var page));
            Assert.Equal("github.com", page.Host);
        }

        [Fact]
        public void TheCommitTimeOffsetAndTheUtcPublicationAreOneClock()
        {
            // Committed 14:44:18 at UTC-6 is 20:44:18 UTC: a build published a second before is older, a second after newer.
            var west = Running(TestBuildSamples.Release084.Replace("2026-10-25 18:29:07 -06:00", "2026-10-28 14:44:18 -06:00"));
            Assert.False(TestBuildFeed.IsNewer(Build("b230-aaaaaaa", new Version(0, 1, 84), new DateTimeOffset(2026, 10, 28, 20, 44, 17, TimeSpan.Zero)), west));
            Assert.True(TestBuildFeed.IsNewer(Build("b230-aaaaaaa", new Version(0, 1, 84), new DateTimeOffset(2026, 10, 28, 20, 44, 19, TimeSpan.Zero)), west));
            // The same instant written east of UTC (+05:30): 02:14:18 the next day.
            var east = Running(TestBuildSamples.Release084.Replace("2026-10-25 18:29:07 -06:00", "2026-10-29 02:14:18 +05:30"));
            Assert.Equal(west.CommitTime.Value.UtcDateTime, east.CommitTime.Value.UtcDateTime);
            Assert.False(TestBuildFeed.IsNewer(Build("b231-bbbbbbb", new Version(0, 1, 84), new DateTimeOffset(2026, 10, 28, 20, 44, 17, TimeSpan.Zero)), east));
            Assert.True(TestBuildFeed.IsNewer(Build("b231-bbbbbbb", new Version(0, 1, 84), new DateTimeOffset(2026, 10, 28, 20, 44, 19, TimeSpan.Zero)), east));
            // The feed's published_at, read from its Z time: b189 at 22:51 UTC is after b189's own commit at 14:44 -06:00 (20:44 UTC).
            var b189 = TestBuildFeed.Parse(TestBuildSamples.TestFeed)[0];
            Assert.Equal(TimeSpan.Zero, b189.Published.Offset);
            Assert.True(b189.Published > Running(TestBuildSamples.TestB189).CommitTime.Value);
        }
    }
}
