using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace EternalVR.Launcher.Core.Settings
{
    /// <summary>The stereo anti-aliasing: the game's TAA per eye, DLSS per eye (the forced <c>r_antialiasing</c> becomes 2), or
    /// none (the forced <c>r_antialiasing</c> becomes 0 and the layer turns every temporal effect off).</summary>
    public enum AntiAliasingMode { Taa, Dlss, Off }

    /// <summary>The DLSS quality the layer holds while DLSS runs (the layer's <c>ETERNALVR_STEREO_DLSS_QUALITY</c>, the game's
    /// <c>r_dlssQuality</c> 3 to 0): how large the image DLSS scales up from is. DLAA renders at the full size (the layer holds
    /// Quality and sets NGX's DLAA mode, with a newer DLSS only); the window lists it first (<see cref="DlssDll.QualityOrder"/>).</summary>
    public enum DlssQuality { Quality, Balanced, Performance, UltraPerformance, Dlaa }

    /// <summary>What Resolution's number multiplies (<c>resolution_base</c>): the size the runtime asks for fitted into the
    /// default pixel budget (Auto, the layer's own rule and the default), the size the runtime asks for, or the headset's native
    /// panel from <c>data\headsets.txt</c>.</summary>
    public enum ResolutionBase { Auto, Ask, Panel }

    /// <summary>The game's post-process sharpening in VR (<c>r_sharpening</c>, the layer's <c>ETERNALVR_SHARPENING</c>): the
    /// player's own setting from the game's menu, or held at 0, 1, 2 or 3.</summary>
    public enum SharpeningMode { Game, Off, Low, Medium, High }

    /// <summary>The game's screen-space reflections in VR (<c>r_SSR</c>, the layer's <c>ETERNALVR_STEREO_SSR</c>): the player's
    /// own Reflections setting from the game's menu (off at Low), or off.</summary>
    public enum ReflectionsMode { Game, Off }

    /// <summary>How much wider than its view each eye renders in stereo (<c>edge_margin</c>, the layer's <c>ETERNALVR_EDGE_MARGIN</c>,
    /// docs/VR_STEREO.md "Edge margin"), so a fast head turn shows picture at the edges instead of black while the runtime turns
    /// the last frame to your head. On a Quest 3 Small, Medium and Large add about 2.5, 5 and 7 degrees past each edge, for 21,
    /// 44 and 69% more pixels (an automatic Resolution grows with it).</summary>
    public enum EdgeMarginMode { Off, Small, Medium, Large }

    /// <summary>
    /// The Play tab's picture settings in launcher.ini: <c>resolution_base = auto|ask|panel</c>, <c>anti_aliasing</c>,
    /// <c>dlss_quality</c>, <c>dlss_version = newest|game|file</c>, <c>dlss_dll_path</c>, <c>dlss_preset</c>, <c>sharpening =
    /// game|off|low|medium|high</c> and <c>screen_reflections = game|off</c>. A file without <c>resolution_base</c> (launcher
    /// 0.1.11 and older) is Auto with its <c>render_scale</c> as it was: Auto is exactly the size those launchers rendered, so
    /// updating changes no one's size.
    /// </summary>
    public sealed partial class LauncherSettings
    {
        public AntiAliasingMode AntiAliasing { get; set; } = AntiAliasingMode.Taa;
        public DlssQuality Dlss { get; set; } = DlssQuality.Quality;
        /// <summary>NVIDIA's newest DLSS (downloaded by the launcher), the game's own, or the player's file at <see cref="DlssDllPath"/>.</summary>
        public DlssDllChoice DlssDll { get; set; } = DlssDllChoice.Newest;
        /// <summary>The player's nvngx_dlss.dll, kept where they chose it (this machine's, like the folders).</summary>
        public string DlssDllPath { get; set; } = string.Empty;
        /// <summary>The DLSS render preset with a newer DLSS (<see cref="Settings.DlssDll.PresetValues"/>).</summary>
        public string DlssPreset { get; set; } = Settings.DlssDll.RecommendedPreset;
        /// <summary>The game's sharpening in VR; the player's own setting by default.</summary>
        public SharpeningMode Sharpening { get; set; } = SharpeningMode.Game;
        /// <summary>The game's screen-space reflections in VR; the player's own Reflections setting by default.</summary>
        public ReflectionsMode Reflections { get; set; } = ReflectionsMode.Game;
        /// <summary>What Resolution's number multiplies; Auto by default (<see cref="ResolutionBase"/>).</summary>
        public ResolutionBase ResolutionBase { get; set; } = ResolutionBase.Auto;
        /// <summary>The edge margin in stereo (<see cref="EdgeMarginMode"/>); <see cref="DefaultEdgeMargin"/> by default.</summary>
        public EdgeMarginMode EdgeMargin { get; set; } = DefaultEdgeMargin;

        /// <summary>The edge margin a fresh data folder (and a settings file without <c>edge_margin</c>) has.</summary>
        public const EdgeMarginMode DefaultEdgeMargin = EdgeMarginMode.Off;

        private void ReadPicture(IDictionary<string, string> map)
        {
            if (map.TryGetValue("resolution_base", out var rb))
                ResolutionBase = Pick(rb, ResolutionBase.Auto, ("ask", ResolutionBase.Ask), ("panel", ResolutionBase.Panel));
            if (map.TryGetValue("anti_aliasing", out var aa)) AntiAliasing = Pick(aa, AntiAliasingMode.Taa, ("dlss", AntiAliasingMode.Dlss), ("off", AntiAliasingMode.Off));
            if (map.TryGetValue("dlss_quality", out var dq))
                Dlss = Pick(dq, DlssQuality.Quality, ("balanced", DlssQuality.Balanced), ("performance", DlssQuality.Performance),
                    ("ultra_performance", DlssQuality.UltraPerformance), ("dlaa", DlssQuality.Dlaa));
            if (map.TryGetValue("dlss_dll_path", out var path)) DlssDllPath = path;
            if (map.TryGetValue("dlss_preset", out var preset)) DlssPreset = Settings.DlssDll.NormalisePreset(preset);
            if (map.TryGetValue("dlss_version", out var version))
                DlssDll = Pick(version, DlssDllChoice.Newest, ("game", DlssDllChoice.Game), ("file", DlssDllChoice.File));
            else if (map.ContainsKey("dlss_dll"))
            {
                // A file from before the DLSS group (0.1.9 and older) always wrote dlss_dll, game by default: the newest
                // DLSS takes the game's place and a preset left at the DLL's default becomes the recommended one. A file
                // the player chose stays theirs.
                if (Pick(map["dlss_dll"], false, ("file", true))) DlssDll = DlssDllChoice.File;
                if (DlssPreset == Settings.DlssDll.AutomaticPreset) DlssPreset = Settings.DlssDll.RecommendedPreset;
            }
            if (map.TryGetValue("sharpening", out var sh))
                Sharpening = Pick(sh, SharpeningMode.Game, ("off", SharpeningMode.Off), ("low", SharpeningMode.Low),
                    ("medium", SharpeningMode.Medium), ("high", SharpeningMode.High));
            if (map.TryGetValue("screen_reflections", out var sr)) Reflections = Pick(sr, ReflectionsMode.Game, ("off", ReflectionsMode.Off));
            if (map.TryGetValue("edge_margin", out var em))
                EdgeMargin = Pick(em, DefaultEdgeMargin, ("off", EdgeMarginMode.Off), ("none", EdgeMarginMode.Off), ("small", EdgeMarginMode.Small),
                    ("medium", EdgeMarginMode.Medium), ("large", EdgeMarginMode.Large));
        }

        private void WritePicture(StringBuilder sb)
        {
            sb.AppendLine("resolution_base = " + ResolutionBaseName(ResolutionBase));
            sb.AppendLine("anti_aliasing = " + (AntiAliasing == AntiAliasingMode.Dlss ? "dlss" : AntiAliasing == AntiAliasingMode.Off ? "off" : "taa"));
            sb.AppendLine("dlss_quality = " + DlssQualityName(Dlss));
            sb.AppendLine("dlss_version = " + DlssVersionName(DlssDll));
            sb.AppendLine("dlss_dll_path = " + OneLine(DlssDllPath));
            sb.AppendLine("dlss_preset = " + Settings.DlssDll.NormalisePreset(DlssPreset));
            sb.AppendLine("sharpening = " + SharpeningName(Sharpening));
            sb.AppendLine("screen_reflections = " + ReflectionsName(Reflections));
            sb.AppendLine("edge_margin = " + EdgeMarginName(EdgeMargin));
        }

        /// <summary>The settings file's <c>edge_margin</c> value and the layer's <c>ETERNALVR_EDGE_MARGIN</c>: off, small, medium or large.</summary>
        public static string EdgeMarginName(EdgeMarginMode m) =>
            m == EdgeMarginMode.Small ? "small" : m == EdgeMarginMode.Medium ? "medium" : m == EdgeMarginMode.Large ? "large" : "off";

        /// <summary>
        /// The layer's <c>ETERNALVR_EDGE_MARGIN</c> for the setting and the margin the launch's render size keeps
        /// (<see cref="Launch.RenderSizeChoice.KeptGrowth"/>; null: none worked out here). The setting's name, unless the runtime's
        /// limits cut the grown size: then the margin it keeps as a whole percentage, rounded down so the view never gets less
        /// detail than without the margin, or <c>off</c> below 1%.
        /// </summary>
        public static string EdgeMarginValue(EdgeMarginMode m, double? kept)
        {
            double growth = EdgeMarginGrowth(m);
            // A kept factor an ulp or so under the growth (rounding) is the whole margin.
            if (m == EdgeMarginMode.Off || kept == null || !(kept.Value < growth - 1e-9)) return EdgeMarginName(m);
            int percent = (int)Math.Floor((kept.Value - 1.0) * 100.0 + 1e-9);
            return percent < 1 ? "off" : percent.ToString(CultureInfo.InvariantCulture);
        }

        /// <summary>
        /// The factor an automatic render size grows by on both sides, as the layer's (<c>src/features/render_size/edge_margin.hpp</c>:
        /// its float constants, taken as doubles): 1 when off.
        /// </summary>
        public static double EdgeMarginGrowth(EdgeMarginMode m) =>
            m == EdgeMarginMode.Small ? (double)1.10f : m == EdgeMarginMode.Medium ? (double)1.20f : m == EdgeMarginMode.Large ? (double)1.30f : 1.0;

        /// <summary>The layer's <c>ETERNALVR_STEREO_DLSS_QUALITY</c> value (and the settings file's).</summary>
        public static string DlssQualityName(DlssQuality q) =>
            q == DlssQuality.Balanced ? "balanced" : q == DlssQuality.Performance ? "performance"
            : q == DlssQuality.UltraPerformance ? "ultra_performance" : q == DlssQuality.Dlaa ? "dlaa" : "quality";

        /// <summary>The settings file's <c>resolution_base</c> value: auto, ask or panel.</summary>
        public static string ResolutionBaseName(ResolutionBase b) => b == ResolutionBase.Ask ? "ask" : b == ResolutionBase.Panel ? "panel" : "auto";

        /// <summary>The settings file's <c>dlss_version</c> value: newest, game or file.</summary>
        public static string DlssVersionName(DlssDllChoice c) =>
            c == DlssDllChoice.Game ? "game" : c == DlssDllChoice.File ? "file" : "newest";

        /// <summary>The settings file's <c>sharpening</c> value.</summary>
        public static string SharpeningName(SharpeningMode m) =>
            m == SharpeningMode.Off ? "off" : m == SharpeningMode.Low ? "low" : m == SharpeningMode.Medium ? "medium"
            : m == SharpeningMode.High ? "high" : "game";

        /// <summary>The settings file's <c>screen_reflections</c> value: game or off.</summary>
        public static string ReflectionsName(ReflectionsMode m) => m == ReflectionsMode.Off ? "off" : "game";

        /// <summary>The <c>r_sharpening</c> the layer holds (<c>ETERNALVR_SHARPENING</c>); null for the player's own setting.</summary>
        public static string SharpeningValue(SharpeningMode m) =>
            m == SharpeningMode.Off ? "0" : m == SharpeningMode.Low ? "1" : m == SharpeningMode.Medium ? "2" : m == SharpeningMode.High ? "3" : null;
    }
}
