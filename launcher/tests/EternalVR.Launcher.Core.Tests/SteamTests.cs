using System.IO;
using System.Linq;
using EternalVR.Launcher.Core.Game;
using EternalVR.Launcher.Core.Steam;
using EternalVR.Launcher.Core.Text;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    public class VdfTests
    {
        private const string LibraryFolders = @"""libraryfolders""
{
	""0""
	{
		""path""		""C:\\Program Files (x86)\\Steam""
		""apps""
		{
			""228980""		""1016964654""
		}
	}
	// a comment
	""1""
	{
		""path""		""E:\\SteamLibrary""
		""label""		""""
		""apps""
		{
			""782330""		""96126414731""
		}
	}
}";

        [Fact]
        public void ParsesNestedBlocksAndEscapes()
        {
            var root = VdfParser.Parse(LibraryFolders);
            Assert.Equal(@"E:\SteamLibrary", root.Path("libraryfolders", "1").GetString("path"));
            Assert.Equal("", root.Path("libraryfolders", "1").GetString("label"));
            Assert.Equal("96126414731", root.Path("libraryfolders", "1", "apps").GetString("782330"));
            Assert.Null(root.Path("libraryfolders", "7"));
        }

        [Fact]
        public void KeysAreCaseInsensitive()
        {
            var root = VdfParser.Parse("\"AppState\" { \"InstallDir\" \"DOOMEternal\" }");
            Assert.Equal("DOOMEternal", root["appstate"].GetString("installdir"));
        }

        [Fact]
        public void BareTokensAndConditionalsAreAccepted()
        {
            var root = VdfParser.Parse("key { sub value [$WIN32] other \"x y\" }");
            Assert.Equal("value", root["key"].GetString("sub"));
            Assert.Equal("x y", root["key"].GetString("other"));
        }

        [Theory]
        [InlineData("\"a\" { \"b\" \"c\"")]
        [InlineData("\"a\" }")]
        [InlineData("\"a\" \"unterminated")]
        [InlineData("\"a\"")]
        public void MalformedInputThrows(string text)
        {
            Assert.Throws<VdfFormatException>(() => VdfParser.Parse(text));
        }

        [Fact]
        public void LibraryFoldersListsSteamRootFirstWithoutDuplicates()
        {
            var libs = SteamLibraries.ParseLibraryFolders(LibraryFolders, @"C:\Program Files (x86)\Steam\");
            Assert.Equal(new[] { @"C:\Program Files (x86)\Steam", @"E:\SteamLibrary" }, libs);
        }

        [Fact]
        public void OldLibraryFormatIsRead()
        {
            var text = "\"LibraryFolders\" { \"TimeNextStatsReport\" \"1\" \"1\" \"D:\\\\Games\" }";
            var libs = SteamLibraries.ParseLibraryFolders(text, null);
            Assert.Equal(new[] { @"D:\Games" }, libs);
        }

        [Fact]
        public void LibrariesListingAppUsesTheAppsBlock()
        {
            Assert.Equal(new[] { @"E:\SteamLibrary" }, SteamLibraries.LibrariesListingApp(LibraryFolders, "782330"));
        }
    }

    public class SteamDiscoveryTests
    {
        private static void MakeLibrary(TempDir t, string lib, bool withGame, string installDir = "DOOMEternal")
        {
            Directory.CreateDirectory(t.Combine(lib, "steamapps"));
            if (!withGame) return;
            t.Write($"{lib}/steamapps/appmanifest_782330.acf",
                $"\"AppState\"\n{{\n\t\"appid\"\t\t\"782330\"\n\t\"installdir\"\t\t\"{installDir}\"\n\t\"buildid\"\t\t\"25216728\"\n}}\n");
            t.Write($"{lib}/steamapps/common/{installDir}/{GameLayout.RetailExe}", "exe");
        }

        private static void WriteLibraryFolders(TempDir t, params string[] libs)
        {
            var body = string.Join("\n", libs.Select((l, i) =>
                $"\"{i}\" {{ \"path\" \"{t.Combine(l).Replace("\\", "\\\\")}\" \"apps\" {{ \"10\" \"1\" }} }}"));
            t.Write("steam/steamapps/libraryfolders.vdf", "\"libraryfolders\" {\n" + body + "\n}");
        }

        [Fact]
        public void FindsTheGameThroughTheAppManifestEvenWhenTheAppsListIsStale()
        {
            using (var t = new TempDir())
            {
                MakeLibrary(t, "steam", withGame: false);
                MakeLibrary(t, "lib2", withGame: true);
                WriteLibraryFolders(t, "steam", "lib2");
                var found = SteamLibraries.FindApp(t.Combine("steam"), GameLayout.SteamAppId, GameLayout.RetailExe);
                Assert.NotNull(found);
                Assert.Equal(t.Combine("lib2", "steamapps", "common", "DOOMEternal"), found.GameRoot);
                Assert.Equal("25216728", found.BuildId);
            }
        }

        [Fact]
        public void ManifestWithoutTheExeIsNotAMatch()
        {
            using (var t = new TempDir())
            {
                MakeLibrary(t, "steam", withGame: true);
                File.Delete(t.Combine("steam", "steamapps", "common", "DOOMEternal", GameLayout.RetailExe));
                WriteLibraryFolders(t, "steam");
                Assert.Null(SteamLibraries.FindApp(t.Combine("steam"), GameLayout.SteamAppId, GameLayout.RetailExe));
            }
        }

        [Fact]
        public void MissingLibraryFoldersFallsBackToTheSteamRoot()
        {
            using (var t = new TempDir())
            {
                MakeLibrary(t, "steam", withGame: true, installDir: "DOOM Eternal Custom");
                var found = SteamLibraries.FindApp(t.Combine("steam"), GameLayout.SteamAppId, GameLayout.RetailExe);
                Assert.EndsWith("DOOM Eternal Custom", found.GameRoot);
            }
        }

        [Fact]
        public void SteamVrRuntimesAreFoundPerLibrary()
        {
            using (var t = new TempDir())
            {
                t.Write("lib/steamapps/common/SteamVR/steamxr_win64.json", "{}");
                Directory.CreateDirectory(t.Combine("other"));
                var found = SteamLibraries.FindSteamVrRuntimes(new[] { t.Combine("other"), t.Combine("lib") }).ToList();
                Assert.Single(found);
                Assert.EndsWith("steamxr_win64.json", found[0]);
            }
        }
    }
}
