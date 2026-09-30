using System;
using System.Diagnostics;
using System.IO;

namespace EternalVR.Launcher.Core.Settings
{
    /// <summary>Which nvngx_dlss.dll DLSS runs with: the game's own (2.3, in its folder), a newer one the player chose, or
    /// NVIDIA's newest the launcher downloads (data\dlss-downloads.txt).</summary>
    public enum DlssDllChoice { Game, File, Newest }

    /// <summary>
    /// A newer DLSS DLL (docs/rig-findings/dlss-dll.md): the layer's <c>ETERNALVR_DLSS_DLL</c> and <c>ETERNALVR_DLSS_PRESET</c>.
    /// The file stays where it is (the player's, or the launcher's download); nothing is copied into the game folder. The
    /// layer checks the file again and falls back to the game's DLL when it cannot use it.
    /// </summary>
    public static class DlssDll
    {
        public const string FileName = "nvngx_dlss.dll";

        /// <summary>The DLL the game ships.</summary>
        public static readonly Version GameVersion = new Version(2, 3, 0, 0);

        /// <summary>The first DLSS with render presets.</summary>
        public static readonly Version FirstPresetVersion = new Version(3, 1, 0, 0);

        /// <summary>The preset choices (the layer's values), in the window's order: NVIDIA's pick, then the transformer model's letters.</summary>
        public static readonly string[] PresetValues = { "default", "K", "J", "M", "L", "F" };

        /// <summary>NVIDIA's own pick for each quality (the DLL's default).</summary>
        public const string AutomaticPreset = "default";

        /// <summary>The default: the transformer model at every quality, the sharpest (docs/release/TROUBLESHOOTING.md).</summary>
        public const string RecommendedPreset = "K";

        public static readonly string[] PresetNames =
        {
            "Automatic (NVIDIA's pick)", "K (recommended)", "J (transformer)", "M (transformer, Performance)", "L (transformer, Ultra Perf.)",
            "F (older model)",
        };

        /// <summary>The Version list's order in the window: the newest first (recommended), then the game's, then a file.</summary>
        public static readonly DlssDllChoice[] VersionOrder = { DlssDllChoice.Newest, DlssDllChoice.Game, DlssDllChoice.File };

        /// <summary>One of <see cref="PresetValues"/>; the default for anything else.</summary>
        public static string NormalisePreset(string text)
        {
            var t = (text ?? string.Empty).Trim();
            foreach (var v in PresetValues)
                if (string.Equals(v, t, StringComparison.OrdinalIgnoreCase)) return v;
            return PresetValues[0];
        }

        public static int PresetIndex(string text) => Array.IndexOf(PresetValues, NormalisePreset(text));

        /// <summary>
        /// The DLL the launch passes to the layer in stereo with DLSS: NVIDIA's newest when it is downloaded
        /// (<paramref name="newestPath"/>, null when it is not), or the player's file; null for the game's own.
        /// </summary>
        public static string PathFor(LauncherSettings s, string newestPath)
        {
            if (s.Mode != VrMode.Stereo || s.AntiAliasing != AntiAliasingMode.Dlss) return null;
            if (s.DlssDll == DlssDllChoice.Newest) return string.IsNullOrWhiteSpace(newestPath) ? null : newestPath;
            return s.DlssDll == DlssDllChoice.File && !string.IsNullOrWhiteSpace(s.DlssDllPath) ? s.DlssDllPath.Trim() : null;
        }

        /// <summary>The DLSS qualities' names, in <see cref="DlssQuality"/> order.</summary>
        public static readonly string[] QualityNames = { "Quality", "Balanced", "Performance", "Ultra Performance" };

        /// <summary>
        /// The Play tab's "In the headset" line: the DLSS that runs, its preset and its quality. <paramref name="newest"/> is
        /// NVIDIA's newest listed DLSS (null when none is listed), <paramref name="newestReady"/> whether it is downloaded, and
        /// <paramref name="file"/> what the launcher found about the player's file (null when none is chosen).
        /// </summary>
        public static string WhatRuns(LauncherSettings s, DlssRelease newest, bool newestReady, Check file)
        {
            var quality = QualityNames[Math.Max(0, Math.Min(QualityNames.Length - 1, (int)s.Dlss))];
            var game = "The game's DLSS 2.3, " + quality + ", both eyes";
            if (s.DlssDll == DlssDllChoice.Newest)
            {
                if (newest == null) return game;
                var version = newest.Version.ToString(3);
                return newestReady ? $"DLSS {version}, {PresetText(s.DlssPreset)}, {quality}, both eyes"
                    : $"{game}, until you download DLSS {version}";
            }
            if (s.DlssDll != DlssDllChoice.File) return game;
            if (file == null || !file.Ok) return game + ": your file cannot be used";
            var own = "DLSS " + file.Version.ToString(3) + " from your file";
            return file.HasPresets ? $"{own}, {PresetText(s.DlssPreset)}, {quality}, both eyes" : $"{own}, {quality}, both eyes";
        }

        private static string PresetText(string preset)
        {
            var p = NormalisePreset(preset);
            return p == AutomaticPreset ? "NVIDIA's preset" : "preset " + p;
        }

        /// <summary>What the launcher found about a chosen file.</summary>
        public sealed class Check
        {
            /// <summary>Null when the file looks usable, else why not (one sentence).</summary>
            public string Problem { get; set; }
            public Version Version { get; set; }
            public bool Ok => Problem == null;
            /// <summary>True for a usable DLL that knows render presets (DLSS 3.1 or later).</summary>
            public bool HasPresets => Ok && Version != null && Version >= FirstPresetVersion;
        }

        /// <summary>The file's name, the start of the file (a 64-bit DLL) and its version information (NVIDIA's).</summary>
        public static Check Inspect(string path)
        {
            if (string.IsNullOrWhiteSpace(path)) return new Check { Problem = "No file is chosen." };
            if (!string.Equals(Path.GetFileName(path), FileName, StringComparison.OrdinalIgnoreCase))
                return new Check { Problem = "The file must be named " + FileName + "." };
            if (!File.Exists(path)) return new Check { Problem = "The file is not there any more." };
            byte[] head;
            try
            {
                using (var f = File.OpenRead(path))
                {
                    head = new byte[4096];
                    int n = f.Read(head, 0, head.Length);
                    Array.Resize(ref head, n);
                }
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException)
            {
                return new Check { Problem = "The file cannot be read: " + e.Message };
            }
            var pe = PeProblem(head);
            if (pe != null) return new Check { Problem = pe };
            FileVersionInfo info;
            try { info = FileVersionInfo.GetVersionInfo(path); }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException) { info = null; }
            if (info == null || (info.FileMajorPart == 0 && info.FileMinorPart == 0 && info.FileBuildPart == 0 && info.FilePrivatePart == 0))
                return new Check { Problem = "The file has no version information: it is not NVIDIA's DLSS DLL." };
            if ((info.CompanyName ?? string.Empty).IndexOf("NVIDIA", StringComparison.OrdinalIgnoreCase) < 0)
                return new Check { Problem = "The file is not NVIDIA's DLSS DLL." };
            return new Check { Version = new Version(info.FileMajorPart, info.FileMinorPart, info.FileBuildPart, info.FilePrivatePart) };
        }

        /// <summary>Null for the start of an x86-64 DLL, else why not.</summary>
        public static string PeProblem(byte[] head)
        {
            const string notDll = "The file is not a Windows DLL.";
            if (head == null || head.Length < 0x40 || head[0] != 'M' || head[1] != 'Z') return notDll;
            int nt = BitConverter.ToInt32(head, 0x3C);
            if (nt < 0 || nt > head.Length - 24) return notDll;
            if (head[nt] != 'P' || head[nt + 1] != 'E' || head[nt + 2] != 0 || head[nt + 3] != 0) return notDll;
            if (BitConverter.ToUInt16(head, nt + 4) != 0x8664) return "The file is not a 64-bit DLL.";
            if ((BitConverter.ToUInt16(head, nt + 22) & 0x2000) == 0) return notDll;
            return null;
        }

        /// <summary>The line under the choice: the version found, or the problem.</summary>
        public static string Describe(Check c) =>
            c.Ok ? "Version " + c.Version + (c.Version <= GameVersion ? " (not newer than the game's)" : string.Empty) : c.Problem;
    }
}
