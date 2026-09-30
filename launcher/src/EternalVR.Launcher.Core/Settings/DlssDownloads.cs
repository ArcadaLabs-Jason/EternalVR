using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Threading;
using System.Threading.Tasks;
using EternalVR.Launcher.Core.Data;
using EternalVR.Launcher.Core.Game;
using EternalVR.Launcher.Core.Net;

namespace EternalVR.Launcher.Core.Settings
{
    /// <summary>One DLSS DLL release NVIDIA publishes, pinned by its size and SHA-256 (<c>data\dlss-downloads.txt</c>).</summary>
    public sealed class DlssRelease
    {
        public DlssRelease(Version version, Uri url, long size, string sha256, Uri license)
        {
            Version = version;
            Url = url;
            Size = size;
            Sha256 = sha256;
            License = license;
        }

        public Version Version { get; }
        public Uri Url { get; }
        public long Size { get; }
        public string Sha256 { get; }
        /// <summary>NVIDIA's license text for this release, which the player accepts before the download.</summary>
        public Uri License { get; }
        public string SizeText => (Size / (1024.0 * 1024.0)).ToString("0", CultureInfo.InvariantCulture) + " MB";
    }

    /// <summary>
    /// The DLSS DLL the launcher downloads on the player's request, straight from NVIDIA's repository, for "DLSS version:
    /// From a file" (docs/rig-findings/dlss-dll.md). EternalVR does not ship or host NVIDIA's file: it is fetched on the
    /// player's click after the player accepts NVIDIA's license, kept in the data folder, and used only when its size and
    /// SHA-256 are the pinned ones.
    /// </summary>
    public sealed class DlssDownloads
    {
        private DlssDownloads(IReadOnlyList<DlssRelease> releases) => Releases = releases;

        public IReadOnlyList<DlssRelease> Releases { get; }

        /// <summary>The release offered: the newest; null when the file lists none.</summary>
        public DlssRelease Newest => Releases.OrderByDescending(r => r.Version).FirstOrDefault();

        public static DlssDownloads Parse(string text)
        {
            var list = new List<DlssRelease>();
            foreach (var r in DataFile.ParseRecords(text))
            {
                if (!string.Equals(DataFile.Field(r, 0), "dll", StringComparison.OrdinalIgnoreCase))
                    throw new FormatException("dlss-downloads: unknown line " + string.Join(" | ", r));
                if (!Version.TryParse(DataFile.Field(r, 1), out var version)) throw new FormatException("dlss-downloads: bad version " + DataFile.Field(r, 1));
                var url = HttpsUrl(DataFile.Field(r, 2));
                if (!long.TryParse(DataFile.Field(r, 3), NumberStyles.None, CultureInfo.InvariantCulture, out var size) || size <= 0)
                    throw new FormatException("dlss-downloads: bad size " + DataFile.Field(r, 3));
                var sha = DataFile.Field(r, 4).ToLowerInvariant();
                if (sha.Length != 64 || sha.Any(c => !Uri.IsHexDigit(c))) throw new FormatException("dlss-downloads: bad sha256 " + sha);
                list.Add(new DlssRelease(version, url, size, sha, HttpsUrl(DataFile.Field(r, 5))));
            }
            return new DlssDownloads(list);
        }

        private static Uri HttpsUrl(string text)
        {
            if (!Uri.TryCreate(text, UriKind.Absolute, out var url) || url.Scheme != Uri.UriSchemeHttps)
                throw new FormatException("dlss-downloads: not an https url: " + text);
            return url;
        }

        /// <summary>Where a release's file is kept: <c>&lt;dlssRoot&gt;\&lt;version&gt;\nvngx_dlss.dll</c>.</summary>
        public static string PathFor(string dlssRoot, DlssRelease release) =>
            Path.Combine(dlssRoot, release.Version.ToString(), DlssDll.FileName);

        /// <summary>True when a file of the release's size is at <paramref name="path"/> (the quick check, for the window).</summary>
        public static bool HasSize(string path, DlssRelease release) => File.Exists(path) && new FileInfo(path).Length == release.Size;

        /// <summary>True when the file at <paramref name="path"/> is the release's (its size and SHA-256).</summary>
        public static bool Matches(string path, DlssRelease release) =>
            File.Exists(path) && new FileInfo(path).Length == release.Size
            && string.Equals(KnownBuilds.Sha256OfFile(path), release.Sha256, StringComparison.OrdinalIgnoreCase);

        /// <summary>
        /// The release's file in <paramref name="dlssRoot"/>: the one already there when it matches, else downloaded and
        /// checked against the pinned size and SHA-256 (<see cref="VerifiedDownload"/>: nothing is kept otherwise).
        /// </summary>
        public static async Task<string> FetchAsync(DlssRelease release, string dlssRoot, HttpClient http, IProgress<double> progress,
            CancellationToken cancel)
        {
            var target = PathFor(dlssRoot, release);
            if (Matches(target, release)) return target;
            await VerifiedDownload.ToFileAsync(release.Url, target, release.Size, release.Sha256, http, progress, cancel).ConfigureAwait(false);
            return target;
        }
    }
}
