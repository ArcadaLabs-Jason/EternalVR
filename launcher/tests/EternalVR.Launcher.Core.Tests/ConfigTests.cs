using System.IO;
using System.Linq;
using EternalVR.Launcher.Core.Game;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    public class CvarConfigTests
    {
        private const string Cfg =
            "configVersion 9\r\n//========\r\nbind \"W\" \"_moveforward\"\r\nr_swapInterval \"1\"\r\nm_smooth \"1\"\r\nseta r_dof 1\r\n";

        [Fact]
        public void ReadsCvarsAndSkipsCommands()
        {
            var c = CvarConfig.Parse(Cfg);
            Assert.Equal("1", c.Values["r_swapInterval"]);
            Assert.Equal("1", c.Values["r_dof"]);
            Assert.False(c.Values.ContainsKey("bind"));
            Assert.False(c.Values.ContainsKey("configVersion"));
            Assert.Equal(3, c.Values.Count);
        }

        [Fact]
        public void RoundTripIsByteExact()
        {
            Assert.Equal(Cfg, CvarConfig.Parse(Cfg).ToString());
            Assert.Equal("a \"1\"", CvarConfig.Parse("a \"1\"").ToString());
            Assert.Equal("a \"1\"\n", CvarConfig.Parse("a \"1\"\n").ToString());
        }

        [Fact]
        public void SetRewritesInPlaceKeepingPrefixAndLineEndings()
        {
            var c = CvarConfig.Parse(Cfg);
            c.Set("R_DOF", "0");
            c.Set("r_new", "5");
            var text = c.ToString();
            Assert.Contains("seta r_dof \"0\"\r\n", text);
            Assert.EndsWith("r_new \"5\"\r\n", text);
            Assert.Contains("bind \"W\" \"_moveforward\"\r\n", text);
        }

        [Fact]
        public void SetCollapsesDuplicateAssignments()
        {
            var c = CvarConfig.Parse("x \"1\"\ny \"2\"\nx \"3\"\n");
            c.Set("x", "9");
            Assert.Equal("y \"2\"\nx \"9\"\n", c.ToString());
        }

        [Fact]
        public void RemoveDeletesEveryAssignment()
        {
            var c = CvarConfig.Parse("x \"1\"\ny \"2\"\nx \"3\"\n");
            Assert.True(c.Remove("x"));
            Assert.False(c.Remove("x"));
            Assert.Equal("y \"2\"\n", c.ToString());
        }

        [Theory]
        [InlineData("// r_dof \"1\"", false)]
        [InlineData("bindSecondary \"Z\" \"_weapprev\"", false)]
        [InlineData("r_mode \"25\" // trailing comment", true)]
        [InlineData("   ", false)]
        [InlineData("unbindall", false)]
        public void LineClassification(string line, bool isCvar)
        {
            Assert.Equal(isCvar, CvarConfig.TryParseLine(line, out _, out _));
        }
    }

    public class ForcedKeyRestoreTests
    {
        private static readonly string[] Forced = { "r_hdrDisplay", "r_motionblur", "r_dof" };

        [Fact]
        public void ForcedKeysGoBackAndPlayerChangesStay()
        {
            var before = CvarConfig.Parse("r_mode \"25\"\nr_hdrDisplay \"1\"\nr_motionblur \"1\"\n");
            var after = CvarConfig.Parse("r_mode \"30\"\nr_hdrDisplay \"0\"\nr_motionblur \"1\"\nr_windowWidth \"2560\"\n");
            var changes = ForcedKeyRestore.Apply(before, after, Forced);
            Assert.Single(changes);
            Assert.Equal("r_hdrDisplay", changes[0].Key);
            Assert.Equal("1", changes[0].RestoredValue);
            var values = after.Values;
            Assert.Equal("1", values["r_hdrDisplay"]);
            Assert.Equal("30", values["r_mode"]);          // the player's change is kept
            Assert.Equal("2560", values["r_windowWidth"]);  // not forced, kept
        }

        [Fact]
        public void ForcedKeyAbsentBeforeIsRemoved()
        {
            var before = CvarConfig.Parse("r_mode \"25\"\n");
            var after = CvarConfig.Parse("r_mode \"25\"\nr_dof \"0\"\n");
            var changes = ForcedKeyRestore.Apply(before, after, Forced);
            Assert.Single(changes);
            Assert.Null(changes[0].RestoredValue);
            Assert.Equal("r_mode \"25\"\n", after.ToString());
        }

        [Fact]
        public void ForcedKeyDroppedDuringSessionIsPutBack()
        {
            // The rig saw the game drop r_hdrDisplay from DOOMEternalConfig.local (docs/rig-findings/launch.md).
            var before = CvarConfig.Parse("r_mode \"25\"\nr_hdrDisplay \"1\"\n");
            var after = CvarConfig.Parse("r_mode \"25\"\n");
            ForcedKeyRestore.Apply(before, after, Forced);
            Assert.Equal("1", after.Values["r_hdrDisplay"]);
        }

        [Fact]
        public void NothingToDoWhenUnchanged()
        {
            var before = CvarConfig.Parse("r_dof \"1\"\n");
            var after = CvarConfig.Parse("r_dof \"1\"\n");
            Assert.Empty(ForcedKeyRestore.Apply(before, after, Forced));
        }
    }

    public class GameLayoutTests
    {
        [Theory]
        [InlineData("76561197972611406", "12345678")]
        [InlineData("76561197960265728", "0")]
        [InlineData("123", null)]
        [InlineData("abc", null)]
        public void AccountIdFromSteamId64(string steamId, string expected)
        {
            Assert.Equal(expected, GameLayout.AccountIdFromSteamId64(steamId));
        }

        private static TempDir MakeTree()
        {
            var t = new TempDir();
            t.Write("saved/base/DOOMEternalConfig.cfg", "a \"1\"\n");
            t.Write("saved/base/DOOMEternalConfig.local", "b \"1\"\n");
            t.Write("saved/base/qconsole.log", "log");
            t.Write("steam/userdata/111/782330/remote/PROFILE/profile.bin", "p1");
            t.Write("steam/userdata/111/782330/remote/GAME-AUTOSAVE0/game.details", "s1");
            t.Write("steam/userdata/222/782330/remote/PROFILE/profile.bin", "p2");
            Directory.CreateDirectory(t.Combine("steam", "userdata", "333", "440"));
            return t;
        }

        [Fact]
        public void ActiveUserOnlyWhenKnown()
        {
            using (var t = MakeTree())
            {
                var locs = GameLayout.FindSettingsLocations(t.Combine("saved"), t.Combine("steam"), "222");
                Assert.Equal(new[] { "saved-games", "steam-222" }, locs.Select(l => l.Name));
            }
        }

        [Fact]
        public void EveryUserWhenActiveUnknown()
        {
            using (var t = MakeTree())
            {
                var locs = GameLayout.FindSettingsLocations(t.Combine("saved"), t.Combine("steam"), "0");
                Assert.Equal(new[] { "saved-games", "steam-111", "steam-222" }, locs.Select(l => l.Name));
            }
        }

        [Fact]
        public void NothingFoundGivesNoLocations()
        {
            using (var t = new TempDir())
                Assert.Empty(GameLayout.FindSettingsLocations(t.Combine("nope"), t.Combine("nosteam"), null));
        }

        [Fact]
        public void ConfigFilesAndSaveSlots()
        {
            using (var t = MakeTree())
            {
                var locs = GameLayout.FindSettingsLocations(t.Combine("saved"), t.Combine("steam"), "111");
                var saved = GameLayout.ConfigFilesOf(locs[0]);
                Assert.Equal(2, saved.Count); // qconsole.log and the missing config.json are not included
                Assert.Equal(new[] { Path.Combine("PROFILE", "profile.bin") }, GameLayout.ConfigFilesOf(locs[1]));
                var slots = GameLayout.SaveSlotFolders(locs[1].Path);
                Assert.Equal(new[] { "GAME-AUTOSAVE0" }, slots.Select(Path.GetFileName));
            }
        }
    }
}
