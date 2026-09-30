using System;
using System.IO;
using System.Net.Http;
using System.Security.Cryptography;
using System.Threading;
using System.Threading.Tasks;

namespace EternalVR.Launcher.Core.Net
{
    /// <summary>
    /// A download that is kept only when it is the expected file: streamed into <c>&lt;target&gt;.part</c>, its size and
    /// SHA-256 checked, then moved to the target. On any failure nothing is left behind.
    /// </summary>
    public static class VerifiedDownload
    {
        /// <summary>
        /// Downloads <paramref name="url"/> to <paramref name="target"/>. Throws <see cref="IOException"/> when the file is
        /// not the expected one (size or SHA-256) and passes on network and cancellation errors. <paramref name="size"/>
        /// may be 0 when unknown (the hash still decides); <paramref name="progress"/> gets the fraction done, 0 to 1.
        /// </summary>
        public static async Task ToFileAsync(Uri url, string target, long size, string sha256, HttpClient http, IProgress<double> progress,
            CancellationToken cancel)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(target));
            var part = target + ".part";
            try
            {
                using (var response = await http.GetAsync(url, HttpCompletionOption.ResponseHeadersRead, cancel).ConfigureAwait(false))
                {
                    response.EnsureSuccessStatusCode();
                    long total = size > 0 ? size : response.Content.Headers.ContentLength ?? 0;
                    using (var body = await response.Content.ReadAsStreamAsync().ConfigureAwait(false))
                    using (var file = new FileStream(part, FileMode.Create, FileAccess.Write, FileShare.None))
                    using (var sha = SHA256.Create())
                    {
                        var buffer = new byte[1 << 16];
                        long done = 0;
                        int n;
                        while ((n = await body.ReadAsync(buffer, 0, buffer.Length, cancel).ConfigureAwait(false)) > 0)
                        {
                            done += n;
                            if (size > 0 && done > size) throw new IOException($"the download is larger than expected ({size} bytes)");
                            sha.TransformBlock(buffer, 0, n, null, 0);
                            await file.WriteAsync(buffer, 0, n, cancel).ConfigureAwait(false);
                            if (total > 0) progress?.Report(Math.Min(1.0, (double)done / total));
                        }
                        sha.TransformFinalBlock(buffer, 0, 0);
                        if (size > 0 && done != size) throw new IOException($"the download ended after {done} of {size} bytes");
                        var hash = BitConverter.ToString(sha.Hash).Replace("-", string.Empty);
                        if (!string.Equals(hash, sha256, StringComparison.OrdinalIgnoreCase))
                            throw new IOException("the download is not the expected file (its SHA-256 differs)");
                        file.Flush(true);
                    }
                }
                if (File.Exists(target)) File.Delete(target);
                File.Move(part, target);
            }
            finally
            {
                if (File.Exists(part)) File.Delete(part);
            }
        }
    }
}
