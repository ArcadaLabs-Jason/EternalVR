using System;
using System.Globalization;

namespace EternalVR.Launcher.Core.Launch
{
    /// <summary>A size in pixels (<c>evr::render_size::Extent</c>).</summary>
    public readonly struct Extent : IEquatable<Extent>
    {
        public Extent(uint width, uint height)
        {
            Width = width;
            Height = height;
        }

        public uint Width { get; }
        public uint Height { get; }
        public bool IsEmpty => Width == 0 || Height == 0;

        public bool Equals(Extent other) => Width == other.Width && Height == other.Height;
        public override bool Equals(object obj) => obj is Extent e && Equals(e);
        public override int GetHashCode() => (int)(Width * 31 + Height);
        public static bool operator ==(Extent a, Extent b) => a.Equals(b);
        public static bool operator !=(Extent a, Extent b) => !a.Equals(b);
        public override string ToString() => Width.ToString(CultureInfo.InvariantCulture) + "x" + Height.ToString(CultureInfo.InvariantCulture);
    }

    /// <summary>What the runtime allows (<c>evr::render_size::ViewLimits</c>).</summary>
    public sealed class ViewLimits
    {
        /// <summary>The largest recommended image rect over the views.</summary>
        public Extent Recommended { get; set; }
        /// <summary>The smallest maximum image rect over the views (0: no limit).</summary>
        public Extent MaxImageRect { get; set; }
        /// <summary>The system's maximum swapchain image size (0: no limit).</summary>
        public Extent MaxSwapchain { get; set; }
        /// <summary>Eye images side by side in one swapchain image (Route S: 2): the width limit is shared.</summary>
        public uint EyesSideBySide { get; set; } = 1;

        public ViewLimits WithEyesSideBySide(uint eyes) =>
            new ViewLimits { Recommended = Recommended, MaxImageRect = MaxImageRect, MaxSwapchain = MaxSwapchain, EyesSideBySide = eyes };
    }

    public enum RenderSizeMode { Off, Auto, Fixed }

    /// <summary>A parsed <c>ETERNALVR_RENDER_SIZE</c> (<c>evr::render_size::Request</c>).</summary>
    public sealed class RenderSizeRequest
    {
        public RenderSizeMode Mode { get; set; } = RenderSizeMode.Off;
        public Extent Fixed { get; set; }
        public float Scale { get; set; } = 1.0f;
    }

    /// <summary>
    /// The layer's render size rules, ported line for line from <c>src/features/render_size/render_size.cpp</c> so the
    /// launcher computes exactly the size the layer would (docs/rig-findings/render-size.md, sections 5 and 7). The
    /// arithmetic is the C++'s: doubles for the scaling, <c>std::round</c> and <c>std::lround</c> (half away from
    /// zero), the scale as a float. <c>RenderSizeTests</c> repeat the C++ tests' vectors.
    /// </summary>
    public static class RenderSize
    {
        public const uint MinSide = 256;
        public const uint MaxFixedSide = 8192;
        /// <summary>Vulkan's usual maxImageDimension2D.</summary>
        public const uint MaxSide = 16384;
        /// <summary>Sides are multiples of this.</summary>
        public const uint Align = 8;
        public const float MinScale = 0.5f;
        public const float MaxScale = 2.0f;
        /// <summary>The default pixel budget per eye: 2064 x 2208 (4.56 Mpix); the recommendation is scaled down into it, never up.</summary>
        public const ulong DefaultPixelBudget = 2064UL * 2208UL;

        /// <summary>Unset or empty, <c>off</c>, <c>0</c>: Off. <c>auto</c>. <c>WxH</c> with each side 256..8192. Null for anything else.</summary>
        public static RenderSizeRequest ParseRenderSize(string text)
        {
            var t = (text ?? string.Empty).Trim().ToLowerInvariant();
            if (t.Length == 0 || t == "off" || t == "0") return new RenderSizeRequest();
            if (t == "auto") return new RenderSizeRequest { Mode = RenderSizeMode.Auto };
            int x = t.IndexOf('x');
            if (x < 0) return null;
            var w = ParseUnsigned(t.Substring(0, x));
            var h = ParseUnsigned(t.Substring(x + 1));
            if (w == null || h == null || w < MinSide || h < MinSide || w > MaxFixedSide || h > MaxFixedSide) return null;
            return new RenderSizeRequest { Mode = RenderSizeMode.Fixed, Fixed = new Extent(w.Value, h.Value) };
        }

        /// <summary>Empty or <c>auto</c>: 1.0; digits with at most one decimal point, 0.5..2.0. Null for anything else.</summary>
        public static float? ParseRenderScale(string text)
        {
            var t = (text ?? string.Empty).Trim().ToLowerInvariant();
            if (t.Length == 0 || t == "auto") return 1.0f;
            int points = 0;
            foreach (char c in t)
            {
                if (c == '.') points++;
                else if (c < '0' || c > '9') return null;
            }
            if (points > 1 || t == ".") return null;
            // float.Parse rounds the decimal to the nearest float, as wcstof does.
            float value = float.Parse(t.StartsWith(".", StringComparison.Ordinal) ? "0" + t : t, NumberStyles.AllowDecimalPoint, CultureInfo.InvariantCulture);
            if (float.IsNaN(value) || float.IsInfinity(value) || value < MinScale || value > MaxScale) return null;
            return value;
        }

        /// <summary>
        /// <paramref name="size"/> scaled down uniformly, if needed, to fit the runtime's limits (and 16384), then
        /// rounded to multiples of 8 (at least 256 per side).
        /// </summary>
        public static Extent FitToLimits(Extent size, ViewLimits limits)
        {
            if (size.IsEmpty) return default;
            limits = limits ?? new ViewLimits();
            uint eyes = Math.Max(1u, limits.EyesSideBySide);
            uint maxWidth = Math.Min(LimitOf(limits.MaxImageRect.Width), LimitOf(limits.MaxSwapchain.Width) / eyes);
            uint maxHeight = Math.Min(LimitOf(limits.MaxImageRect.Height), LimitOf(limits.MaxSwapchain.Height));
            maxWidth = Math.Max(maxWidth, MinSide);
            maxHeight = Math.Max(maxHeight, MinSide);
            double w = size.Width;
            double h = size.Height;
            double down = Math.Min(1.0, Math.Min((double)maxWidth / w, (double)maxHeight / h));
            return new Extent(AlignSide(w * down, maxWidth), AlignSide(h * down, maxHeight));
        }

        /// <summary>
        /// Auto: the recommendation scaled uniformly into <paramref name="budget"/> pixels (never up), times
        /// <paramref name="scale"/>, then fitted. Null when the recommendation is empty or the scale is not a positive
        /// finite number.
        /// </summary>
        public static Extent? AutoSize(ViewLimits limits, float scale, ulong budget = DefaultPixelBudget)
        {
            if (limits == null || limits.Recommended.IsEmpty || float.IsNaN(scale) || float.IsInfinity(scale) || scale <= 0.0f) return null;
            double w = limits.Recommended.Width;
            double h = limits.Recommended.Height;
            double pixels = w * h;
            double fit = budget > 0 && pixels > budget ? Math.Sqrt(budget / pixels) : 1.0;
            double factor = fit * scale;
            var wanted = new Extent((uint)LRound(w * factor), (uint)LRound(h * factor));
            return FitToLimits(wanted, limits);
        }

        /// <summary>Off: null; Fixed: its size (fitted when the limits are known); Auto: needs the limits (null without them).</summary>
        public static Extent? SelectSize(RenderSizeRequest request, ViewLimits limits)
        {
            switch (request.Mode)
            {
                case RenderSizeMode.Fixed: return FitToLimits(request.Fixed, limits ?? new ViewLimits());
                case RenderSizeMode.Auto: return limits == null ? (Extent?)null : AutoSize(limits, request.Scale);
                default: return null;
            }
        }

        /// <summary>For the log: "off", "auto x1.00" or "WxH".</summary>
        public static string Describe(RenderSizeRequest request)
        {
            switch (request.Mode)
            {
                case RenderSizeMode.Auto: return "auto x" + ((double)request.Scale).ToString("0.00", CultureInfo.InvariantCulture);
                case RenderSizeMode.Fixed: return request.Fixed.ToString();
                default: return "off";
            }
        }

        // The side rounded to the nearest multiple of 8, within 256..limit (limit itself rounded down).
        private static uint AlignSide(double side, uint limit)
        {
            uint top = Math.Max(MinSide, limit / Align * Align);
            double aligned = Math.Round(side / Align, MidpointRounding.AwayFromZero) * Align;
            return (uint)Math.Max((double)MinSide, Math.Min((double)top, aligned));
        }

        private static uint LimitOf(uint value) => value == 0 ? MaxSide : Math.Min(value, MaxSide);

        // std::lround for the non-negative values used here.
        private static long LRound(double v) => (long)Math.Round(v, MidpointRounding.AwayFromZero);

        // A decimal integer that fills the text (digits only, at most 6), or null.
        private static uint? ParseUnsigned(string text)
        {
            if (text.Length == 0 || text.Length > 6) return null;
            uint value = 0;
            foreach (char c in text)
            {
                if (c < '0' || c > '9') return null;
                value = value * 10 + (uint)(c - '0');
            }
            return value;
        }
    }
}
