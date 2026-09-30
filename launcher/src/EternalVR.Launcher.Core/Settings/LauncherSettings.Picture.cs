using System.Collections.Generic;
using System.Text;

namespace EternalVR.Launcher.Core.Settings
{
    /// <summary>The stereo anti-aliasing: the game's TAA per eye, DLSS per eye (the forced <c>r_antialiasing</c> becomes 2), or
    /// none (the forced <c>r_antialiasing</c> becomes 0 and the layer turns every temporal effect off).</summary>
    public enum AntiAliasingMode { Taa, Dlss, Off }

    /// <summary>The DLSS quality the layer holds while DLSS runs (the layer's <c>ETERNALVR_STEREO_DLSS_QUALITY</c>, the game's
    /// <c>r_dlssQuality</c> 3 to 0): how large the image DLSS scales up from is.</summary>
    public enum DlssQuality { Quality, Balanced, Performance, UltraPerformance }

    /// <summary>The game's post-process sharpening in VR (<c>r_sharpening</c>, the layer's <c>ETERNALVR_SHARPENING</c>): the
    /// player's own setting from the game's menu, or held at 0, 1, 2 or 3.</summary>
    public enum SharpeningMode { Game, Off, Low, Medium, High }

    /// <summary>
    /// The Play tab's picture settings in launcher.ini: <c>anti_aliasing</c>, <c>dlss_quality</c>, <c>dlss_version =
    /// newest|game|file</c>, <c>dlss_dll_path</c>, <c>dlss_preset</c> and <c>sharpening = game|off|low|medium|high</c>.
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

        private void ReadPicture(IDictionary<string, string> map)
        {
            if (map.TryGetValue("anti_aliasing", out var aa)) AntiAliasing = Pick(aa, AntiAliasingMode.Taa, ("dlss", AntiAliasingMode.Dlss), ("off", AntiAliasingMode.Off));
            if (map.TryGetValue("dlss_quality", out var dq))
                Dlss = Pick(dq, DlssQuality.Quality, ("balanced", DlssQuality.Balanced), ("performance", DlssQuality.Performance),
                    ("ultra_performance", DlssQuality.UltraPerformance));
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
        }

        private void WritePicture(StringBuilder sb)
        {
            sb.AppendLine("anti_aliasing = " + (AntiAliasing == AntiAliasingMode.Dlss ? "dlss" : AntiAliasing == AntiAliasingMode.Off ? "off" : "taa"));
            sb.AppendLine("dlss_quality = " + DlssQualityName(Dlss));
            sb.AppendLine("dlss_version = " + DlssVersionName(DlssDll));
            sb.AppendLine("dlss_dll_path = " + OneLine(DlssDllPath));
            sb.AppendLine("dlss_preset = " + Settings.DlssDll.NormalisePreset(DlssPreset));
            sb.AppendLine("sharpening = " + SharpeningName(Sharpening));
        }

        /// <summary>The layer's <c>ETERNALVR_STEREO_DLSS_QUALITY</c> value (and the settings file's).</summary>
        public static string DlssQualityName(DlssQuality q) =>
            q == DlssQuality.Balanced ? "balanced" : q == DlssQuality.Performance ? "performance"
            : q == DlssQuality.UltraPerformance ? "ultra_performance" : "quality";

        /// <summary>The settings file's <c>dlss_version</c> value: newest, game or file.</summary>
        public static string DlssVersionName(DlssDllChoice c) =>
            c == DlssDllChoice.Game ? "game" : c == DlssDllChoice.File ? "file" : "newest";

        /// <summary>The settings file's <c>sharpening</c> value.</summary>
        public static string SharpeningName(SharpeningMode m) =>
            m == SharpeningMode.Off ? "off" : m == SharpeningMode.Low ? "low" : m == SharpeningMode.Medium ? "medium"
            : m == SharpeningMode.High ? "high" : "game";

        /// <summary>The <c>r_sharpening</c> the layer holds (<c>ETERNALVR_SHARPENING</c>); null for the player's own setting.</summary>
        public static string SharpeningValue(SharpeningMode m) =>
            m == SharpeningMode.Off ? "0" : m == SharpeningMode.Low ? "1" : m == SharpeningMode.Medium ? "2" : m == SharpeningMode.High ? "3" : null;
    }
}
