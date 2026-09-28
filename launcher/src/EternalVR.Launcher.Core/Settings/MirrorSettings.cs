using System;
using System.Globalization;

namespace EternalVR.Launcher.Core.Settings
{
    /// <summary>The shape of the flat screen cutscenes play on (the layer's <c>ETERNALVR_CINEMA_ASPECT</c>).</summary>
    public enum CinemaShape { Wide16x9, Wide16x10, Full }

    /// <summary>
    /// The desktop mirror window's display, size and crop, and the cutscene screen's shape (the layer's
    /// <c>ETERNALVR_MIRROR_DISPLAY</c>, <c>ETERNALVR_MIRROR_SIZE</c>, <c>ETERNALVR_MIRROR_CROP</c> and
    /// <c>ETERNALVR_CINEMA_ASPECT</c>; docs/rig-findings/render-size.md, section 7). The display is kept as a desktop
    /// point, not a number: the launcher's and Windows' display orders can differ.
    /// </summary>
    public static class MirrorSettings
    {
        /// <summary>The launcher places the window (a virtual display if there is one, else the primary).</summary>
        public const string DisplayAuto = "auto";
        public const string DisplayPrimary = "primary";

        /// <summary>The window's client sizes offered by name, in <see cref="Sizes"/> order; Medium is the default.</summary>
        public static readonly string[] SizeNames = { "Small (960x540)", "Medium (1280x720)", "Large (1920x1080)", "Fill the monitor" };
        public static readonly string[] Sizes = { "960x540", "1280x720", "1920x1080", "fill" };
        public const string DefaultSize = "1280x720";
        /// <summary>The window covers its whole monitor, borderless, for others watching (the layer's <c>fill</c>).</summary>
        public const string SizeFill = "fill";
        /// <summary>The layer's range per side.</summary>
        public const int MinSide = 64;
        public const int MaxSide = 8192;

        /// <summary><c>auto</c>, <c>primary</c> or <c>x,y</c> (a desktop point), or null when it is none of them.</summary>
        public static string NormaliseDisplay(string text)
        {
            var t = (text ?? string.Empty).Trim();
            if (t.Length == 0 || string.Equals(t, DisplayAuto, StringComparison.OrdinalIgnoreCase)) return DisplayAuto;
            if (string.Equals(t, DisplayPrimary, StringComparison.OrdinalIgnoreCase)) return DisplayPrimary;
            return TryParsePoint(t, out int x, out int y) ? Point(x, y) : null;
        }

        public static string Point(int x, int y) =>
            x.ToString(CultureInfo.InvariantCulture) + "," + y.ToString(CultureInfo.InvariantCulture);

        public static bool TryParsePoint(string text, out int x, out int y)
        {
            x = y = 0;
            var parts = (text ?? string.Empty).Split(',');
            return parts.Length == 2
                && int.TryParse(parts[0].Trim(), NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out x)
                && int.TryParse(parts[1].Trim(), NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out y);
        }

        /// <summary>The size as <c>WxH</c> within the layer's range or <c>fill</c>, or null when it is neither.</summary>
        public static string NormaliseSize(string text)
        {
            if (IsFill(text)) return SizeFill;
            return TryParseSize(text, out int w, out int h)
                ? w.ToString(CultureInfo.InvariantCulture) + "x" + h.ToString(CultureInfo.InvariantCulture)
                : null;
        }

        public static bool IsFill(string text) => string.Equals((text ?? string.Empty).Trim(), SizeFill, StringComparison.OrdinalIgnoreCase);

        public static bool TryParseSize(string text, out int width, out int height)
        {
            width = height = 0;
            var parts = (text ?? string.Empty).Trim().ToLowerInvariant().Split('x');
            return parts.Length == 2
                && int.TryParse(parts[0].Trim(), NumberStyles.None, CultureInfo.InvariantCulture, out width)
                && int.TryParse(parts[1].Trim(), NumberStyles.None, CultureInfo.InvariantCulture, out height)
                && width >= MinSide && width <= MaxSide && height >= MinSide && height <= MaxSide;
        }

        /// <summary>The layer's <c>ETERNALVR_MIRROR_DISPLAY</c> value: <c>launcher</c> keeps the launcher's placement.</summary>
        public static string DisplayEnvironment(string display)
        {
            var d = NormaliseDisplay(display) ?? DisplayAuto;
            return d == DisplayAuto ? "launcher" : d;
        }

        /// <summary>The layer's <c>ETERNALVR_CINEMA_ASPECT</c> value, also the ini's <c>cinema_aspect</c>.</summary>
        public static string CinemaName(CinemaShape c) => c == CinemaShape.Wide16x10 ? "16:10" : c == CinemaShape.Full ? "full" : "16:9";

        public static CinemaShape ParseCinema(string text)
        {
            var t = (text ?? string.Empty).Trim();
            if (t == "16:10") return CinemaShape.Wide16x10;
            if (string.Equals(t, "full", StringComparison.OrdinalIgnoreCase)) return CinemaShape.Full;
            return CinemaShape.Wide16x9;
        }
    }
}
