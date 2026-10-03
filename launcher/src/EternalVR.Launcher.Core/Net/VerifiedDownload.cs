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
    /// SHA-256 checked, then moved to the target. On any failure nothing is left behind. A connection that sends nothing
    /// for <see cref="DefaultIdleTimeout"/> (the answer's headers, or the next bytes) ends it with an error.
    /// </summary>
    public static class VerifiedDownload
    {
        /// <summary>How long the download waits for the server's answer or its next bytes before it gives up.</summary>
        public static readonly TimeSpan DefaultIdleTimeout = TimeSpan.FromSeconds(30);

        /// <summary>
        /// Downloads <paramref name="url"/> to <paramref name="target"/>. Throws <see cref="IOException"/> when the file is
        /// not the expected one (size or SHA-256) or the connection stalls, and passes on network and cancellation errors.
        /// <paramref name="size"/> may be 0 when unknown (the hash still decides); <paramref name="progress"/> gets the
        /// fraction done, 0 to 1. <paramref name="idleTimeout"/> defaults to <see cref="DefaultIdleTimeout"/>.
        /// </summary>
        public static async Task ToFileAsync(Uri url, string target, long size, string sha256, HttpClient http, IProgress<double> progress,
            CancellationToken cancel, TimeSpan? idleTimeout = null)
        {
            var idle = idleTimeout ?? DefaultIdleTimeout;
            Directory.CreateDirectory(Path.GetDirectoryName(target));
            var part = target + ".part";
            try
            {
                using (var response = await GetHeadersAsync(http, url, idle, cancel).ConfigureAwait(false))
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
                        while ((n = await ReadAsync(body, buffer, idle, cancel).ConfigureAwait(false)) > 0)
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

        private static IOException Stalled(TimeSpan idle) =>
            new IOException($"the server sent nothing for {idle.TotalSeconds:0} s");

        /// <summary>The answer's headers, or <see cref="Stalled"/> after <paramref name="idle"/> without them.</summary>
        private static async Task<HttpResponseMessage> GetHeadersAsync(HttpClient http, Uri url, TimeSpan idle, CancellationToken cancel)
        {
            using (var wait = CancellationTokenSource.CreateLinkedTokenSource(cancel))
            {
                wait.CancelAfter(idle);
                try
                {
                    return await http.GetAsync(url, HttpCompletionOption.ResponseHeadersRead, wait.Token).ConfigureAwait(false);
                }
                catch (OperationCanceledException) when (!cancel.IsCancellationRequested)
                {
                    throw Stalled(idle);
                }
            }
        }

        /// <summary>
        /// One read that gives up after <paramref name="idle"/> without bytes. The read is not trusted to watch its token
        /// (.NET Framework's response stream does not): it is raced against a timer, and the stream closed if the timer wins.
        /// </summary>
        private static async Task<int> ReadAsync(Stream body, byte[] buffer, TimeSpan idle, CancellationToken cancel)
        {
            var read = body.ReadAsync(buffer, 0, buffer.Length, cancel);
            using (var timer = CancellationTokenSource.CreateLinkedTokenSource(cancel))
            {
                var first = await Task.WhenAny(read, Task.Delay(idle, timer.Token)).ConfigureAwait(false);
                timer.Cancel();
                if (first == read) return await read.ConfigureAwait(false);
            }
            // Stalled or cancelled while the read waits: closing the stream ends the read, whose error is seen here.
            body.Dispose();
            _ = read.ContinueWith(t => t.Exception, TaskContinuationOptions.OnlyOnFaulted | TaskContinuationOptions.ExecuteSynchronously);
            cancel.ThrowIfCancellationRequested();
            throw Stalled(idle);
        }
    }
}
