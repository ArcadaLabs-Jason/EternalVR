using System;
using System.Globalization;
using System.IO;
using System.Text;
using EternalVR.Launcher.Core.Data;
using EternalVR.Launcher.Core.Settings;

namespace EternalVR.Launcher.Core.Launch
{
    /// <summary>
    /// The headset's view limits from the last runtime probe that answered, kept in <c>headset.txt</c> in the data folder
    /// (not launcher.ini: they are this machine's headset, not a setting, and no profile or Reset touches them). The Play
    /// tab's "Each eye" line reads them, since the probe itself runs only at Play (it can take 30 s and start the runtime).
    /// </summary>
    public static class LastHeadset
    {
        /// <summary>The remembered limits (<see cref="ViewLimits.EyesSideBySide"/> 1); null when there are none or the file is unreadable.</summary>
        public static ViewLimits Load(string path)
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
            var limits = new ViewLimits();
            foreach (var r in DataFile.ParseRecords(text.Replace('=', '|')))
            {
                var key = DataFile.Field(r, 0).ToLowerInvariant();
                var size = ParseExtent(DataFile.Field(r, 1));
                if (size == null) continue;
                if (key == "recommended") limits.Recommended = size.Value;
                else if (key == "max_image") limits.MaxImageRect = size.Value;
                else if (key == "max_swapchain") limits.MaxSwapchain = size.Value;
            }
            return limits.Recommended.IsEmpty ? null : limits;
        }

        /// <summary>Writes atomically (temporary file, then replace).</summary>
        public static void Save(string path, ViewLimits limits)
        {
            var sb = new StringBuilder();
            sb.Append("# EternalVR launcher: the headset's sizes from the last runtime probe that answered (the Play tab's Each eye line)\n");
            sb.Append("recommended = ").Append(limits.Recommended).Append('\n');
            sb.Append("max_image = ").Append(limits.MaxImageRect).Append('\n');
            sb.Append("max_swapchain = ").Append(limits.MaxSwapchain).Append('\n');
            Directory.CreateDirectory(Path.GetDirectoryName(path));
            FileUtil.WriteAllTextAtomic(path, sb.ToString());
        }

        /// <summary>
        /// The Play tab's "Each eye" line: the size each eye renders at with these settings and <paramref name="last"/>
        /// (null: none remembered), worked out as the launch does (<see cref="RenderSizeChoice.Decide"/>). With auto it says
        /// how that compares with the headset's recommended size, per side as Virtual Desktop gives its resolution. When the
        /// last session rendered below its plan (<paramref name="cap"/>: the graphics driver held each eye at the window's
        /// size), it says what that session really got instead.
        /// </summary>
        public static string EachEye(LauncherSettings s, ViewLimits last, RenderCap cap = null)
        {
            if (s.Mode != VrMode.Stereo) return "The game's own resolution (mono)";
            var setting = LauncherSettings.NormaliseRenderSize(s.RenderSize) ?? LauncherSettings.RenderSizeAuto;
            if (setting == LauncherSettings.RenderSizeOff) return "The game window's size (render_size off)";
            if (cap != null && cap.Capped) return cap.EachEyeText();
            var choice = RenderSizeChoice.Decide(setting, s.RenderScale, last == null ? null : new OpenXrProbeResult { Limits = last });
            if (choice.Size == null) return "Set from your headset when you press Play";
            var size = choice.Size.Value;
            if (!choice.IsAuto) return size + " (fixed size)";
            var asked = last.Recommended;
            var percent = Math.Round(100.0 * size.Width / asked.Width, MidpointRounding.AwayFromZero);
            return size + ", " + percent.ToString("0", CultureInfo.InvariantCulture) + "% of the " + asked + " your headset asked for";
        }

        // "WxH" with decimal sides (0 allowed: no limit), or null.
        private static Extent? ParseExtent(string text)
        {
            var parts = text.ToLowerInvariant().Split('x');
            if (parts.Length != 2
                || !uint.TryParse(parts[0], NumberStyles.None, CultureInfo.InvariantCulture, out var w)
                || !uint.TryParse(parts[1], NumberStyles.None, CultureInfo.InvariantCulture, out var h))
                return null;
            return new Extent(w, h);
        }
    }
}
