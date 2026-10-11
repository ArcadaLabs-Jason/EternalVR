using System;
using System.Linq;
using System.Text;

namespace EternalVR.Launcher.Core.Settings
{
    /// <summary>
    /// Weapon by direction's table (launcher.ini <c>weapon_directions</c>, the layer's <c>ETERNALVR_WEAPON_DIRECTIONS</c>):
    /// the weapon slot of each stick direction, 1 to 8, or 0 for none. Read as the layer reads it
    /// (src/features/input/weapon_directions.cpp): <c>direction=slot</c> entries separated by commas, the directions up,
    /// up_right, right, down_right, down, down_left, left and up_left (any case, a hyphen for the underscore), the slot 1 to
    /// 8, or 0 or none; an entry that cannot be read, and a direction left out, keep the default. A direction set to none
    /// leaves its part of the circle to its nearest neighbours that have a weapon.
    /// </summary>
    public static class WeaponDirectionTable
    {
        /// <summary>The directions, clockwise from up, as the setting is written; every table is indexed in this order.</summary>
        public static readonly string[] Directions = { "up", "up_right", "right", "down_right", "down", "down_left", "left", "up_left" };

        /// <summary>The weapons by slot (index 0 is slot 1), as the game names them.</summary>
        public static readonly string[] Weapons =
        {
            "Combat Shotgun", "Heavy Cannon", "Plasma Rifle", "Rocket Launcher", "Super Shotgun", "Ballista", "Chaingun", "BFG-9000",
        };

        /// <summary>The layer's default: the game's own wheel, clockwise from up (combat shotgun, super shotgun, heavy cannon,
        /// chaingun, plasma rifle, ballista, rocket launcher, BFG).</summary>
        public static int[] GameWheel() => new[] { 1, 5, 2, 7, 3, 6, 4, 8 };

        /// <summary>The game wheel's weapons on up, right, down and left, nothing on the diagonals: each takes a quarter.</summary>
        public static int[] FourDirections() => new[] { 1, 0, 2, 0, 3, 0, 4, 0 };

        /// <summary>The table <paramref name="text"/> sets, from the game's wheel.</summary>
        public static int[] Parse(string text)
        {
            var slots = GameWheel();
            foreach (var entry in (text ?? string.Empty).Split(','))
            {
                int equals = entry.IndexOf('=');
                if (equals < 0) continue;
                int direction = Array.IndexOf(Directions, Normalised(entry.Substring(0, equals)));
                int slot = Slot(Normalised(entry.Substring(equals + 1)));
                if (direction >= 0 && slot >= 0) slots[direction] = slot;
            }
            return slots;
        }

        /// <summary>The setting's text for <paramref name="slots"/>: all eight directions, or empty for the game's wheel (the
        /// line is then left out of launcher.ini).</summary>
        public static string Format(int[] slots)
        {
            if (slots == null || slots.Length != Directions.Length || slots.SequenceEqual(GameWheel())) return string.Empty;
            var sb = new StringBuilder();
            for (int i = 0; i < Directions.Length; i++)
            {
                if (i > 0) sb.Append(',');
                sb.Append(Directions[i]).Append('=').Append(slots[i] >= 1 && slots[i] <= 8 ? slots[i].ToString() : "none");
            }
            return sb.ToString();
        }

        /// <summary>A name or value as the layer compares it: trimmed, ASCII lower case, a hyphen read as an underscore.</summary>
        private static string Normalised(string text)
        {
            var sb = new StringBuilder();
            foreach (char c in text.Trim(' ', '\t', '\r', '\n'))
                sb.Append(c == '-' ? '_' : c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c);
            return sb.ToString();
        }

        /// <summary>0 to 8, or -1 when the value is no slot.</summary>
        private static int Slot(string value)
        {
            if (value == "none") return 0;
            return value.Length == 1 && value[0] >= '0' && value[0] <= '8' ? value[0] - '0' : -1;
        }
    }
}
