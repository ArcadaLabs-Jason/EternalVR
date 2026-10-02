using System;
using System.Drawing;
using System.IO;
using System.Windows.Forms;
using EternalVR.Launcher.Core.Settings;

namespace EternalVR.Launcher
{
    /// <summary>
    /// The Play tab's DLSS group: the quality, the version (NVIDIA's newest, downloaded on request, the game's own, or a file of
    /// the player's), the preset, and the line saying what will run in the headset.
    /// </summary>
    public sealed partial class MainForm
    {
        private readonly ComboBox dlssQuality = Choices(Setting.DlssQuality);
        private readonly ComboBox dlssDll = Choices(Setting.DlssVersion);
        /// <summary>Downloads NVIDIA's newest pinned DLL (data\dlss-downloads.txt); shown while it is chosen and not downloaded.</summary>
        private readonly Button dlssDownload = new Button { Text = "Download...", Width = 150, Height = ButtonHeight };
        private readonly Button dlssChoose = new Button { Text = "Choose file...", Width = 110, Height = ButtonHeight };
        private readonly ComboBox dlssPreset = Choices(Setting.DlssPreset);
        /// <summary>What will run in the headset (<see cref="DlssDll.WhatRuns"/>); a chosen file's path and version in its tooltip.</summary>
        private readonly Label dlssRuns = new Label { AutoSize = true, MaximumSize = new Size(200, 0), Margin = new Padding(3, 4, 3, 4) };

        private GroupBox DlssGroup()
        {
            if (ctx.Data.DlssDownloads?.Newest is DlssRelease newest)
            {
                dlssDll.Items[Array.IndexOf(DlssDll.VersionOrder, DlssDllChoice.Newest)] = $"NVIDIA {newest.Version.ToString(3)} (latest)";
                dlssDownload.Text = $"Download ({newest.SizeText})...";
                ownTips[dlssDownload] = Wrap($"Downloads DLSS {newest.Version.ToString(3)} ({newest.SizeText}) straight from NVIDIA's GitHub, once you "
                    + "accept NVIDIA's license. EternalVR does not ship NVIDIA's file.");
            }
            dlssDownload.Click += (s, e) => DownloadDlssDll();
            dlssChoose.Click += (s, e) => ChooseDlssDll();
            dlssDll.SelectedIndexChanged += (s, e) =>
            {
                // Load file... with none chosen yet: pick one now, or go back to the newest.
                if (!loading && SelectedDlssVersion() == DlssDllChoice.File && string.IsNullOrWhiteSpace(ctx.Settings.DlssDllPath)
                    && !ChooseDlssDll())
                    SelectDlssVersion(DlssDllChoice.Newest);
            };
            var buttons = new FlowLayoutPanel { AutoSize = true, Margin = Padding.Empty, WrapContents = false };
            buttons.Controls.AddRange(new Control[] { dlssDownload, dlssChoose });
            var version = new FlowLayoutPanel { AutoSize = true, Margin = Padding.Empty, WrapContents = false, FlowDirection = FlowDirection.TopDown };
            version.Controls.AddRange(new Control[] { dlssDll, buttons });
            return Group("DLSS",
                Row(Setting.DlssQuality, dlssQuality,
                    s => dlssQuality.SelectedIndex = (int)s.Dlss,
                    s => s.Dlss = (DlssQuality)Math.Max(0, dlssQuality.SelectedIndex)),
                Row(Setting.DlssVersion, version, s => SelectDlssVersion(s.DlssDll), s => s.DlssDll = SelectedDlssVersion(), fit: w => FitList(dlssDll, w)),
                Row(Setting.DlssPreset, dlssPreset,
                    s => dlssPreset.SelectedIndex = DlssDll.PresetIndex(s.DlssPreset),
                    s => s.DlssPreset = DlssDll.PresetValues[Math.Max(0, dlssPreset.SelectedIndex)]),
                // Nothing to save: the line follows the rows above (ShowDlss).
                Row(Setting.DlssInHeadset, dlssRuns, s => { }, s => { }));
        }

        private DlssDllChoice SelectedDlssVersion() =>
            dlssDll.SelectedIndex >= 0 ? DlssDll.VersionOrder[dlssDll.SelectedIndex] : DlssDllChoice.Newest;

        private void SelectDlssVersion(DlssDllChoice choice) => dlssDll.SelectedIndex = Math.Max(0, Array.IndexOf(DlssDll.VersionOrder, choice));

        /// <summary>Picks the player's nvngx_dlss.dll; false when none was chosen or the file cannot be used (said in a message).</summary>
        private bool ChooseDlssDll()
        {
            using (var dlg = new OpenFileDialog { Filter = "DLSS DLL (" + DlssDll.FileName + ")|" + DlssDll.FileName, Title = "Choose a newer " + DlssDll.FileName })
            {
                if (!string.IsNullOrWhiteSpace(ctx.Settings.DlssDllPath)) dlg.InitialDirectory = Path.GetDirectoryName(ctx.Settings.DlssDllPath);
                if (dlg.ShowDialog(this) != DialogResult.OK) return false;
                var check = DlssDll.Inspect(dlg.FileName);
                if (!check.Ok)
                {
                    MessageBox.Show(this, check.Problem, "DLSS version", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    return false;
                }
                ctx.Settings.DlssDllPath = dlg.FileName;
                ctx.Log.Info("DLSS file chosen: " + dlg.FileName + " (version " + check.Version + ")");
                UseDlssVersion(DlssDllChoice.File);
                return true;
            }
        }

        /// <summary>Downloads NVIDIA's newest DLL after the player accepts NVIDIA's license, then uses it.</summary>
        private void DownloadDlssDll()
        {
            var release = ctx.Data.DlssDownloads?.Newest;
            if (release == null) return;
            using (var dlg = new DlssDownloadDialog(release, ctx.Paths.DlssFiles, ctx.Log.Info))
            {
                if (dlg.ShowDialog(this) != DialogResult.OK || dlg.FilePath == null) return;
                UseDlssVersion(DlssDllChoice.Newest);
            }
        }

        private void UseDlssVersion(DlssDllChoice choice)
        {
            loading = true;
            try { SelectDlssVersion(choice); }
            finally { loading = false; }
            SaveSettings();
            MarkProfileChanged();
            UpdateRules();
        }

        /// <summary>The Download and Choose buttons of the version chosen, and the "In the headset" line.</summary>
        private void ShowDlss()
        {
            var s = ctx.Settings;
            var newest = ctx.Data.DlssDownloads?.Newest;
            bool ready = ctx.NewestDlss(verify: false) != null;
            dlssDownload.Visible = s.DlssDll == DlssDllChoice.Newest && newest != null && !ready;
            dlssChoose.Visible = s.DlssDll == DlssDllChoice.File;
            var file = s.DlssDll == DlssDllChoice.File && !string.IsNullOrWhiteSpace(s.DlssDllPath) ? DlssDll.Inspect(s.DlssDllPath) : null;
            dlssRuns.Text = DlssDll.WhatRuns(s, newest, ready, file);
            // Green when the newer DLSS asked for will run, amber when the game's runs in its place, plain for the game's by choice.
            bool? newer = s.DlssDll == DlssDllChoice.Newest ? newest != null && ready
                : s.DlssDll == DlssDllChoice.File ? file != null && file.Ok : (bool?)null;
            dlssRuns.ForeColor = newer == null ? SystemColors.ControlText : newer.Value ? GoodText : WarningText;
            var tip = SettingTexts.For(Setting.DlssInHeadset).Tooltip;
            if (file != null) tip = s.DlssDllPath + ": " + DlssDll.Describe(file) + ". " + tip;
            ownTips[dlssRuns] = Wrap(tip);
            tips.SetToolTip(dlssRuns, ownTips[dlssRuns]);
        }
    }
}
