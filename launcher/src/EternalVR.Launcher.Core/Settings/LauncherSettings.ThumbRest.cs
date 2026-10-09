using System.Collections.Generic;
using System.Text;

namespace EternalVR.Launcher.Core.Settings
{
    /// <summary>The thumb-rest weapon wheel (the layer's <c>ETERNALVR_THUMBREST_WHEEL</c>): a thumb on its thumb rest, then the
    /// other stick pushed (edge); the other stick picks for as long as the thumb rests (full); off (the default). The layer's
    /// 'extreme' (the turn stick always picks) is not offered until it is redesigned; a file that names it reads as off.</summary>
    public enum ThumbRestMode { Edge, Full, Off }

    /// <summary>What the thumb-rest wheel picks with (the layer's <c>ETERNALVR_THUMBREST_PICK</c>): the game's weapon wheel, or
    /// one weapon slot per stick direction.</summary>
    public enum ThumbRestPick { Wheel, Directions }

    /// <summary>The thumb-rest wheel in launcher.ini: <c>thumb_rest_wheel = edge|full|extreme|off</c>,
    /// <c>thumb_rest_pick = wheel|directions</c>, <c>thumb_rest_face_touch = 0|1</c> (off by default),
    /// <c>thumb_rest_slowdown = 1|0</c> (on by default) and <c>weapon_directions</c>, written by hand only.</summary>
    public sealed partial class LauncherSettings
    {
        /// <summary>The thumb-rest wheel; off by default until more players have tried it.</summary>
        public ThumbRestMode ThumbRest { get; set; } = ThumbRestMode.Off;
        /// <summary>The game's weapon wheel by default, or a weapon slot per stick direction.</summary>
        public ThumbRestPick ThumbRestPicks { get; set; } = ThumbRestPick.Wheel;
        /// <summary>A thumb on A/B or X/Y counts as resting, on controllers without a thumb rest; off by default.</summary>
        public bool ThumbRestFaceTouch { get; set; } = false;
        /// <summary>Time slows while the thumb-rest wheel holds the game's wheel open, as for the game's own; on by default.</summary>
        public bool ThumbRestSlowdown { get; set; } = true;
        /// <summary>The slot of each stick direction under Weapon by direction (the layer's <c>ETERNALVR_WEAPON_DIRECTIONS</c>,
        /// <c>up=1,up_right=2,...</c>); set in the file by hand, empty for the layer's own table.</summary>
        public string WeaponDirections { get; set; } = string.Empty;

        /// <summary>The settings file's and the layer's <c>ETERNALVR_THUMBREST_WHEEL</c> value.</summary>
        public static string ThumbRestName(ThumbRestMode w)
        {
            switch (w)
            {
                case ThumbRestMode.Full: return "full";
                case ThumbRestMode.Off: return "off";
                default: return "edge";
            }
        }

        /// <summary>The settings file's value: wheel or directions.</summary>
        public static string ThumbRestPickName(ThumbRestPick p) => p == ThumbRestPick.Directions ? "directions" : "wheel";

        /// <summary>The layer's <c>ETERNALVR_THUMBREST_PICK</c> value: wheel or slots.</summary>
        public static string ThumbRestPickEnvironment(ThumbRestPick p) => p == ThumbRestPick.Directions ? "slots" : "wheel";

        /// <summary>Reads the thumb-rest keys (any case); a value this version does not know takes the default.</summary>
        private void ReadThumbRest(IDictionary<string, string> map)
        {
            if (map.TryGetValue("thumb_rest_wheel", out var tw))
                ThumbRest = Pick(tw, ThumbRestMode.Off, ("edge", ThumbRestMode.Edge), ("full", ThumbRestMode.Full));
            if (map.TryGetValue("thumb_rest_pick", out var tp)) ThumbRestPicks = Pick(tp, ThumbRestPick.Wheel, ("directions", ThumbRestPick.Directions));
            if (map.TryGetValue("thumb_rest_face_touch", out var ft)) ThumbRestFaceTouch = On(ft);
            if (map.TryGetValue("thumb_rest_slowdown", out var sl)) ThumbRestSlowdown = Switch(sl) ?? true;
            if (map.TryGetValue("weapon_directions", out var wd)) WeaponDirections = wd.Trim();
        }

        private void WriteThumbRest(StringBuilder sb)
        {
            sb.AppendLine("thumb_rest_wheel = " + ThumbRestName(ThumbRest));
            sb.AppendLine("thumb_rest_pick = " + ThumbRestPickName(ThumbRestPicks));
            sb.AppendLine("thumb_rest_face_touch = " + (ThumbRestFaceTouch ? "1" : "0"));
            sb.AppendLine("thumb_rest_slowdown = " + (ThumbRestSlowdown ? "1" : "0"));
            if (!string.IsNullOrWhiteSpace(WeaponDirections)) sb.AppendLine("weapon_directions = " + WeaponDirections.Trim());
        }
    }
}
