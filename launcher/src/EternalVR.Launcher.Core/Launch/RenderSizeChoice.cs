using System;
using System.Globalization;
using EternalVR.Launcher.Core.Settings;

namespace EternalVR.Launcher.Core.Launch
{
    /// <summary>
    /// The size a launch starts the game at (docs/rig-findings/render-size.md, section 7): each eye's in stereo, the one image's in
    /// mono (one eye's rule, no edge margin; docs/VR_HEAD_TRACKED.md "Render size"). With a size, the
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
        /// <summary>Head-tracked mono's ring holds its one image alone: the swapchain's width is not shared.</summary>
        public const uint MonoEyesSideBySide = 1;

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
        /// <summary>The edge margin's growth of an automatic size (1: none; a fixed setting never grows).</summary>
        public double Growth { get; private set; } = 1.0;
        /// <summary>
        /// The part of <see cref="Growth"/> the size keeps once fitted to the runtime's limits (<see cref="RenderSize.FittedGrowth"/>):
        /// <see cref="Growth"/> when the grown size fits, less when the limits cut it (the layer is then told the smaller margin, so
        /// the FOV is widened only as far as the size holds at the centre's density). <see cref="Growth"/> without a size.
        /// </summary>
        public double KeptGrowth { get; private set; } = 1.0;
        /// <summary>
        /// With DLSS on Auto: the share of each side DLSS renders (<see cref="RenderSize.DlssInputShare"/>), so the pixel budget holds
        /// what DLSS renders; 1 without DLSS, with DLAA, or when the base is not Auto (no budget).
        /// </summary>
        public double DlssInput { get; private set; } = 1.0;

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
        /// <param name="growth">The edge margin's factor (<see cref="LauncherSettings.EdgeMarginGrowth"/>): an auto setting's size
        /// grows by it on both sides, after the scale, whatever the base; a fixed one stays as it is.</param>
        /// <param name="dlssInput">With DLSS, the share of each side it renders (<see cref="DlssInputFor"/>): on Auto the pixel budget
        /// holds what DLSS renders, not the size it scales up to; 1 without DLSS.</param>
        /// <param name="eyesSideBySide">Images side by side in the layer's swapchain: <see cref="StereoEyesSideBySide"/> in stereo,
        /// <see cref="MonoEyesSideBySide"/> in mono (the one image both eyes see).</param>
        public static RenderSizeChoice Decide(string setting, double renderScale, OpenXrProbeResult probe, ResolutionBase resolutionBase, Extent? panel,
            double growth = 1.0, double dlssInput = 1.0, uint eyesSideBySide = StereoEyesSideBySide)
        {
            var c = new RenderSizeChoice
            {
                Setting = setting,
                Scale = RenderSize.ParseRenderScale(ScaleText(renderScale)) ?? 1.0f,
                Limits = probe != null && probe.Ok ? probe.Limits.WithEyesSideBySide(eyesSideBySide) : null,
            };
            var request = RenderSize.ParseRenderSize(setting) ?? new RenderSizeRequest { Mode = RenderSizeMode.Auto };
            if (request.Mode == RenderSizeMode.Off) request.Mode = RenderSizeMode.Auto;
            request.Scale = c.Scale;
            c.Setting = request.Mode == RenderSizeMode.Fixed ? request.Fixed.ToString() : LauncherSettings.RenderSizeAuto;
            c.Growth = request.Mode == RenderSizeMode.Auto && growth > 1.0 && !double.IsInfinity(growth) ? growth : 1.0;
            request.Growth = c.Growth;

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
            if (c.Base == ResolutionBase.Auto && request.Mode == RenderSizeMode.Auto && dlssInput > 0.0 && dlssInput < 1.0)
                c.DlssInput = dlssInput;
            request.DlssInput = c.DlssInput;

            var size = c.Base == ResolutionBase.Ask ? Scaled(c.Limits.Recommended, c.Scale, c.Growth, c.Limits)
                : c.Base == ResolutionBase.Panel ? Scaled(panel.Value, c.Scale, c.Growth, c.Limits)
                : RenderSize.SelectSize(request, c.Limits);
            c.KeptGrowth = c.Base == ResolutionBase.Ask ? RenderSize.FittedGrowth(Wanted(c.Limits.Recommended, c.Scale, c.Growth), c.Growth, Capped(c.Limits))
                : c.Base == ResolutionBase.Panel ? RenderSize.FittedGrowth(Wanted(panel.Value, c.Scale, c.Growth), c.Growth, Capped(c.Limits))
                : c.Limits != null && request.Mode == RenderSizeMode.Auto ? RenderSize.RequestGrowth(request, c.Limits, c.Growth)
                : c.Growth;
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
        /// <paramref name="from"/> times <paramref name="scale"/> and <paramref name="growth"/> (rounded as the layer rounds),
        /// fitted to the runtime's limits and to the largest fixed size the layer takes (8192 per side), sides rounded to
        /// multiples of 8.
        /// </summary>
        private static Extent Scaled(Extent from, float scale, double growth, ViewLimits limits) =>
            RenderSize.FitToLimits(Wanted(from, scale, growth), Capped(limits));

        // `from` times the scale and the growth, rounded as the layer rounds: the size before it is fitted.
        private static Extent Wanted(Extent from, float scale, double growth) =>
            new Extent((uint)Math.Round(from.Width * (double)scale * growth, MidpointRounding.AwayFromZero),
                (uint)Math.Round(from.Height * (double)scale * growth, MidpointRounding.AwayFromZero));

        // The runtime's limits and the largest fixed size the layer takes (8192 per side).
        private static ViewLimits Capped(ViewLimits limits)
        {
            uint Cap(uint v) => v == 0 ? RenderSize.MaxFixedSide : Math.Min(v, RenderSize.MaxFixedSide);
            return new ViewLimits
            {
                Recommended = limits.Recommended,
                MaxImageRect = new Extent(Cap(limits.MaxImageRect.Width), Cap(limits.MaxImageRect.Height)),
                MaxSwapchain = limits.MaxSwapchain,
                EyesSideBySide = limits.EyesSideBySide,
            };
        }

        /// <summary>
        /// The share of each side DLSS renders for these settings (<see cref="RenderSize.DlssInputShare"/>): stereo with Anti-aliasing
        /// DLSS, as the launch passes it to the layer, on an NVIDIA RTX card (<paramref name="rtxCard"/>,
        /// <see cref="Preflight.UntestedHardware.MainGpuIsNvidiaRtx"/>); 1 otherwise (TAA, Off, mono), for DLAA, and on any other or an
        /// unknown card: there DLSS cannot run, the layer falls back to TAA, and TAA at the larger size would be the costly way.
        /// </summary>
        public static double DlssInputFor(LauncherSettings s, bool rtxCard) =>
            rtxCard && s != null && s.Mode == VrMode.Stereo && s.AntiAliasing == AntiAliasingMode.Dlss ? RenderSize.DlssInputShare(s.Dlss) : 1.0;

        /// <summary>The base in the log's words.</summary>
        public static string BaseText(ResolutionBase b) =>
            b == ResolutionBase.Ask ? "what the runtime asks for" : b == ResolutionBase.Panel ? "the native panel" : "Auto";

        /// <summary>For the launch plan in the launcher log.</summary>
        public string Describe()
        {
            var note = BaseNote == null ? string.Empty : "; " + BaseNote;
            if (Size == null) return "render size auto, decided in-game (" + Reason + ")" + note;
            var scale = ((double)Scale).ToString("0.00", CultureInfo.InvariantCulture);
            if (Growth > 1.0) scale += " and the edge margin's " + Growth.ToString("0.00", CultureInfo.InvariantCulture);
            if (Size != null && KeptGrowth < Growth - 1e-9)
                scale += " (the runtime's limits keep x" + KeptGrowth.ToString("0.000", CultureInfo.InvariantCulture) + " of the margin)";
            if (IsAuto && Base == ResolutionBase.Ask)
                return $"render size {Size} from what the runtime asks for, {Limits.Recommended}, times {scale}";
            if (IsAuto && Base == ResolutionBase.Panel)
                return $"render size {Size} from the headset's native panel {Panel} times {scale} (the runtime asks for {Limits.Recommended})";
            if (IsAuto && DlssInput < 1.0)
                return $"render size {Size} from the runtime's recommendation {Limits.Recommended} (scale {scale}), the pixel budget on what DLSS renders (x"
                    + DlssInput.ToString("0.000", CultureInfo.InvariantCulture) + " per side)" + note;
            if (IsAuto) return $"render size {Size} from the runtime's recommendation {Limits.Recommended} (scale {scale})" + note;
            return $"render size {Size} from the setting {Setting}"
                + (Limits == null ? " (the runtime's limits unknown)" : Size.Value.ToString() == Setting ? string.Empty : " fitted to the runtime's limits");
        }
    }
}
