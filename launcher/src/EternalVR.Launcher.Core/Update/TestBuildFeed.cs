using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text.RegularExpressions;
using EternalVR.Launcher.Core.Text;

namespace EternalVR.Launcher.Core.Update
{
    /// <summary>A test build on the test repository: what the update check found with "Get test builds" on.</summary>
    public sealed class TestBuild
    {
        public TestBuild(string tag, int batch, string sha, Version version, string title, Uri page, DateTimeOffset published, string notes)
        {
            Tag = tag;
            Batch = batch;
            Sha = sha;
            Version = version;
            Title = title;
            Page = page;
            Published = published;
            Notes = notes ?? string.Empty;
        }

        /// <summary>The release tag, <c>b&lt;batch&gt;-&lt;sha7&gt;</c>.</summary>
        public string Tag { get; }
        public int Batch { get; }
        /// <summary>The commit it was built from, lower-case hex (7 digits as tagged).</summary>
        public string Sha { get; }
        /// <summary>The version in its zip's name: the version the code it was built from carries.</summary>
        public Version Version { get; }
        public string Title { get; }
        /// <summary>Its release page on GitHub.</summary>
        public Uri Page { get; }
        /// <summary>When it was published.</summary>
        public DateTimeOffset Published { get; }
        /// <summary>The release notes (Markdown): the tester sheet.</summary>
        public string Notes { get; }

        /// <summary>The short name the window shows: <c>b189</c>.</summary>
        public string Name => "b" + Batch.ToString(CultureInfo.InvariantCulture);
    }

    /// <summary>
    /// The test repository's releases (<c>/repos/ArcadaLabs-Jason/EternalVR-test/releases</c>), read by the update check only
    /// while "Get test builds" is on. A test build is a release tagged <c>b&lt;batch&gt;-&lt;sha7&gt;</c> (pre-release or not;
    /// drafts never count) with an <c>EternalVR-&lt;channel&gt;-&lt;x.y.z&gt;-&lt;sha7&gt;.zip</c> asset of the same commit.
    /// Nothing is downloaded: the window links its page (DECISIONS T-198).
    /// </summary>
    public static class TestBuildFeed
    {
        public const string Repository = "ArcadaLabs-Jason/EternalVR-test";
        public static readonly Uri ApiUrl = new Uri("https://api.github.com/repos/" + Repository + "/releases?per_page=20");
        public static readonly Uri ReleasesPage = new Uri("https://github.com/" + Repository + "/releases");

        private static readonly Regex Tag = new Regex(@"^b(\d{1,9})-([0-9a-f]{7,40})$", RegexOptions.CultureInvariant | RegexOptions.IgnoreCase);
        private static readonly Regex Zip = new Regex(@"^EternalVR-[A-Za-z]+-(\d{1,9})\.(\d{1,9})\.(\d{1,9})-([0-9a-f]{7,40})\.zip$",
            RegexOptions.CultureInvariant | RegexOptions.IgnoreCase);

        /// <summary>Every test build in the API's answer; an answer that is not a release list throws <see cref="FormatException"/>.</summary>
        public static IReadOnlyList<TestBuild> Parse(string json)
        {
            if (!(MiniJson.Parse(json) is List<object> releases)) throw new FormatException("the test build list is not a JSON array");
            var found = new List<TestBuild>();
            foreach (var r in releases)
            {
                if (MiniJson.Get(r, "draft") is bool draft && draft) continue;
                var tag = MiniJson.Get(r, "tag_name") as string ?? string.Empty;
                var t = Tag.Match(tag);
                if (!t.Success) continue;
                var sha = t.Groups[2].Value.ToLowerInvariant();
                var zip = (MiniJson.Get(r, "assets") as List<object> ?? new List<object>())
                    .Select(a => Zip.Match(MiniJson.Get(a, "name") as string ?? string.Empty))
                    .FirstOrDefault(m => m.Success && SameCommit(m.Groups[4].Value, sha));
                if (zip == null) continue;
                if (!(MiniJson.Get(r, "published_at") is string at) || !TryTime(at, out var published)) continue;
                var page = IsTestPage(MiniJson.Get(r, "html_url") as string, out var p) ? p : PageOf(tag);
                found.Add(new TestBuild(tag, int.Parse(t.Groups[1].Value, CultureInfo.InvariantCulture), sha, VersionOf(zip), Title(r, tag), page, published,
                    MiniJson.Get(r, "body") as string));
            }
            return found;
        }

        /// <summary>
        /// True when <paramref name="build"/> is newer than <paramref name="running"/> (DECISIONS T-198): a higher version is
        /// newer and a lower one older; at the same version (test builds carry the last release's version until the next one),
        /// it is newer when it is not the running build's own commit and was published after the running build was made: its
        /// commit time, or <paramref name="since"/> when the running build is itself a test build of the feed. A running build
        /// with no <c>BUILD-INFO.txt</c> (no commit time) counts only higher versions.
        /// </summary>
        public static bool IsNewer(TestBuild build, RunningBuild running, DateTimeOffset? since = null)
        {
            var v = ReleaseFeed.Plain(build.Version);
            if (v != running.Version) return v > running.Version;
            if (running.IsCommit(build.Sha)) return false;
            var after = since ?? running.CommitTime;
            return after != null && build.Published > after.Value;
        }

        /// <summary>The newest test build newer than <paramref name="running"/>; null when there is none.</summary>
        public static TestBuild Newest(IEnumerable<TestBuild> builds, RunningBuild running)
        {
            var list = builds.ToList();
            // A running test build counts as made when it was published: one published between its commit and then is older.
            var own = list.Where(b => running.IsCommit(b.Sha)).OrderByDescending(b => b.Published).FirstOrDefault();
            DateTimeOffset? since = own != null && (running.CommitTime == null || own.Published > running.CommitTime.Value) ? own.Published : running.CommitTime;
            return list.Where(b => IsNewer(b, running, since))
                .OrderByDescending(b => ReleaseFeed.Plain(b.Version)).ThenByDescending(b => b.Published).ThenByDescending(b => b.Batch)
                .FirstOrDefault();
        }

        /// <summary>
        /// True when <paramref name="text"/> is a page of the test repository on GitHub (https, github.com, no user or port,
        /// under <c>/ArcadaLabs-Jason/EternalVR-test/</c>): the only release page the window opens. Any other is replaced by
        /// <see cref="PageOf"/>.
        /// </summary>
        public static bool IsTestPage(string text, out Uri page)
        {
            page = null;
            if (!Uri.TryCreate(text, UriKind.Absolute, out var u) || u.Scheme != Uri.UriSchemeHttps || !u.IsDefaultPort || u.UserInfo.Length > 0
                || !string.Equals(u.Host, "github.com", StringComparison.OrdinalIgnoreCase)
                || !u.AbsolutePath.StartsWith("/" + Repository + "/", StringComparison.OrdinalIgnoreCase))
                return false;
            page = u;
            return true;
        }

        /// <summary>The release page of the test build tagged <paramref name="tag"/>.</summary>
        public static Uri PageOf(string tag) => new Uri(ReleasesPage.AbsoluteUri + "/tag/" + Uri.EscapeDataString(tag));

        /// <summary>
        /// A test build as the update state keeps it (<see cref="UpdateState.TestFound"/>: its tag, version and publication
        /// time); null when any of them is malformed.
        /// </summary>
        public static TestBuild FromState(string tag, string version, string published)
        {
            var t = Tag.Match(tag ?? string.Empty);
            if (!t.Success || !Version.TryParse(version, out var v) || !TryTime(published, out var at)) return null;
            return new TestBuild(tag, int.Parse(t.Groups[1].Value, CultureInfo.InvariantCulture), t.Groups[2].Value.ToLowerInvariant(), ReleaseFeed.Plain(v),
                "EternalVR test build " + tag, PageOf(tag), at, null);
        }

        /// <summary>The publication time as the update state writes it (UTC).</summary>
        public static string TimeText(DateTimeOffset t) => t.UtcDateTime.ToString("yyyy-MM-ddTHH:mm:ssZ", CultureInfo.InvariantCulture);

        private static bool TryTime(string text, out DateTimeOffset t) =>
            DateTimeOffset.TryParse(text, CultureInfo.InvariantCulture, DateTimeStyles.AssumeUniversal | DateTimeStyles.AdjustToUniversal, out t);

        private static bool SameCommit(string a, string b) =>
            a.StartsWith(b, StringComparison.OrdinalIgnoreCase) || b.StartsWith(a, StringComparison.OrdinalIgnoreCase);

        private static Version VersionOf(Match zip) => new Version(int.Parse(zip.Groups[1].Value, CultureInfo.InvariantCulture),
            int.Parse(zip.Groups[2].Value, CultureInfo.InvariantCulture), int.Parse(zip.Groups[3].Value, CultureInfo.InvariantCulture));

        private static string Title(object release, string tag)
        {
            var name = (MiniJson.Get(release, "name") as string ?? string.Empty).Trim();
            return name.Length > 0 ? name : "EternalVR test build " + tag;
        }
    }
}
