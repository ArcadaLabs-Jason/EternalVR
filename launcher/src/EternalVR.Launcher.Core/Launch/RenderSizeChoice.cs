using System.Globalization;
using EternalVR.Launcher.Core.Settings;

namespace EternalVR.Launcher.Core.Launch
{
    /// <summary>
    /// The size a stereo launch starts the game at (docs/rig-findings/render-size.md, section 7). With a size, the
    /// game gets it as <c>ETERNALVR_RENDER_SIZE=WxH</c> and <c>+r_windowWidth/Height</c>, so its first swapchain is
    /// already at the final size and nothing is resized mid-session. Without one (auto and no runtime answer), the
    /// layer decides in-game (<c>ETERNALVR_RENDER_SIZE=auto</c>) and the game starts at the mirror's size.
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

        public bool IsAuto => Setting == LauncherSettings.RenderSizeAuto;

        /// <summary>The layer's <c>ETERNALVR_RENDER_SIZE</c> value.</summary>
        public string EnvironmentValue => Size?.ToString() ?? LauncherSettings.RenderSizeAuto;

        /// <summary>The same two-decimal text the launch plan gives the layer as <c>ETERNALVR_RENDER_SCALE</c>.</summary>
        public static string ScaleText(double renderScale) =>
            LauncherSettings.ClampRenderScale(renderScale).ToString("0.00", CultureInfo.InvariantCulture);

        /// <param name="setting">The normalised setting: <c>auto</c> or <c>WxH</c> (not <c>off</c>).</param>
        /// <param name="probe">The runtime probe; null when none was made.</param>
        public static RenderSizeChoice Decide(string setting, double renderScale, OpenXrProbeResult probe)
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

            var size = RenderSize.SelectSize(request, c.Limits);
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

        /// <summary>For the launch plan in the launcher log.</summary>
        public string Describe()
        {
            if (Size == null) return "render size auto, decided in-game (" + Reason + ")";
            var scale = ((double)Scale).ToString("0.00", CultureInfo.InvariantCulture);
            if (IsAuto) return $"render size {Size} from the runtime's recommendation {Limits.Recommended} (scale {scale})";
            return $"render size {Size} from the setting {Setting}"
                + (Limits == null ? " (the runtime's limits unknown)" : Size.Value.ToString() == Setting ? string.Empty : " fitted to the runtime's limits");
        }
    }
}
