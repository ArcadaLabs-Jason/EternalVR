using System;
using System.IO;
using EternalVR.Launcher.Core.Game;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    public class FileHashCacheTests
    {
        [Fact]
        public void AnUnchangedFileIsNotReadAgain()
        {
            using (var t = new TempDir())
            {
                var exe = t.Write("game/DOOMEternalx64vk.exe", "build 1");
                int reads = 0;
                var cache = new FileHashCache(p => { reads++; return KnownBuilds.Sha256OfFile(p); });
                var first = cache.Get(exe);
                Assert.Equal(first, cache.Get(exe));
                Assert.Equal(first, cache.Get(t.Combine("game", ".", "DOOMEternalx64vk.exe")));
                Assert.Equal(1, reads);

                // A game update: new size and time, read again.
                File.WriteAllText(exe, "build 2, longer");
                File.SetLastWriteTimeUtc(exe, DateTime.UtcNow.AddMinutes(1));
                Assert.NotEqual(first, cache.Get(exe));
                Assert.Equal(2, reads);

                // Same size, a new write time: read again too.
                File.WriteAllText(exe, "build 3, longer");
                File.SetLastWriteTimeUtc(exe, DateTime.UtcNow.AddMinutes(2));
                cache.Get(exe);
                Assert.Equal(3, reads);
            }
        }

        [Fact]
        public void TheBuildCheckHashesTheExeOncePerChange()
        {
            using (var t = new TempDir())
            {
                var exe = t.Write("game/DOOMEternalx64vk.exe", "known");
                var hash = KnownBuilds.Sha256OfFile(exe);
                int reads = 0;
                var builds = new KnownBuilds(new[] { new KnownBuild(hash, "1", "test") }, p => { reads++; return KnownBuilds.Sha256OfFile(p); });
                for (int i = 0; i < 5; i++) Assert.Equal(BuildStatus.Known, builds.Check(exe).Status);
                Assert.Equal(1, reads);
                File.Delete(exe);
                Assert.Equal(BuildStatus.Missing, builds.Check(exe).Status);
            }
        }
    }
}
