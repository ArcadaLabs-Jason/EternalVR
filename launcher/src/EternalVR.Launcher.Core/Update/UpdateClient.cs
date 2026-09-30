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
            using (var request = new HttpRequestMessage(HttpMethod.Get, ReleaseFeed.ApiUrl))
            {
                request.Headers.Accept.ParseAdd("application/vnd.github+json");
                using (var cts = CancellationTokenSource.CreateLinkedTokenSource(cancel))
                {
                    cts.CancelAfter(TimeSpan.FromSeconds(20));
                    using (var response = await http.SendAsync(request, cts.Token).ConfigureAwait(false))
                    {
                        response.EnsureSuccessStatusCode();
                        var json = await response.Content.ReadAsStringAsync().ConfigureAwait(false);
                        return ReleaseFeed.Newest(ReleaseFeed.Parse(json), current, skipped);
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
