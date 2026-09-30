using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text.RegularExpressions;
using EternalVR.Launcher.Core.Text;

namespace EternalVR.Launcher.Core.Update
{
    /// <summary>A published EternalVR release with its zip: what the update check found.</summary>
    public sealed class AvailableRelease
    {
        public AvailableRelease(Version version, string title, Uri page, string zipName, Uri zipUrl, long zipSize, string zipSha256, Uri sha256Url,
            string notes)
        {
            Version = version;
            Title = title;
            Page = page;
            ZipName = zipName;
            ZipUrl = zipUrl;
            ZipSize = zipSize;
            ZipSha256 = zipSha256;
            Sha256Url = sha256Url;
            Notes = notes;
        }

        public Version Version { get; }
        public string Title { get; }
        /// <summary>The release's page on GitHub.</summary>
        public Uri Page { get; }
        public string ZipName { get; }
        public Uri ZipUrl { get; }
        public long ZipSize { get; }
        /// <summary>GitHub's SHA-256 of the zip (its asset digest); null when GitHub gave none (then <see cref="Sha256Url"/>).</summary>
        public string ZipSha256 { get; }
        /// <summary>The <c>&lt;zip&gt;.sha256</c> asset published next to the zip; null when there is none.</summary>
        public Uri Sha256Url { get; }
        /// <summary>The release notes (Markdown), as published.</summary>
        public string Notes { get; }
    }

    /// <summary>
    /// The public repository's releases, from GitHub's API (<c>/repos/&lt;owner&gt;/&lt;repo&gt;/releases</c>). Alpha
    /// builds are published as pre-releases and count; drafts do not. A release counts when its tag is a version
    /// (<c>v0.1.5</c>) and it has an <c>EternalVR-*.zip</c> asset (not a symbols zip).
    /// </summary>
    public static class ReleaseFeed
    {
        public const string Repository = "ArcadaLabs-Jason/EternalVR";
        public static readonly Uri ApiUrl = new Uri("https://api.github.com/repos/" + Repository + "/releases?per_page=20");
        public static readonly Uri ReleasesPage = new Uri("https://github.com/" + Repository + "/releases");

        private static readonly Regex Tag = new Regex(@"^v?(\d+)\.(\d+)\.(\d+)$", RegexOptions.CultureInvariant);
        private static readonly Regex Zip = new Regex(@"^EternalVR-[A-Za-z0-9._-]+\.zip$", RegexOptions.CultureInvariant);

        /// <summary>Every usable release in the API's answer; a malformed answer throws <see cref="FormatException"/>.</summary>
        public static IReadOnlyList<AvailableRelease> Parse(string json)
        {
            if (!(MiniJson.Parse(json) is List<object> releases)) throw new FormatException("the release list is not a JSON array");
            var found = new List<AvailableRelease>();
            foreach (var r in releases)
            {
                if (MiniJson.Get(r, "draft") is bool draft && draft) continue;
                var m = Tag.Match(MiniJson.Get(r, "tag_name") as string ?? string.Empty);
                if (!m.Success) continue;
                var version = new Version(int.Parse(m.Groups[1].Value, CultureInfo.InvariantCulture), int.Parse(m.Groups[2].Value, CultureInfo.InvariantCulture),
                    int.Parse(m.Groups[3].Value, CultureInfo.InvariantCulture));
                var assets = (MiniJson.Get(r, "assets") as List<object> ?? new List<object>()).ToList();
                var zip = assets.FirstOrDefault(a => IsZip(Name(a)));
                if (zip == null || !Https(MiniJson.Get(zip, "browser_download_url") as string, out var zipUrl)) continue;
                var sums = assets.FirstOrDefault(a => string.Equals(Name(a), Name(zip) + ".sha256", StringComparison.OrdinalIgnoreCase));
                Uri sumsUrl = null;
                if (sums != null) Https(MiniJson.Get(sums, "browser_download_url") as string, out sumsUrl);
                var digest = MiniJson.Get(zip, "digest") as string;
                var sha = digest != null && digest.StartsWith("sha256:", StringComparison.OrdinalIgnoreCase) && IsSha256(digest.Substring(7))
                    ? digest.Substring(7).ToLowerInvariant() : null;
                if (sha == null && sumsUrl == null) continue;
                Https(MiniJson.Get(r, "html_url") as string, out var page);
                found.Add(new AvailableRelease(version, MiniJson.Get(r, "name") as string ?? ("EternalVR " + version), page ?? ReleasesPage,
                    Name(zip), zipUrl, Number(MiniJson.Get(zip, "size")), sha, sumsUrl, MiniJson.Get(r, "body") as string ?? string.Empty));
            }
            return found;
        }

        /// <summary>The newest release newer than <paramref name="current"/> and not the skipped one; null when there is none.</summary>
        public static AvailableRelease Newest(IEnumerable<AvailableRelease> releases, Version current, Version skipped)
        {
            var newest = releases.OrderByDescending(r => r.Version).FirstOrDefault();
            if (newest == null || newest.Version <= Plain(current)) return null;
            return skipped != null && newest.Version == Plain(skipped) ? null : newest;
        }

        /// <summary>The SHA-256 in a <c>.sha256</c> file (<c>&lt;hash&gt;  &lt;name&gt;</c>); null when it holds none.</summary>
        public static string Sha256FromSumsFile(string text)
        {
            var first = (text ?? string.Empty).Trim().Split(new[] { ' ', '\t', '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries).FirstOrDefault();
            return first != null && IsSha256(first) ? first.ToLowerInvariant() : null;
        }

        /// <summary>The first paragraph of the notes (after the title), for the update offer; plain text, at most 400 characters.</summary>
        public static string Summary(string notes)
        {
            foreach (var block in (notes ?? string.Empty).Replace("\r\n", "\n").Split(new[] { "\n\n" }, StringSplitOptions.RemoveEmptyEntries))
            {
                var t = block.Trim();
                if (t.StartsWith("#", StringComparison.Ordinal)) continue;
                t = Regex.Replace(t, @"\s+", " ").Replace("**", string.Empty).Replace("`", string.Empty);
                return t.Length > 400 ? t.Substring(0, 397) + "..." : t;
            }
            return string.Empty;
        }

        /// <summary>Major.minor.build only (the launcher's version may carry a revision of 0).</summary>
        public static Version Plain(Version v) => new Version(v.Major, v.Minor, Math.Max(0, v.Build));

        private static bool IsZip(string name) => Zip.IsMatch(name) && name.IndexOf("symbols", StringComparison.OrdinalIgnoreCase) < 0;

        private static string Name(object asset) => MiniJson.Get(asset, "name") as string ?? string.Empty;

        private static bool IsSha256(string s) => s.Length == 64 && s.All(Uri.IsHexDigit);

        private static long Number(object value) => value is double d ? (long)d : value is long l ? l : value is int i ? i : 0;

        private static bool Https(string text, out Uri url) =>
            Uri.TryCreate(text, UriKind.Absolute, out url) && url.Scheme == Uri.UriSchemeHttps;
    }
}
