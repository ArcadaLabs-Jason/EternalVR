using System;
using System.Globalization;
using System.IO;
using System.Text;
using EternalVR.Launcher.Core.Data;

namespace EternalVR.Launcher.Core.Launch
{
    /// <summary>
    /// The eye size the last session really rendered at against the planned one, from the layer's status file
    /// (<c>render</c> and <c>render_planned</c>). On a graphics driver without usable present scaling (AMD Radeon RX 5000
    /// and 6000, some newer AMD drivers) the game renders each eye at its desktop window's size, below the plan. Kept in
    /// <c>render-cap.txt</c> in the data folder while the last session was capped, for the Play tab's "Each eye" line,
    /// the preflight and the report's system.txt.
    /// </summary>
    public sealed class RenderCap
    {
        public RenderCap(Extent real, Extent planned)
        {
            Real = real;
            Planned = planned;
        }

        /// <summary>The size of each eye's image the headset got.</summary>
        public Extent Real { get; }
        /// <summary>The size the render size planned for each eye.</summary>
        public Extent Planned { get; }

        /// <summary>Smaller than the plan on either side.</summary>
        public bool Capped => Real.Width < Planned.Width || Real.Height < Planned.Height;

        /// <summary>The smaller side's share of the plan, in whole percent rounded down.</summary>
        public int Percent => Math.Min(PercentOf(Real.Width, Planned.Width), PercentOf(Real.Height, Planned.Height));

        /// <summary>The session's warning (also the preflight's, after "Last session: ").</summary>
        public string Warning() =>
            "Your graphics driver cannot render above the window size, so each eye rendered at " + Spaced(Real) + ", "
            + Percent.ToString(CultureInfo.InvariantCulture) + "% of the planned " + Spaced(Planned)
            + ". A larger display helps; on NVIDIA, update the graphics driver.";

        /// <summary>The first lines of the Play tab's "Each eye" line while the last session was capped (separated by '\n').</summary>
        public string EachEyeText() =>
            Spaced(Real) + " last session, " + Percent.ToString(CultureInfo.InvariantCulture) + "% of the planned " + Spaced(Planned)
            + "\nYour graphics driver renders at the window's size";

        /// <summary>For the report's system.txt.</summary>
        public string Describe() =>
            Real + " of the planned " + Planned + " (" + Percent.ToString(CultureInfo.InvariantCulture) + "%)" + (Capped ? ", capped by the window" : string.Empty);

        /// <summary>From the status file's text; null without both sizes (the render size off, or VR never up).</summary>
        public static RenderCap FromStatus(string text)
        {
            Extent? real = null;
            Extent? planned = null;
            foreach (var raw in (text ?? string.Empty).Replace("\r\n", "\n").Split('\n'))
            {
                int eq = raw.IndexOf('=');
                if (eq <= 0) continue;
                var key = raw.Substring(0, eq).Trim().ToLowerInvariant();
                var value = ParseExtent(raw.Substring(eq + 1).Trim());
                if (key == "render") real = value;
                else if (key == "render_planned") planned = value;
            }
            if (real == null || planned == null || real.Value.IsEmpty || planned.Value.IsEmpty) return null;
            return new RenderCap(real.Value, planned.Value);
        }

        /// <summary>The remembered capped session; null when there is none or the file is unreadable.</summary>
        public static RenderCap Load(string path)
        {
            string text;
            try
            {
                if (!File.Exists(path)) return null;
                text = File.ReadAllText(path);
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException)
            {
                return null;
            }
            Extent? real = null;
            Extent? planned = null;
            foreach (var r in DataFile.ParseRecords(text.Replace('=', '|')))
            {
                var key = DataFile.Field(r, 0).ToLowerInvariant();
                var size = ParseExtent(DataFile.Field(r, 1));
                if (key == "render") real = size;
                else if (key == "render_planned") planned = size;
            }
            if (real == null || planned == null) return null;
            var cap = new RenderCap(real.Value, planned.Value);
            return cap.Capped ? cap : null;
        }

        /// <summary>Remembers <paramref name="cap"/> when it is capped, else forgets the last one. Atomic.</summary>
        public static void Remember(string path, RenderCap cap)
        {
            if (cap == null || !cap.Capped)
            {
                if (File.Exists(path)) File.Delete(path);
                return;
            }
            var sb = new StringBuilder();
            sb.Append("# EternalVR launcher: the last session rendered each eye below the planned size (the window's size)\n");
            sb.Append("render = ").Append(cap.Real).Append('\n');
            sb.Append("render_planned = ").Append(cap.Planned).Append('\n');
            Directory.CreateDirectory(Path.GetDirectoryName(path));
            FileUtil.WriteAllTextAtomic(path, sb.ToString());
        }

        private static int PercentOf(uint real, uint planned) => planned == 0 ? 100 : (int)((ulong)real * 100 / planned);

        private static string Spaced(Extent e) =>
            e.Width.ToString(CultureInfo.InvariantCulture) + " x " + e.Height.ToString(CultureInfo.InvariantCulture);

        // "WxH" with decimal sides, or null.
        private static Extent? ParseExtent(string text)
        {
            var parts = (text ?? string.Empty).ToLowerInvariant().Split('x');
            if (parts.Length != 2
                || !uint.TryParse(parts[0].Trim(), NumberStyles.None, CultureInfo.InvariantCulture, out var w)
                || !uint.TryParse(parts[1].Trim(), NumberStyles.None, CultureInfo.InvariantCulture, out var h))
                return null;
            return new Extent(w, h);
        }
    }
}
