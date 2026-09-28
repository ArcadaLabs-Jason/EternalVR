using System.Drawing;
using System.IO;

namespace EternalVR.Launcher
{
    /// <summary>The branding embedded in the exe: the window icon (all sizes) and the header strip across the window's top.</summary>
    internal static class Branding
    {
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

        private static Stream Resource(string name) => typeof(Branding).Assembly.GetManifestResourceStream(name);
    }
}
