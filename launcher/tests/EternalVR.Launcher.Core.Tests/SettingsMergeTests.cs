using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    public class SettingsMergeTests
    {
        private const string Written = "# EternalVR launcher settings.\nschema_version = 3\nmirror_display = auto\nturn = smooth\n";

        [Fact]
        public void AHandEditOnDiskIsKeptWhenTheLauncherDidNotChangeThatKey()
        {
            var mine = Written.Replace("turn = smooth", "turn = snap");
            var disk = Written.Replace("mirror_display = auto", "mirror_display = 0,-1080");
            var merged = LauncherSettings.MergeHandEdits(Written, mine, disk);
            Assert.Contains("mirror_display = 0,-1080\n", merged);
            Assert.Contains("turn = snap\n", merged);
            Assert.StartsWith("# EternalVR launcher settings.\n", merged);
        }

        [Fact]
        public void TheLauncherWinsAKeyBothChanged()
        {
            var mine = Written.Replace("turn = smooth", "turn = snap");
            var disk = Written.Replace("turn = smooth", "turn = off");
            Assert.Contains("turn = snap\n", LauncherSettings.MergeHandEdits(Written, mine, disk));
        }

        [Fact]
        public void AKeyAddedOnDiskIsKeptAndNothingChangesWithoutAnEdit()
        {
            var merged = LauncherSettings.MergeHandEdits(Written, Written, Written + "extra_args = +com_skipSignInManager 0\n");
            Assert.EndsWith("extra_args = +com_skipSignInManager 0\n", merged);
            Assert.Equal(Written, LauncherSettings.MergeHandEdits(Written, Written, Written));
        }
    }
}
