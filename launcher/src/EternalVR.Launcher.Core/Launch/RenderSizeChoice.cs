using System;
using System.Globalization;
using EternalVR.Launcher.Core.Settings;

namespace EternalVR.Launcher.Core.Launch
{
    /// <summary>
    /// The size a stereo launch starts the game at (docs/rig-findings/render-size.md, section 7). With a size, the
    /// game gets it as <c>ETERNALVR_RENDER_SIZE=WxH</c> and <c>+r_windowWidth/Height</c>, so its first swapchain is
    /// already at the final size and nothing is resized mid-session. Without one (auto and no runtime answer), the
    /// layer decides in-game (<c>ETERNALVR_RENDER_SIZE=auto</c>) and the game starts at the mirror's size.
    /// With auto, Resolution's base (<see cref="ResolutionBase"/>) says what the scale multiplies: Auto is the layer's own rule
    /// (the runtime's recommendation fitted into the pixel budget), the other two are worked out here into a fixed size, so the
    /// layer needs no change. Without the runtime's answer (or the panel, for the native panel) the launch falls back to Auto,
    /// exactly as before, and says so in the log.
    /// </summary>
    public sealed class RenderSizeChoice
    {
        /// <summary>Route S keeps both eyes side by side in one swapchain image, as the layer's ring does in stereo.</summary>
        public const uint StereoEyesSideBySide = 2;

        /// <summary>The per-eye size; null: decided in-game.</summary>
        public Extent? Size { get; private set; }
        /// <summary>The setting: <c>auto</c> or <c>WxH</c>.</summary>
        public string Setting { get; private set; }
        /// <summary>The render scale as the layer reads it (the two-decimal text as a float).</summary>
        public float Scale { get; private set; }
        /// <summary>The runtime's limits (Route S), null when the probe gave none.</summary>
        public ViewLimits Limits { get; private set; }
        /// <summary>Why there is no size (auto only).</summary>
        public string Reason { get; private set; }
        /// <summary>A short note for the window when auto could not be decided before the launch; else null.</summary>
        public string Note { get; private set; }
        /// <summary>What the scale multiplied: the base asked for, or Auto when it could not be used (a fixed setting: Auto).</summary>
        public ResolutionBase Base { get; private set; }
        /// <summary>Why the base asked for could not be used and Auto is (for the log); null when it was used.</summary>
        public string BaseNote { get; private set; }
        /// <summary>The headset's native panel per eye, when the base is the native panel.</summary>
        public Extent? Panel { get; private set; }

        public bool IsAuto => Setting == LauncherSettings.RenderSizeAuto;

        /// <summary>The layer's <c>ETERNALVR_RENDER_SIZE</c> value.</summary>
        public string EnvironmentValue => Size?.ToString() ?? LauncherSettings.RenderSizeAuto;

        /// <summary>The same two-decimal text the launch plan gives the layer as <c>ETERNALVR_RENDER_SCALE</c>.</summary>
        public static string ScaleText(double renderScale) =>
            LauncherSettings.ClampRenderScale(renderScale).ToString("0.00", CultureInfo.InvariantCulture);

        /// <param name="setting">The normalised setting: <c>auto</c> or <c>WxH</c> (not <c>off</c>).</param>
        /// <param name="probe">The runtime probe; null when none was made.</param>
        public static RenderSizeChoice Decide(string setting, double renderScale, OpenXrProbeResult probe) =>
            Decide(setting, renderScale, probe, ResolutionBase.Auto, null);

        /// <param name="setting">The normalised setting: <c>auto</c> or <c>WxH</c> (not <c>off</c>).</param>
        /// <param name="probe">The runtime probe; null when none was made.</param>
        /// <param name="resolutionBase">What the scale multiplies with an auto setting.</param>
        /// <param name="panel">The headset's native panel per eye (<c>data\headsets.txt</c>); null when it is not known.</param>
        public static RenderSizeChoice Decide(string setting, double renderScale, OpenXrProbeResult probe, ResolutionBase resolutionBase, Extent? panel)
        {
            var c = new RenderSizeChoice
            {
                Setting = setting,
                Scale = RenderSize.ParseRenderScale(ScaleText(renderScale)) ?? 1.0f,
                Limits = probe != null && probe.Ok ? probe.Limits.WithEyesSideBySide(StereoEyesSideBySide) : null,
            };
            var request = RenderSize.ParseRenderSize(setting) ?? new RenderSizeRequest { Mode = RenderSizeMode.Auto };
            if (request.Mode == RenderSizeMode.Off) request.Mode = RenderSizeMode.Auto;
            request.Scale = c.Scale;
            c.Setting = request.Mode == RenderSizeMode.Fixed ? request.Fixed.ToString() : LauncherSettings.RenderSizeAuto;

            c.Base = request.Mode == RenderSizeMode.Auto ? resolutionBase : ResolutionBase.Auto;
            if (c.Base == ResolutionBase.Panel && (panel == null || panel.Value.IsEmpty))
            {
                c.BaseNote = "the headset's native panel is not known (not in data\\headsets.txt): Resolution uses Auto";
                c.Base = ResolutionBase.Auto;
            }
            else if (c.Base != ResolutionBase.Auto && c.Limits == null)
            {
                c.BaseNote = "Resolution's base (" + BaseText(c.Base) + ") needs the runtime's answer: the layer uses Auto";
                c.Base = ResolutionBase.Auto;
            }
            if (c.Base == ResolutionBase.Panel) c.Panel = panel;

            var size = c.Base == ResolutionBase.Ask ? Scaled(c.Limits.Recommended, c.Scale, c.Limits)
                : c.Base == ResolutionBase.Panel ? Scaled(panel.Value, c.Scale, c.Limits)
                : RenderSize.SelectSize(request, c.Limits);
            if (size == null)
            {
                c.Reason = probe == null ? "the runtime was not asked" : "the runtime did not answer: " + probe.Error;
                c.Note = probe != null && probe.HeadsetUnavailable
                    ? "Headset not detected; the render size is decided in-game."
                    : "The headset's render size could not be read before the launch; it is decided in-game.";
            }
            else if (size.Value.Width > RenderSize.MaxFixedSide || size.Value.Height > RenderSize.MaxFixedSide)
            {
                // The layer takes WxH only up to 8192 per side; it computes such a size itself.
                c.Reason = "the size " + size.Value + " is larger than a fixed size can be";
            }
            else
            {
                c.Size = size;
            }
            return c;
        }

        /// <summary>
        /// <paramref name="from"/> times <paramref name="scale"/> (rounded as the layer rounds), fitted to the runtime's limits
        /// and to the largest fixed size the layer takes (8192 per side), sides rounded to multiples of 8.
        /// </summary>
        private static Extent Scaled(Extent from, float scale, ViewLimits limits)
        {
            var wanted = new Extent((uint)Math.Round(from.Width * (double)scale, MidpointRounding.AwayFromZero),
                (uint)Math.Round(from.Height * (double)scale, MidpointRounding.AwayFromZero));
            uint Cap(uint v) => v == 0 ? RenderSize.MaxFixedSide : Math.Min(v, RenderSize.MaxFixedSide);
            var capped = new ViewLimits
            {
                Recommended = limits.Recommended,
                MaxImageRect = new Extent(Cap(limits.MaxImageRect.Width), Cap(limits.MaxImageRect.Height)),
                MaxSwapchain = limits.MaxSwapchain,
                EyesSideBySide = limits.EyesSideBySide,
            };
            return RenderSize.FitToLimits(wanted, capped);
        }

        /// <summary>The base in the log's words.</summary>
        public static string BaseText(ResolutionBase b) =>
            b == ResolutionBase.Ask ? "what the runtime asks for" : b == ResolutionBase.Panel ? "the native panel" : "Auto";

        /// <summary>For the launch plan in the launcher log.</summary>
        public string Describe()
        {
            var note = BaseNote == null ? string.Empty : "; " + BaseNote;
            if (Size == null) return "render size auto, decided in-game (" + Reason + ")" + note;
            var scale = ((double)Scale).ToString("0.00", CultureInfo.InvariantCulture);
            if (IsAuto && Base == ResolutionBase.Ask)
                return $"render size {Size} from what the runtime asks for, {Limits.Recommended}, times {scale}";
            if (IsAuto && Base == ResolutionBase.Panel)
                return $"render size {Size} from the headset's native panel {Panel} times {scale} (the runtime asks for {Limits.Recommended})";
            if (IsAuto) return $"render size {Size} from the runtime's recommendation {Limits.Recommended} (scale {scale})" + note;
            return $"render size {Size} from the setting {Setting}"
                + (Limits == null ? " (the runtime's limits unknown)" : Size.Value.ToString() == Setting ? string.Empty : " fitted to the runtime's limits");
        }
    }
}
