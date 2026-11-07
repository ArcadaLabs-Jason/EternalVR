using System;
using System.IO;
using System.Net.Http;
using System.Threading;
using System.Threading.Tasks;
using EternalVR.Launcher.Core.Net;

namespace EternalVR.Launcher.Core.Update
{
    /// <summary>
    /// Asks GitHub for a newer EternalVR release and downloads its zip, checked against GitHub's SHA-256 of it (the asset
    /// digest, else the <c>.sha256</c> file published next to it). The only requests are to github.com and its download
    /// host; nothing about the player is sent beyond what any web request carries.
    /// </summary>
    public static class UpdateClient
    {
        /// <summary>An HTTP client for GitHub: its API wants a User-Agent; a whole download may take minutes.</summary>
        public static HttpClient NewHttp(Version launcher)
        {
            var http = new HttpClient { Timeout = Timeout.InfiniteTimeSpan };
            http.DefaultRequestHeaders.UserAgent.ParseAdd("EternalVR-Launcher/" + ReleaseFeed.Plain(launcher));
            return http;
        }

        /// <summary>The newest release after <paramref name="current"/> (and not <paramref name="skipped"/>); null when up to date.</summary>
        public static async Task<AvailableRelease> NewestAsync(HttpClient http, Version current, Version skipped, CancellationToken cancel)
        {
            var json = await ReleasesJsonAsync(http, ReleaseFeed.ApiUrl, cancel).ConfigureAwait(false);
            return ReleaseFeed.Newest(ReleaseFeed.Parse(json), current, skipped);
        }

        /// <summary>How long the release list is given.</summary>
        public static readonly TimeSpan FeedTimeout = TimeSpan.FromSeconds(20);

        /// <summary>
        /// The answer of GitHub's release list at <paramref name="api"/>, given <paramref name="timeout"/> (<see cref="FeedTimeout"/>
        /// by default); an answer that is not a success (GitHub's rate limit is a 403 or a 429) throws <see cref="HttpRequestException"/>.
        /// </summary>
        public static async Task<string> ReleasesJsonAsync(HttpClient http, Uri api, CancellationToken cancel, TimeSpan? timeout = null)
        {
            using (var request = new HttpRequestMessage(HttpMethod.Get, api))
            {
                request.Headers.Accept.ParseAdd("application/vnd.github+json");
                using (var cts = CancellationTokenSource.CreateLinkedTokenSource(cancel))
                {
                    cts.CancelAfter(timeout ?? FeedTimeout);
                    using (var response = await http.SendAsync(request, cts.Token).ConfigureAwait(false))
                    {
                        response.EnsureSuccessStatusCode();
                        return await response.Content.ReadAsStringAsync().ConfigureAwait(false);
                    }
                }
            }
        }

        /// <summary>
        /// Downloads the release's zip into <c>&lt;updatesDir&gt;\&lt;version&gt;\</c>, checked against GitHub's SHA-256;
        /// returns its path. Throws <see cref="IOException"/> when the zip is not the published one.
        /// </summary>
        public static async Task<string> DownloadAsync(AvailableRelease release, string updatesDir, HttpClient http, IProgress<double> progress,
            CancellationToken cancel)
        {
            var sha = release.ZipSha256;
            if (sha == null)
            {
                var text = await http.GetStringAsync(release.Sha256Url).ConfigureAwait(false);
                sha = ReleaseFeed.Sha256FromSumsFile(text) ?? throw new IOException("the release's .sha256 file holds no SHA-256");
            }
            var target = Path.Combine(updatesDir, release.Version.ToString(), release.ZipName);
            await VerifiedDownload.ToFileAsync(release.ZipUrl, target, release.ZipSize, sha, http, progress, cancel).ConfigureAwait(false);
            return target;
        }
    }
}
