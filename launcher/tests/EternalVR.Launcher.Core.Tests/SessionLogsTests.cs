using System.IO;
using System.Linq;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    public class SessionLogsTests
    {
        [Fact]
        public void OnlyTheNewestSessionFoldersAreKept()
        {
            using (var dir = new TempDir())
            {
                var names = new[] { "20260925-100000", "20260926-100000", "20260926-100000-2", "20260926-100000-10", "20260927-090000" };
                foreach (var n in names) Directory.CreateDirectory(dir.Combine(n));
                Directory.CreateDirectory(dir.Combine("not-a-session"));
                File.WriteAllText(dir.Combine("launcher.log"), "log");

                var deleted = SessionLogs.Prune(dir.Path, 3, null).Select(Path.GetFileName).OrderBy(n => n).ToList();

                Assert.Equal(new[] { "20260925-100000", "20260926-100000" }, deleted);
                Assert.True(Directory.Exists(dir.Combine("20260926-100000-10")));
                Assert.True(Directory.Exists(dir.Combine("20260926-100000-2")));
                Assert.True(Directory.Exists(dir.Combine("not-a-session")));
                Assert.True(File.Exists(dir.Combine("launcher.log")));
            }
        }

        [Fact]
        public void TheCurrentSessionIsNeverDeleted()
        {
            using (var dir = new TempDir())
            {
                Directory.CreateDirectory(dir.Combine("20200101-000000"));
                Directory.CreateDirectory(dir.Combine("20260927-090000"));
                SessionLogs.Prune(dir.Path, 0, dir.Combine("20200101-000000"));
                Assert.True(Directory.Exists(dir.Combine("20200101-000000")));
                Assert.False(Directory.Exists(dir.Combine("20260927-090000")));
                Assert.Empty(SessionLogs.Prune(dir.Combine("missing"), 1, null));
            }
        }
    }
}
