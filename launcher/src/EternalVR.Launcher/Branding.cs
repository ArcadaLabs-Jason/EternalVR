using System;
using System.Drawing;
using System.IO;

namespace EternalVR.Launcher
{
    /// <summary>
    /// The branding embedded in the exe: the window icon (all sizes), the header strip across the window's top, Ko-fi's
    /// cup for the support link, and Discord's and GitHub's marks for the report links (each brand's official file,
    /// unaltered: branding/README.md).
    /// </summary>
    internal static class Branding
    {
        /// <summary>The Ko-fi page the header's support link opens (the same one as the README's).</summary>
        public static readonly Uri SupportPage = new Uri("https://ko-fi.com/FanciestPeanut");

        /// <summary>The multi-size icon; null if the resource is missing.</summary>
        public static Icon WindowIcon()
        {
            using (var s = Resource("EternalVR.Launcher.eternalvr.ico"))
                return s == null ? null : new Icon(s);
        }

        /// <summary>The header strip (1440x240, drawn at half size); null if the resource is missing.</summary>
        public static Image HeaderStrip()
        {
            var s = Resource("EternalVR.Launcher.launcher-header.png");
            // GDI+ reads the image lazily from the stream, so the stream stays open with the image.
            return s == null ? null : Image.FromStream(s);
        }

        /// <summary>Ko-fi's cup symbol (branding/kofi-symbol-64.png, 80x64); null if the resource is missing.</summary>
        public static Image KofiCup()
        {
            var s = Resource("EternalVR.Launcher.kofi-symbol.png");
            return s == null ? null : Image.FromStream(s);
        }

        /// <summary>Discord's symbol in its colour (branding/discord-symbol-blurple.png, 528x400); null if the resource is missing.</summary>
        public static Image DiscordSymbol()
        {
            var s = Resource("EternalVR.Launcher.discord-symbol.png");
            return s == null ? null : Image.FromStream(s);
        }

        /// <summary>GitHub's mark, the Invertocat in black (branding/github-invertocat-black.png, 294x288); null if the resource is missing.</summary>
        public static Image GitHubMark()
        {
            var s = Resource("EternalVR.Launcher.github-mark.png");
            return s == null ? null : Image.FromStream(s);
        }

        private static Stream Resource(string name) => typeof(Branding).Assembly.GetManifestResourceStream(name);
    }
}
