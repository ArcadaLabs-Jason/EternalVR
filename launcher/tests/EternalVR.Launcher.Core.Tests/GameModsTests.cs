using System.IO;
using System.Linq;
using EternalVR.Launcher.Core.Report;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>Other mods of the game in system.txt: the names in the game folder's Mods folder and any mod loader.</summary>
    public class GameModsTests
    {
        [Fact]
        public void TheModsNamesAndTheLoaderAreListed()
        {
            using (var t = new TempDir())
            {
                t.Write(@"game\DOOMEternalx64vk.exe", "exe");
                t.Write(@"game\EternalModInjector.bat", "loader");
                t.Write(@"game\DEternal_loadMods.exe", "loader");
                t.Write(@"game\base\idRehash.exe", "not at the top of the game folder");
                t.Write(@"game\Mods\zz-weapons.zip", "secret mod contents");
                t.Write(@"game\Mods\Better HUD.zip", "secret mod contents");
                t.Write(@"game\Mods\old\skins.zip", "below Mods");
                var lines = GameMods.Describe(t.Combine("game"));
                Assert.Equal(new[] { "game mods", "game mod loader" }, lines.Select(kv => kv.Key));
                Assert.Equal(@"3 in Mods: Better HUD.zip, old\, zz-weapons.zip", lines[0].Value);
                Assert.Equal("DEternal_loadMods.exe, EternalModInjector.bat", lines[1].Value);
            }
        }

        [Fact]
        public void ALongListIsCut()
        {
            using (var t = new TempDir())
            {
                for (int i = 1; i <= GameMods.NamesListed + 5; i++) t.Write($@"game\Mods\mod{i:00}.zip", "x");
                var mods = GameMods.Describe(t.Combine("game"))[0].Value;
                Assert.StartsWith("35 in Mods: mod01.zip, mod02.zip, ", mods);
                Assert.EndsWith(", mod30.zip, and 5 more", mods);
                Assert.DoesNotContain("mod31", mods);
            }
        }

        [Fact]
        public void NoModsFolderNoLoaderAndNoGameAreSaid()
        {
            using (var t = new TempDir())
            {
                t.Write(@"game\DOOMEternalx64vk.exe", "exe");
                var lines = GameMods.Describe(t.Combine("game"));
                Assert.Equal("no Mods folder", lines[0].Value);
                Assert.Equal("none found", lines[1].Value);

                Directory.CreateDirectory(t.Combine("game", "Mods"));
                Assert.Equal("Mods folder empty", GameMods.Describe(t.Combine("game"))[0].Value);

                foreach (var missing in new[] { null, "", t.Combine("nothing") })
                    Assert.All(GameMods.Describe(missing), kv => Assert.Equal("game not found", kv.Value));
            }
        }
    }
}
