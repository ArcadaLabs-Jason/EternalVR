using System;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Security.Cryptography;
using System.Threading;
using System.Threading.Tasks;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    public class DlssDownloadsTests
    {
        /// <summary>Serves fixed bytes for every request and counts the requests.</summary>
        private sealed class FakeHandler : HttpMessageHandler
        {
            private readonly byte[] body;
            private readonly HttpStatusCode status;
            public int Requests;

            public FakeHandler(byte[] body, HttpStatusCode status = HttpStatusCode.OK)
            {
                this.body = body;
                this.status = status;
            }

            protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancellationToken)
            {
                Requests++;
                return Task.FromResult(new HttpResponseMessage(status) { Content = new ByteArrayContent(body) });
            }
        }

        private static readonly byte[] Dll = Enumerable.Range(0, 200000).Select(i => (byte)(i * 7)).ToArray();

        private static string Sha(byte[] b)
        {
            using (var sha = SHA256.Create()) return BitConverter.ToString(sha.ComputeHash(b)).Replace("-", string.Empty).ToLowerInvariant();
        }

        private static DlssRelease Release(byte[] pinned) => new DlssRelease(new Version(310, 9, 1, 0),
            new Uri("https://raw.githubusercontent.com/NVIDIA/DLSS/v310.9.1/lib/Windows_x86_64/rel/nvngx_dlss.dll"), pinned.Length, Sha(pinned),
            new Uri("https://github.com/NVIDIA/DLSS/blob/v310.9.1/LICENSE.txt"));

        [Fact]
        public void TheShippedListOffersNvidiasOwnFile()
        {
            var newest = DlssDownloads.Parse(TestData.Read("dlss-downloads.txt")).Newest;
            Assert.Equal(new Version(310, 9, 1, 0), newest.Version);
            // NVIDIA's repository only, the release DLL (the dev one carries a watermark), over https.
            Assert.Equal("raw.githubusercontent.com", newest.Url.Host);
            Assert.StartsWith("/NVIDIA/DLSS/", newest.Url.AbsolutePath);
            Assert.Contains("/rel/nvngx_dlss.dll", newest.Url.AbsolutePath);
            Assert.Equal(58956912, newest.Size);
            Assert.Equal("3975567b8943c53acce397f2b72380092f84f162d00b0d2c7d08a1025c563983", newest.Sha256);
            Assert.Equal("github.com", newest.License.Host);
            Assert.EndsWith("LICENSE.txt", newest.License.AbsolutePath);
        }

        [Fact]
        public void TheNewestVersionIsOffered()
        {
            var sha = new string('a', 64);
            var list = DlssDownloads.Parse(
                $"dll | 310.3.0.0 | https://x/a.dll | 10 | {sha} | https://x/l\n"
                + $"dll | 310.10.0.0 | https://x/c.dll | 10 | {sha} | https://x/l\n"
                + $"dll | 310.9.1.0 | https://x/b.dll | 10 | {sha} | https://x/l\n");
            Assert.Equal(new Version(310, 10, 0, 0), list.Newest.Version);
            Assert.Null(DlssDownloads.Parse("# nothing\n").Newest);
        }

        [Theory]
        [InlineData("dll | 310.9.1.0 | http://x/a.dll | 10 | {0} | https://x/l")]
        [InlineData("dll | 310.9.1.0 | https://x/a.dll | 0 | {0} | https://x/l")]
        [InlineData("dll | 310.9.1.0 | https://x/a.dll | 10 | abc | https://x/l")]
        [InlineData("dll | 310.9.1.0 | https://x/a.dll | 10 | {0} | not a url")]
        [InlineData("dll | 310 | https://x/a.dll | 10 | {0} | https://x/l")]
        [InlineData("zip | 310.9.1.0 | https://x/a.dll | 10 | {0} | https://x/l")]
        public void ABadLineIsRefused(string line)
        {
            Assert.Throws<FormatException>(() => DlssDownloads.Parse(string.Format(line, new string('a', 64))));
        }

        [Fact]
        public async Task APinnedDownloadIsKeptInItsVersionFolder()
        {
            using (var t = new TempDir())
            {
                var handler = new FakeHandler(Dll);
                double last = 0;
                var path = await DlssDownloads.FetchAsync(Release(Dll), t.Combine("dlss"), new HttpClient(handler), new Progress<double>(p => last = p), CancellationToken.None);
                Assert.Equal(t.Combine("dlss", "310.9.1.0", "nvngx_dlss.dll"), path);
                Assert.Equal(Dll, File.ReadAllBytes(path));
                Assert.False(File.Exists(path + ".part"));

                // Already there and matching: no second download.
                Assert.Equal(path, await DlssDownloads.FetchAsync(Release(Dll), t.Combine("dlss"), new HttpClient(handler), null, CancellationToken.None));
                Assert.Equal(1, handler.Requests);
            }
        }

        [Fact]
        public async Task ADownloadThatIsNotThePinnedFileIsNotKept()
        {
            using (var t = new TempDir())
            {
                var other = (byte[])Dll.Clone();
                other[1234] ^= 1;
                var e = await Assert.ThrowsAsync<IOException>(() =>
                    DlssDownloads.FetchAsync(Release(Dll), t.Combine("dlss"), new HttpClient(new FakeHandler(other)), null, CancellationToken.None));
                Assert.Contains("SHA-256", e.Message);
                Assert.Empty(Directory.GetFiles(t.Combine("dlss"), "*", SearchOption.AllDirectories));

                // Too short, and too long: refused by size.
                await Assert.ThrowsAsync<IOException>(() => DlssDownloads.FetchAsync(Release(Dll), t.Combine("dlss"),
                    new HttpClient(new FakeHandler(Dll.Take(1000).ToArray())), null, CancellationToken.None));
                await Assert.ThrowsAsync<IOException>(() => DlssDownloads.FetchAsync(Release(Dll), t.Combine("dlss"),
                    new HttpClient(new FakeHandler(Dll.Concat(new byte[] { 1 }).ToArray())), null, CancellationToken.None));
                Assert.Empty(Directory.GetFiles(t.Combine("dlss"), "*", SearchOption.AllDirectories));
            }
        }

        [Fact]
        public async Task AChangedFileIsDownloadedAgainAndAServerErrorKeepsNothing()
        {
            using (var t = new TempDir())
            {
                var target = DlssDownloads.PathFor(t.Combine("dlss"), Release(Dll));
                Directory.CreateDirectory(Path.GetDirectoryName(target));
                File.WriteAllBytes(target, new byte[] { 1, 2, 3 });
                Assert.False(DlssDownloads.Matches(target, Release(Dll)));
                await Assert.ThrowsAsync<HttpRequestException>(() => DlssDownloads.FetchAsync(Release(Dll), t.Combine("dlss"),
                    new HttpClient(new FakeHandler(new byte[0], HttpStatusCode.NotFound)), null, CancellationToken.None));
                Assert.False(File.Exists(target + ".part"));
                await DlssDownloads.FetchAsync(Release(Dll), t.Combine("dlss"), new HttpClient(new FakeHandler(Dll)), null, CancellationToken.None);
                Assert.True(DlssDownloads.Matches(target, Release(Dll)));
            }
        }
    }
}
