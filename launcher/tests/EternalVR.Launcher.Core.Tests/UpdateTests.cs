using System;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text;
using EternalVR.Launcher.Core.Game;
using EternalVR.Launcher.Core.Update;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    public class UpdateTests
    {
        private static string Asset(string name, long size, string digest) =>
            $"{{\"name\":\"{name}\",\"size\":{size},\"browser_download_url\":\"https://github.com/ArcadaLabs-Jason/EternalVR/releases/download/x/{name}\""
            + (digest == null ? string.Empty : $",\"digest\":\"sha256:{digest}\"") + "}";

        private static string Release(string tag, bool draft, params string[] assets) =>
            $"{{\"tag_name\":\"{tag}\",\"name\":\"EternalVR {tag} (alpha)\",\"draft\":{(draft ? "true" : "false")},\"prerelease\":true,"
            + $"\"html_url\":\"https://github.com/ArcadaLabs-Jason/EternalVR/releases/tag/{tag}\","
            + "\"body\":\"# EternalVR (alpha)\\n\\nPlaces that open when you **look** at them now follow your `head`.\\n\\n## Download\\n\","
            + "\"assets\":[" + string.Join(",", assets) + "]}";

        private static readonly string A = new string('a', 64), B = new string('b', 64);

        [Fact]
        public void TheFeedKeepsPublishedReleasesWithAZipAndItsHash()
        {
            var json = "[" + string.Join(",",
                Release("v0.1.6", true, Asset("EternalVR-alpha-0.1.6-aaaaaaa.zip", 10, A)),
                Release("v0.1.5", false, Asset("EternalVR-alpha-0.1.5-1234567-symbols.zip", 99, B), Asset("EternalVR-alpha-0.1.5-1234567.zip", 2701043, A),
                    Asset("EternalVR-alpha-0.1.5-1234567.zip.sha256", 101, null)),
                Release("nightly", false, Asset("EternalVR-nightly.zip", 10, A)),
                Release("v0.1.4", false, Asset("EternalVR-alpha-0.1.4-90a904e.zip", 10, null), Asset("EternalVR-alpha-0.1.4-90a904e.zip.sha256", 101, null)),
                Release("v0.1.3", false, Asset("EternalVR-alpha-0.1.3-491955a.zip", 10, null))) + "]";
            var list = ReleaseFeed.Parse(json);
            // The draft, the tag that is not a version and the zip with no hash at all are left out.
            Assert.Equal(new[] { new Version(0, 1, 5), new Version(0, 1, 4) }, list.Select(r => r.Version));
            var r5 = list[0];
            Assert.Equal("EternalVR-alpha-0.1.5-1234567.zip", r5.ZipName);
            Assert.Equal(2701043, r5.ZipSize);
            Assert.Equal(A, r5.ZipSha256);
            Assert.EndsWith(".zip.sha256", r5.Sha256Url.AbsolutePath);
            Assert.Equal("https://github.com/ArcadaLabs-Jason/EternalVR/releases/tag/v0.1.5", r5.Page.AbsoluteUri);
            Assert.Null(list[1].ZipSha256);
            Assert.NotNull(list[1].Sha256Url);
            Assert.Equal("Places that open when you look at them now follow your head.", ReleaseFeed.Summary(r5.Notes));
        }

        [Fact]
        public void OnlyANewerReleaseThatIsNotSkippedIsOffered()
        {
            var list = ReleaseFeed.Parse("[" + Release("v0.1.5", false, Asset("EternalVR-alpha-0.1.5-1234567.zip", 10, A)) + ","
                + Release("v0.1.4", false, Asset("EternalVR-alpha-0.1.4-90a904e.zip", 10, A)) + "]");
            Assert.Equal(new Version(0, 1, 5), ReleaseFeed.Newest(list, new Version(0, 1, 4, 0), null).Version);
            Assert.Null(ReleaseFeed.Newest(list, new Version(0, 1, 5, 0), null));
            Assert.Null(ReleaseFeed.Newest(list, new Version(0, 2, 0), null));
            Assert.Null(ReleaseFeed.Newest(list, new Version(0, 1, 4), new Version(0, 1, 5)));
            Assert.Null(ReleaseFeed.Newest(new AvailableRelease[0], new Version(0, 1, 4), null));
            Assert.Throws<FormatException>(() => ReleaseFeed.Parse("{\"message\":\"API rate limit exceeded\"}"));
        }

        [Fact]
        public void TheShaFileGivesItsHash()
        {
            Assert.Equal(A, ReleaseFeed.Sha256FromSumsFile(A.ToUpperInvariant() + "  EternalVR-alpha-0.1.4-90a904e.zip\n"));
            Assert.Null(ReleaseFeed.Sha256FromSumsFile("not a hash"));
        }

        [Fact]
        public void TheStateSavesTheChoiceTheSkipAndTheLastCheck()
        {
            using (var t = new TempDir())
            {
                var path = t.Combine("updates", "state.txt");
                var fresh = UpdateState.Load(path);
                Assert.True(fresh.Check);
                var now = new DateTime(2026, 9, 30, 6, 0, 0, DateTimeKind.Utc);
                Assert.True(fresh.Due(now));
                new UpdateState { Check = true, Skipped = new Version(0, 1, 5), Found = new Version(0, 1, 6), LastCheck = now }.Save(path);
                var s = UpdateState.Load(path);
                Assert.Equal(new Version(0, 1, 5), s.Skipped);
                Assert.Equal(new Version(0, 1, 6), s.Found);
                Assert.Equal(now, s.LastCheck);
                Assert.False(s.Due(now.AddMinutes(59)));
                Assert.True(s.Due(now.AddHours(1)));
                // A clock set back: asked again rather than never.
                Assert.True(s.Due(now.AddHours(-1)));
                s.Check = false;
                s.Save(path);
                Assert.False(UpdateState.Load(path).Due(now.AddDays(3)));
            }
        }

        /// <summary>A release zip as make-release builds it: one top folder, SHA256SUMS.txt listing every other file.</summary>
        private static void MakeZip(string zipPath, Action<ZipArchive, string> extra = null, bool listAll = true, params (string Name, string Text)[] files) =>
            MakeZip(zipPath, extra, listAll, null, files);

        /// <summary><paramref name="written"/> changes what a file holds after its hash was listed (a tampered file).</summary>
        private static void MakeZip(string zipPath, Action<ZipArchive, string> extra, bool listAll, Func<string, string, string> written,
            params (string Name, string Text)[] files)
        {
            const string top = "EternalVR-alpha-0.1.5-1234567/";
            using (var zip = ZipFile.Open(zipPath, ZipArchiveMode.Create))
            {
                var sums = new StringBuilder();
                foreach (var f in files)
                {
                    using (var w = new StreamWriter(zip.CreateEntry(top + f.Name).Open())) w.Write(written?.Invoke(f.Name, f.Text) ?? f.Text);
                    if (listAll || f.Name != "extra.txt")
                        sums.Append(Sha(f.Text)).Append("  ").Append(f.Name).Append('\n');
                }
                using (var w = new StreamWriter(zip.CreateEntry(top + "SHA256SUMS.txt").Open())) w.Write(sums.ToString());
                extra?.Invoke(zip, top);
            }
        }

        private static string Sha(string text)
        {
            using (var sha = System.Security.Cryptography.SHA256.Create())
                return KnownBuilds.ToHex(sha.ComputeHash(Encoding.UTF8.GetBytes(text)));
        }

        private static readonly (string, string)[] Release015 =
        {
            ("EternalVR.Launcher.exe", "new exe"), ("EternalVR.Launcher.Core.dll", "new core"), ("layer/EternalVR.dll", "new layer"),
            ("data/cpu-saver.txt", "new data"), ("docs/INSTALL.md", "new doc"),
        };

        [Fact]
        public void AReleaseIsStagedCheckedAndInstalledOverTheOldFolder()
        {
            using (var t = new TempDir())
            {
                MakeZip(t.Combine("r.zip"), null, true, Release015);
                UpdatePackage.Stage(t.Combine("r.zip"), t.Combine("staging"), null);
                Assert.Equal("new layer", t.Read("staging/layer/EternalVR.dll"));

                t.Write("program/EternalVR.Launcher.exe", "old exe");
                t.Write("program/layer/EternalVR.dll", "old layer");
                t.Write("program/my-notes.txt", "the player's own file");
                var installed = UpdatePackage.Install(t.Combine("staging"), t.Combine("program"));
                Assert.Equal(6, installed.Count);
                Assert.Equal("new exe", t.Read("program/EternalVR.Launcher.exe"));
                Assert.Equal("new layer", t.Read("program/layer/EternalVR.dll"));
                Assert.Equal("new doc", t.Read("program/docs/INSTALL.md"));
                Assert.Equal("the player's own file", t.Read("program/my-notes.txt"));
                Assert.Equal("old exe", t.Read("program/EternalVR.Launcher.exe.evr-old"));
                Assert.Equal(0, UpdatePackage.RemoveOldFiles(t.Combine("program")));
                Assert.False(File.Exists(t.Combine("program", "EternalVR.Launcher.exe.evr-old")));
                Assert.False(File.Exists(t.Combine("program", "layer", "EternalVR.dll.evr-old")));
            }
        }

        [Fact]
        public void AFailedInstallPutsTheOldFilesBack()
        {
            using (var t = new TempDir())
            {
                MakeZip(t.Combine("r.zip"), null, true, Release015.Concat(new[] { ("zz-last.txt", "x") }).ToArray());
                UpdatePackage.Stage(t.Combine("r.zip"), t.Combine("staging"), null);
                t.Write("program/EternalVR.Launcher.exe", "old exe");
                t.Write("program/layer/EternalVR.dll", "old layer");
                // A folder where the last file goes: copying it fails after the others were replaced.
                Directory.CreateDirectory(t.Combine("program", "zz-last.txt"));
                Assert.ThrowsAny<Exception>(() => UpdatePackage.Install(t.Combine("staging"), t.Combine("program")));
                Assert.Equal("old exe", t.Read("program/EternalVR.Launcher.exe"));
                Assert.Equal("old layer", t.Read("program/layer/EternalVR.dll"));
                Assert.False(File.Exists(t.Combine("program", "EternalVR.Launcher.Core.dll")));
                Assert.Empty(Directory.GetFiles(t.Combine("program"), "*.evr-old*", SearchOption.AllDirectories));
            }
        }

        [Fact]
        public void AZipThatIsNotAWholeCheckedReleaseIsRefused()
        {
            using (var t = new TempDir())
            {
                // A path out of the folder.
                MakeZip(t.Combine("slip.zip"), (zip, top) => zip.CreateEntry(top + "../../evil.dll"), true, Release015);
                Assert.Throws<IOException>(() => UpdatePackage.Stage(t.Combine("slip.zip"), t.Combine("s1"), null));
                Assert.False(File.Exists(t.Combine("evil.dll")));

                // A file its sums do not list.
                MakeZip(t.Combine("extra.zip"), null, false, Release015.Concat(new[] { ("extra.txt", "x") }).ToArray());
                Assert.Contains("does not list", Assert.Throws<IOException>(() => UpdatePackage.Stage(t.Combine("extra.zip"), t.Combine("s2"), null)).Message);

                // A file changed after the sums were made.
                MakeZip(t.Combine("changed.zip"), null, true, (name, text) => name == "data/cpu-saver.txt" ? "tampered" : text, Release015);
                Assert.Contains("does not match", Assert.Throws<IOException>(() => UpdatePackage.Stage(t.Combine("changed.zip"), t.Combine("s3"), null)).Message);

                // No layer.
                MakeZip(t.Combine("nolayer.zip"), null, true, Release015.Where(f => f.Item1 != "layer/EternalVR.dll").ToArray());
                Assert.Contains("EternalVR.dll", Assert.Throws<IOException>(() => UpdatePackage.Stage(t.Combine("nolayer.zip"), t.Combine("s4"), null)).Message);
            }
        }
    }
}
