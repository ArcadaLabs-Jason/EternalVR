using System;
using System.IO;
using EternalVR.Launcher.Core.Update;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>When the update dialog can install a release, and what it says when it cannot.</summary>
    public class UpdateOfferTests
    {
        [Fact]
        public void AReleaseThatCanBeWrittenAndNoGameCanInstall()
        {
            Assert.Null(InstallBlock.For(isRelease: true, writeError: null, gameRunning: false));
        }

        [Fact]
        public void ABuildFolderIsNotUpdatedWhateverElseHolds()
        {
            var block = InstallBlock.For(isRelease: false, writeError: "denied", gameRunning: true);
            Assert.Equal(InstallBlockKind.NotRelease, block.Kind);
            Assert.True(block.Lasting);
            Assert.Contains("BUILD-INFO.txt", block.Text);
            Assert.Contains("release from its page", block.Text);
        }

        [Fact]
        public void AFolderThatCannotBeWrittenSaysWhy()
        {
            var block = InstallBlock.For(isRelease: true, writeError: "Access to the path 'C:\\Program Files\\EternalVR\\x' is denied.", gameRunning: true);
            Assert.Equal(InstallBlockKind.NotWritable, block.Kind);
            Assert.True(block.Lasting);
            // The system's message is quoted once, without its own full stop before the bracket.
            Assert.Contains("(Access to the path 'C:\\Program Files\\EternalVR\\x' is denied). ", block.Text);
        }

        [Fact]
        public void TheRunningGameOnlyWaits()
        {
            var block = InstallBlock.For(isRelease: true, writeError: null, gameRunning: true);
            Assert.Equal(InstallBlockKind.GameRunning, block.Kind);
            Assert.False(block.Lasting);
            Assert.StartsWith("Quit DOOM Eternal", block.Text);
        }

        [Fact]
        public void TheReasonsAreShortFactsWithoutAdvice()
        {
            foreach (var block in new[] { InstallBlock.For(false, null, false), InstallBlock.For(true, "denied", false), InstallBlock.For(true, null, true) })
            {
                UiText.AssertNoAdvice(block.Text, block.Kind.ToString());
                Assert.True(block.Text.Length <= 140, block.Kind + " is long: " + block.Text);
            }
        }

        [Fact]
        public void OnlyAFolderWithBuildInfoIsARelease()
        {
            using (var t = new TempDir())
            {
                Assert.False(InstallBlock.IsRelease(t.Path));
                File.WriteAllText(Path.Combine(t.Path, "BUILD-INFO.txt"), "EternalVR alpha 0.1.14\n");
                Assert.True(InstallBlock.IsRelease(t.Path));
            }
        }

        [Fact]
        public void ABuildOfAnOldBranchSeesEveryNewerTagAsNewer()
        {
            // A test build made from a branch of 0.1.4 (assembly version 0.1.4.0) would be offered 0.1.14: why only a release
            // (InstallBlock.IsRelease) is checked at start.
            var latest = new AvailableRelease(new Version(0, 1, 14), "EternalVR v0.1.14 (alpha)", new Uri("https://github.com/x/y/releases/tag/v0.1.14"),
                "EternalVR-alpha-0.1.14-3ba1728.zip", new Uri("https://github.com/x/y/releases/download/v0.1.14/z.zip"), 10, new string('a', 64), null, string.Empty);
            Assert.Same(latest, ReleaseFeed.Newest(new[] { latest }, new Version(0, 1, 4, 0), null));
            // A Debug build carries -dev only in its informational version: its assembly version is the release's.
            Assert.Null(ReleaseFeed.Newest(new[] { latest }, new Version(0, 1, 14, 0), null));
            // A version without a build number counts as .0, not as older than every release of that minor.
            Assert.Equal(new Version(0, 2, 0), ReleaseFeed.Plain(new Version(0, 2)));
            Assert.Null(ReleaseFeed.Newest(new[] { latest }, new Version(0, 2), null));
        }
    }
}
