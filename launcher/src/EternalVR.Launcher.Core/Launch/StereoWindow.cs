using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;

namespace EternalVR.Launcher.Core.Launch
{
    /// <summary>A display's area in physical pixels (the whole display: a taskbar over the game window does not matter to the headset).</summary>
    public sealed class DisplayArea
    {
        public DisplayArea(string name, int x, int y, int width, int height, bool primary, bool isVirtual = false)
        {
            Name = name;
            X = x;
            Y = y;
            Width = width;
            Height = height;
            Primary = primary;
            Virtual = isVirtual;
        }

        public string Name { get; }
        public int X { get; }
        public int Y { get; }
        public int Width { get; }
        public int Height { get; }
        public bool Primary { get; }
        /// <summary>A virtual display (a virtual display driver or a headset app's virtual monitor, <see cref="VirtualDisplays"/>).</summary>
        public bool Virtual { get; }

        public override string ToString() =>
            string.Format(CultureInfo.InvariantCulture, "{0} {1},{2} {3}x{4}{5}{6}", Name, X, Y, Width, Height,
                Primary ? " primary" : string.Empty, Virtual ? " virtual" : string.Empty);
    }

    /// <summary>Recognises virtual displays by their adapter's name or their monitor's hardware id.</summary>
    public static class VirtualDisplays
    {
        private static readonly string[] Markers =
        {
            "Virtual Display", "Virtual Desktop Monitor", "Virtual Monitor", "IddSample", "Parsec Virtual", "SudoVDA", "SudoMaker",
            "MTT1337",
        };

        public static bool IsVirtual(string adapterName, string monitorId) =>
            Markers.Any(m => (adapterName ?? string.Empty).IndexOf(m, StringComparison.OrdinalIgnoreCase) >= 0
                || (monitorId ?? string.Empty).IndexOf(m, StringComparison.OrdinalIgnoreCase) >= 0);
    }

    /// <summary>
    /// Where the stereo game window goes: its client area, which is one eye's image when the game renders at its
    /// window's size, or the small desktop mirror when the layer sets the render size (<see cref="Mirror"/>).
    /// </summary>
    public sealed class StereoWindowPlan
    {
        public int X { get; set; }
        public int Y { get; set; }
        public int Width { get; set; }
        public int Height { get; set; }
        /// <summary>The display it is placed on; null when no display was known (placed at 0,0).</summary>
        public DisplayArea Display { get; set; }
        /// <summary>True when the display is too small for the requested per-eye size and the size was scaled down.</summary>
        public bool Reduced { get; set; }
        /// <summary>The window is only the desktop mirror: each eye renders at the headset's size, not at this one.</summary>
        public bool Mirror { get; set; }

        /// <summary>The layer's <c>ETERNALVR_WINDOW</c> value (client area x,y,width,height).</summary>
        public string EnvironmentValue => string.Format(CultureInfo.InvariantCulture, "{0},{1},{2},{3}", X, Y, Width, Height);
    }

    /// <summary>
    /// Stereo renders each eye at the game window's client size (docs/VR_STEREO.md, "Render size"), and a
    /// window cannot be larger than its display, so the per-eye size (default 2064x2100) is fitted to a
    /// display: the one that holds the largest image at the requested aspect (the primary one on a tie),
    /// scaled down uniformly when even that one is too small. The client area goes at the display's
    /// top-left corner; the title bar sits above it, off the display.
    /// </summary>
    public static class StereoWindow
    {
        /// <summary>The desktop mirror's client size (16:9; the eye image is letterboxed into it).</summary>
        public const int MirrorWidth = 1280;
        public const int MirrorHeight = 720;

        /// <summary>
        /// The desktop mirror when the layer sets the render size (docs/rig-findings/render-size.md): the eyes do
        /// not depend on the window, so it is small, on a virtual display when there is one (else the primary),
        /// at the display's top-left, scaled down to fit a smaller display.
        /// </summary>
        public static StereoWindowPlan Mirror(IEnumerable<DisplayArea> displays) => Mirror(displays, MirrorWidth, MirrorHeight);

        /// <summary><see cref="Mirror(IEnumerable{DisplayArea})"/> with the client size <paramref name="width"/> x <paramref name="height"/>.</summary>
        public static StereoWindowPlan Mirror(IEnumerable<DisplayArea> displays, int width, int height)
        {
            if (width <= 0 || height <= 0) throw new ArgumentException("the mirror size must be positive");
            var candidates = (displays ?? Enumerable.Empty<DisplayArea>()).Where(d => d.Width > 0 && d.Height > 0).ToList();
            var d = candidates.FirstOrDefault(c => c.Virtual) ?? candidates.FirstOrDefault(c => c.Primary) ?? candidates.FirstOrDefault();
            if (d == null)
                return new StereoWindowPlan { Width = width, Height = height, Mirror = true };
            double scale = Math.Min(1.0, Math.Min((double)d.Width / width, (double)d.Height / height));
            return new StereoWindowPlan
            {
                X = d.X, Y = d.Y,
                Width = Math.Max(1, (int)Math.Floor(width * scale)),
                Height = Math.Max(1, (int)Math.Floor(height * scale)),
                Display = d, Reduced = scale < 1.0, Mirror = true,
            };
        }

        public static StereoWindowPlan Fit(IEnumerable<DisplayArea> displays, int eyeWidth, int eyeHeight)
        {
            if (eyeWidth <= 0 || eyeHeight <= 0) throw new ArgumentException("the eye size must be positive");
            var candidates = (displays ?? Enumerable.Empty<DisplayArea>()).Where(d => d.Width > 0 && d.Height > 0).ToList();
            if (candidates.Count == 0)
                return new StereoWindowPlan { Width = eyeWidth, Height = eyeHeight };

            DisplayArea best = null;
            double bestScale = 0;
            foreach (var d in candidates)
            {
                double scale = Math.Min(1.0, Math.Min((double)d.Width / eyeWidth, (double)d.Height / eyeHeight));
                bool better = best == null || scale > bestScale + 1e-9 || (Math.Abs(scale - bestScale) <= 1e-9 && d.Primary && !best.Primary);
                if (better)
                {
                    best = d;
                    bestScale = scale;
                }
            }
            int w = Math.Min(best.Width, (int)Math.Floor(eyeWidth * bestScale));
            int h = Math.Min(best.Height, (int)Math.Floor(eyeHeight * bestScale));
            return new StereoWindowPlan { X = best.X, Y = best.Y, Width = w, Height = h, Display = best, Reduced = bestScale < 1.0 };
        }
    }
}
