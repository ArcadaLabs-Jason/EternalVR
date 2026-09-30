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

        private const string Index = "/interaction_profiles/valve/index_controller";
        private const string Touch = "/interaction_profiles/oculus/touch_controller";

        private static string Reports(string profile, string hand) =>
            "[   12.345] [ 4242] controllers: the runtime reports " + profile + " for the " + hand + " hand\n";

        [Fact]
        public void TheControlsEditorOpensOnTheControllersOfTheLastGame()
        {
            using (var dir = new TempDir())
            {
                dir.Write("20260926-100000/eternalvr-20260926-100001-100.log", Reports(Touch, "right"));
                // The newest session: the controllers changed during it, and the right hand's last report counts.
                dir.Write("20260927-090000/eternalvr-20260927-090001-200.log",
                    Reports("no controller", "right") + Reports(Touch, "right") + Reports(Index, "left") + Reports(Index, "right"));
                // A later session whose game never reported controllers is passed over.
                dir.Write("20260927-100000/eternalvr-20260927-100001-300.log", "[    0.000] log start\n");
                Directory.CreateDirectory(dir.Combine("20260927-110000"));

                Assert.Equal(Index, SessionLogs.LastControllerProfile(dir.Path));
            }
        }

        [Fact]
        public void NoGameWithControllersLeavesTheControlsEditorOnItsDefault()
        {
            using (var dir = new TempDir())
            {
                Assert.Null(SessionLogs.LastControllerProfile(dir.Combine("missing")));
                dir.Write("20260927-090000/eternalvr-20260927-090001-200.log",
                    Reports("no controller", "right") + Reports(Index, "left"));
                Assert.Null(SessionLogs.LastControllerProfile(dir.Path));
            }
        }
    }
}
