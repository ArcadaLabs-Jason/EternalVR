using System.IO;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    public class ProfileStoreTests
    {
        private static LauncherSettings Machine() => new LauncherSettings
        {
            GameDir = @"E:\Games\DOOMEternal", LayerDir = @"E:\EternalVR\layer", Runtime = @"E:\runtime.json",
        };

        [Fact]
        public void NamesAreTrimmedAndCheckedAsFileNames()
        {
            Assert.Equal("Riley", ProfileStore.NormaliseName("  Riley "));
            Assert.Null(ProfileStore.NormaliseName(""));
            Assert.Null(ProfileStore.NormaliseName("a/b"));
            Assert.Null(ProfileStore.NormaliseName("what?"));
            Assert.Null(ProfileStore.NormaliseName(".hidden"));
            Assert.Null(ProfileStore.NormaliseName("con"));
            Assert.Null(ProfileStore.NormaliseName(new string('x', ProfileStore.MaxNameLength + 1)));
            Assert.Equal(new string('x', ProfileStore.MaxNameLength), ProfileStore.NormaliseName(new string('x', ProfileStore.MaxNameLength)));
        }

        [Fact]
        public void AProfileKeepsThePlayersSettingsButNotTheMachines()
        {
            using (var t = new TempDir())
            {
                var store = new ProfileStore(t.Combine("profiles"));
                var intense = Machine();
                intense.Turn = TurnMode.Smooth;
                intense.TurnRate = 400;
                intense.Profile = "Evening";
                store.Save("Evening", intense);
                var text = File.ReadAllText(t.Combine("profiles", "Evening.ini"));
                Assert.DoesNotContain(@"E:\Games", text);
                Assert.DoesNotContain("runtime.json", text);
                Assert.DoesNotContain("profile =", text);

                var elsewhere = new LauncherSettings { GameDir = @"D:\Other", Runtime = LauncherSettings.SystemRuntime, Turn = TurnMode.Snap };
                var loaded = store.Load("Evening", elsewhere);
                Assert.Equal(TurnMode.Smooth, loaded.Turn);
                Assert.Equal(400, loaded.TurnRate);
                Assert.Equal(@"D:\Other", loaded.GameDir);
                Assert.Equal(LauncherSettings.SystemRuntime, loaded.Runtime);
                Assert.Equal("Evening", loaded.Profile);
            }
        }

        [Fact]
        public void ProfilesAreListedByNameAndCanBeDeleted()
        {
            using (var t = new TempDir())
            {
                var store = new ProfileStore(t.Combine("profiles"));
                Assert.Empty(store.Names());
                store.Save("riley", Machine());
                store.Save("Evening", Machine());
                t.Write("profiles/notes.txt", "not a profile");
                Assert.Equal(new[] { "Evening", "riley" }, store.Names());
                Assert.True(store.Exists("evening"));
                store.Delete("riley");
                Assert.Equal(new[] { "Evening" }, store.Names());
                Assert.Null(store.Load("riley", Machine()));
                store.Delete("nobody");
            }
        }

        [Fact]
        public void TheActiveProfileIsKeptInTheSettingsFile()
        {
            var s = Machine();
            Assert.DoesNotContain("profile =", s.Serialize());
            s.Profile = "Riley";
            var again = LauncherSettings.Parse(s.Serialize());
            Assert.Equal("Riley", again.Profile);
            Assert.Equal("Riley", again.WithDefaults().Profile);
            Assert.Equal(string.Empty, LauncherSettings.Parse("profile = a/b").Profile);
        }
    }
}
