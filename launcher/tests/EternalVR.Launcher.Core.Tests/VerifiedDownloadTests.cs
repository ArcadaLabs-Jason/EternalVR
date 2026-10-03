using System;
using System.Diagnostics;
using System.IO;
using System.Net;
using System.Net.Http;
using System.Threading;
using System.Threading.Tasks;
using EternalVR.Launcher.Core.Net;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    public class VerifiedDownloadTests
    {
        private static readonly Uri Url = new Uri("https://example.invalid/file.dll");
        private static readonly TimeSpan Idle = TimeSpan.FromMilliseconds(300);

        /// <summary>Sends some bytes, then nothing ever again; its reads do not watch their token (as on .NET Framework).</summary>
        private sealed class StallingStream : Stream
        {
            private int sent;

            public override Task<int> ReadAsync(byte[] buffer, int offset, int count, CancellationToken cancel)
            {
                if (sent > 0) return new TaskCompletionSource<int>().Task;
                sent = Math.Min(count, 100);
                return Task.FromResult(sent);
            }

            public override int Read(byte[] buffer, int offset, int count) => ReadAsync(buffer, offset, count, CancellationToken.None).GetAwaiter().GetResult();
            public override bool CanRead => true;
            public override bool CanSeek => false;
            public override bool CanWrite => false;
            public override long Length => throw new NotSupportedException();
            public override long Position { get => throw new NotSupportedException(); set => throw new NotSupportedException(); }
            public override void Flush() { }
            public override long Seek(long offset, SeekOrigin origin) => throw new NotSupportedException();
            public override void SetLength(long value) => throw new NotSupportedException();
            public override void Write(byte[] buffer, int offset, int count) => throw new NotSupportedException();
        }

        private sealed class Handler : HttpMessageHandler
        {
            private readonly bool answer;
            public Handler(bool answer) { this.answer = answer; }

            protected override async Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancel)
            {
                // A server that never answers: only the token ends the wait.
                if (!answer) await Task.Delay(Timeout.Infinite, cancel);
                return new HttpResponseMessage(HttpStatusCode.OK) { Content = new StreamContent(new StallingStream()) };
            }
        }

        private static HttpClient Http(bool answer) => new HttpClient(new Handler(answer)) { Timeout = Timeout.InfiniteTimeSpan };

        [Fact]
        public async Task AConnectionThatStopsSendingEndsWithAnError()
        {
            using (var t = new TempDir())
            {
                var target = t.Combine("dlss", "nvngx_dlss.dll");
                var watch = Stopwatch.StartNew();
                var e = await Assert.ThrowsAsync<IOException>(() =>
                    VerifiedDownload.ToFileAsync(Url, target, 1000, new string('a', 64), Http(true), null, CancellationToken.None, Idle));
                Assert.Contains("sent nothing", e.Message);
                Assert.True(watch.Elapsed < TimeSpan.FromSeconds(10));
                Assert.False(File.Exists(target));
                Assert.False(File.Exists(target + ".part"));
            }
        }

        [Fact]
        public async Task AServerThatNeverAnswersEndsWithAnError()
        {
            using (var t = new TempDir())
            {
                var e = await Assert.ThrowsAsync<IOException>(() =>
                    VerifiedDownload.ToFileAsync(Url, t.Combine("x.dll"), 1000, new string('a', 64), Http(false), null, CancellationToken.None, Idle));
                Assert.Contains("sent nothing", e.Message);
            }
        }

        [Fact]
        public async Task CancelEndsAStalledDownloadAsACancel()
        {
            using (var t = new TempDir())
            using (var cancel = new CancellationTokenSource())
            {
                cancel.CancelAfter(100);
                await Assert.ThrowsAnyAsync<OperationCanceledException>(() =>
                    VerifiedDownload.ToFileAsync(Url, t.Combine("x.dll"), 1000, new string('a', 64), Http(true), null, cancel.Token, TimeSpan.FromSeconds(30)));
                Assert.False(File.Exists(t.Combine("x.dll.part")));
            }
        }
    }
}
