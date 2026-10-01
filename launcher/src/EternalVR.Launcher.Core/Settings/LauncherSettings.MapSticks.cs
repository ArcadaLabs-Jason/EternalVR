using System.Collections.Generic;
using System.Text;

namespace EternalVR.Launcher.Core.Settings
{
    /// <summary>Which stick pans the Dossier's map (the layer's <c>ETERNALVR_MAP_STICKS</c>): the weapon hand's, or the other
    /// hand's; the second stick zooms and rotates the map.</summary>
    public enum MapPanStick { Weapon, Other }

    /// <summary>The Dossier map's sticks in launcher.ini: <c>map_sticks = weapon|other</c>.</summary>
    public sealed partial class LauncherSettings
    {
        /// <summary>Which stick pans the Dossier's map; the weapon hand's by default.</summary>
        public MapPanStick MapSticks { get; set; } = MapPanStick.Weapon;

        /// <summary>The settings file's and the layer's <c>ETERNALVR_MAP_STICKS</c> value: weapon or other.</summary>
        public static string MapSticksName(MapPanStick m) => m == MapPanStick.Other ? "other" : "weapon";

        /// <summary>Reads <c>map_sticks</c> (any case); weapon for anything else.</summary>
        private void ReadMapSticks(IDictionary<string, string> map)
        {
            if (map.TryGetValue("map_sticks", out var ms)) MapSticks = Pick(ms, MapPanStick.Weapon, ("other", MapPanStick.Other));
        }

        private void WriteMapSticks(StringBuilder sb) => sb.AppendLine("map_sticks = " + MapSticksName(MapSticks));
    }
}
