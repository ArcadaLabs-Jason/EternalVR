using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>"Weapon directions...": the table read and written as the layer reads it (weapon_directions.cpp), clockwise
    /// from up.</summary>
    public class WeaponDirectionTableTests
    {
        [Fact]
        public void TheDefaultIsTheGamesWheelAndWritesNothing()
        {
            Assert.Equal(new[] { 1, 5, 2, 7, 3, 6, 4, 8 }, WeaponDirectionTable.GameWheel());
            Assert.Equal(WeaponDirectionTable.GameWheel(), WeaponDirectionTable.Parse(string.Empty));
            Assert.Equal(WeaponDirectionTable.GameWheel(), WeaponDirectionTable.Parse(null));
            Assert.Equal(string.Empty, WeaponDirectionTable.Format(WeaponDirectionTable.GameWheel()));
            // A table that names the defaults is the game's wheel too: the line goes.
            Assert.Equal(string.Empty, WeaponDirectionTable.Format(WeaponDirectionTable.Parse("up=1,left=4")));
        }

        [Fact]
        public void FourDirectionsKeepsTheGameWheelsCardinals()
        {
            var four = WeaponDirectionTable.FourDirections();
            Assert.Equal(new[] { 1, 0, 2, 0, 3, 0, 4, 0 }, four);
            var wheel = WeaponDirectionTable.GameWheel();
            for (int i = 0; i < 8; i += 2) Assert.Equal(wheel[i], four[i]);
            Assert.Equal("up=1,up_right=none,right=2,down_right=none,down=3,down_left=none,left=4,up_left=none",
                WeaponDirectionTable.Format(four));
            Assert.Equal(four, WeaponDirectionTable.Parse(WeaponDirectionTable.Format(four)));
        }

        [Fact]
        public void EntriesAreReadAsTheLayerReadsThem()
        {
            // Any case, a hyphen for the underscore, spaces around; 0 and none are both none; directions left out keep
            // the default.
            Assert.Equal(new[] { 5, 5, 8, 7, 3, 0, 0, 8 }, WeaponDirectionTable.Parse(" Up = 5, down-left=none ,left=0,right=8"));
            Assert.Equal(new[] { 1, 5, 2, 7, 3, 6, 4, 0 }, WeaponDirectionTable.Parse("UP_LEFT=NONE"));
        }

        [Fact]
        public void EntriesThatCannotBeReadKeepTheDefault()
        {
            // An unknown direction, a slot past 8, no slot, a slot that is no number, an empty entry, two digits.
            Assert.Equal(new[] { 1, 5, 2, 7, 3, 6, 4, 8 }, WeaponDirectionTable.Parse("north=1,up=9,down,left=x,,right=02"));
            Assert.Equal(new[] { 1, 5, 6, 7, 3, 6, 4, 8 }, WeaponDirectionTable.Parse("up=-1,right=6,down=3x"));
        }

        [Fact]
        public void AnyTableRoundTripsThroughItsText()
        {
            var tables = new[]
            {
                new[] { 8, 7, 6, 5, 4, 3, 2, 1 },
                new[] { 0, 0, 0, 0, 0, 0, 0, 0 },
                new[] { 0, 0, 0, 0, 0, 6, 0, 0 },
                new[] { 1, 5, 0, 0, 0, 0, 0, 0 },
            };
            foreach (var table in tables)
            {
                var text = WeaponDirectionTable.Format(table);
                Assert.StartsWith("up=", text);
                Assert.Equal(table, WeaponDirectionTable.Parse(text));
            }
            Assert.Equal("up=none,up_right=none,right=none,down_right=none,down=none,down_left=none,left=none,up_left=none",
                WeaponDirectionTable.Format(new int[8]));
            // Through launcher.ini as well.
            var s = new LauncherSettings { WeaponDirections = WeaponDirectionTable.Format(WeaponDirectionTable.FourDirections()) };
            Assert.Equal(WeaponDirectionTable.FourDirections(), WeaponDirectionTable.Parse(LauncherSettings.Parse(s.Serialize()).WeaponDirections));
        }

        [Fact]
        public void TheWeaponsAreTheGamesSlotsOneToEight()
        {
            Assert.Equal(new[] { "Combat Shotgun", "Heavy Cannon", "Plasma Rifle", "Rocket Launcher", "Super Shotgun", "Ballista", "Chaingun", "BFG-9000" },
                WeaponDirectionTable.Weapons);
            Assert.Equal(new[] { "up", "up_right", "right", "down_right", "down", "down_left", "left", "up_left" }, WeaponDirectionTable.Directions);
        }
    }
}
