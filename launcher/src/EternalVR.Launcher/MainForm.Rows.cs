using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Text;
using System.Windows.Forms;
using EternalVR.Launcher.Core.Settings;

namespace EternalVR.Launcher
{
    /// <summary>
    /// The settings rows of the Play and Advanced tabs: each row loads and reads one setting, saves on every change, and is
    /// greyed out with the reason in its tooltip when the others make it not apply (<see cref="SettingRules"/>).
    /// </summary>
    public sealed partial class MainForm
    {
        private readonly ToolTip tips = new ToolTip { AutoPopDelay = 30000, InitialDelay = 400, ReshowDelay = 100 };
        private readonly List<SettingRow> rows = new List<SettingRow>();
        /// <summary>Controls with a tooltip of their own inside a row (a CPU Saver item's checkbox), kept over the row's.</summary>
        private readonly Dictionary<Control, string> ownTips = new Dictionary<Control, string>();
        /// <summary>True while the controls are filled from the settings: their change events save nothing.</summary>
        private bool loading;

        private sealed class SettingRow
        {
            public Setting Id;
            public Label Label;
            public Control Control;
            public Action<LauncherSettings> Load;
            public Action<LauncherSettings> Read;
            /// <summary>The row's label and tooltip when they are not the setting's own (a CPU Saver item's, from its data file).</summary>
            public SettingTexts.Text Text;
            /// <summary>The checks run again after a change (the runtime, the mode, the arguments).</summary>
            public bool Preflight;
            /// <summary>The control goes on its own line under the label, across both columns (too wide for the second one).</summary>
            public bool Wide;
            /// <summary>A yes/no row: its checkbox, then its label, across both columns (<see cref="CheckLine"/>).</summary>
            public bool Check;
            /// <summary>Fits the control to the width it has, in pixels (<see cref="FitGroup"/>); null when its size is its own.</summary>
            public Action<int> Fit;
        }

        private SettingRow Row(Setting id, Control control, Action<LauncherSettings> load, Action<LauncherSettings> read, bool preflight = false,
            SettingTexts.Text text = null, bool wide = false, Action<int> fit = null)
        {
            var label = (text ?? SettingTexts.For(id)).Label;
            control.AccessibleName = label;
            fit = fit ?? FitOf(control);
            bool check = control is CheckBox;
            // In a flow panel: a table row keeps a list's first height (FitListHeights).
            if (control is ComboBox) control = WithUnit(control, null);
            var row = new SettingRow
            {
                Id = id, Control = control, Load = load, Read = read, Preflight = preflight, Text = text, Wide = wide, Check = check, Fit = fit,
                Label = new Label { Text = label, AutoSize = true, Anchor = AnchorStyles.Left },
            };
            // A yes/no row's checkbox and label are placed by CheckLine; the others sit at the top of their row (FitGroup).
            control.Anchor = check ? AnchorStyles.Left : AnchorStyles.Top | AnchorStyles.Left;
            if (!check) row.Label.Anchor = AnchorStyles.Top | AnchorStyles.Left;
            rows.Add(row);
            Watch(control, row);
            return row;
        }

        /// <summary>How a control takes the width it has: a list fills it up to a cap, a text box fills it, a value wraps in it.</summary>
        private Action<int> FitOf(Control control)
        {
            switch (control)
            {
                case ComboBox list: return w => FitList(list, w);
                case TextBox box: return w => SetWidth(box, w - box.Margin.Horizontal);
                case Label value: return w => WrapIn(value, w);
                default: return null;
            }
        }

        /// <summary>A list control filled with the setting's choices (<see cref="SettingTexts"/>).</summary>
        private static ComboBox Choices(Setting id)
        {
            var box = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 180 };
            foreach (var c in SettingTexts.For(id).Choices) box.Items.Add(c);
            return box;
        }

        private static NumericUpDown Number(double min, double max, double step, int decimals) => new NumericUpDown
        {
            Minimum = (decimal)min, Maximum = (decimal)max, Increment = (decimal)step, DecimalPlaces = decimals, Width = 70,
        };

        /// <summary>A control followed by its unit.</summary>
        private static FlowLayoutPanel WithUnit(Control control, string unit)
        {
            var panel = new FlowLayoutPanel { AutoSize = true, Margin = Padding.Empty, WrapContents = false };
            panel.Controls.Add(control);
            if (unit != null) panel.Controls.Add(Caption(unit));
            return panel;
        }

        private static Label Caption(string text) =>
            new Label { Text = text, AutoSize = true, Padding = new Padding(0, 6, 0, 0), Margin = new Padding(2, 0, 2, 0) };

        private void Watch(Control c, SettingRow row)
        {
            if (c is ComboBox combo) combo.SelectedIndexChanged += (s, e) => Changed(row);
            else if (c is CheckBox box) box.CheckedChanged += (s, e) => Changed(row);
            else if (c is NumericUpDown number) number.ValueChanged += (s, e) => Changed(row);
            else if (c is TextBox text)
            {
                text.TextChanged += (s, e) => Changed(row, false);
                text.Leave += (s, e) => { if (row.Preflight && !loading) RunPreflight(); };
            }
            else
                foreach (Control child in c.Controls) Watch(child, row);
        }

        private void Changed(SettingRow row, bool preflight = true)
        {
            if (loading) return;
            SaveSettings();
            MarkProfileChanged();
            UpdateRules();
            if (preflight && row.Preflight) RunPreflight();
        }

        private void LoadSettingsIntoControls()
        {
            if (settingsWritten == null)
            {
                try { if (File.Exists(ctx.Paths.SettingsFile)) settingsWritten = File.ReadAllText(ctx.Paths.SettingsFile); }
                catch (Exception e) when (e is IOException || e is UnauthorizedAccessException) { }
            }
            loading = true;
            if (!ParallelEyesOffered) ctx.Settings.ParallelEyes = false;
            try
            {
                foreach (var r in rows) r.Load(ctx.Settings);
                FillRuntimes();
            }
            finally
            {
                loading = false;
            }
            UpdateRules();
        }

        private void ReadControlsIntoSettings()
        {
            foreach (var r in rows) r.Read(ctx.Settings);
            // A launcher.ini from a test build cannot turn on Parallel Eye Rendering while its row is hidden.
            if (!ParallelEyesOffered) ctx.Settings.ParallelEyes = false;
        }

        // The settings file as the launcher last read or wrote it: an edit made by hand while the launcher is open is merged
        // into the next save instead of being overwritten (LauncherSettings.MergeHandEdits).
        private string settingsWritten;

        private void SaveSettings()
        {
            ReadControlsIntoSettings();
            var path = ctx.Paths.SettingsFile;
            try
            {
                var disk = File.Exists(path) ? File.ReadAllText(path) : null;
                if (settingsWritten != null && disk != null && disk != settingsWritten)
                {
                    try
                    {
                        ctx.Settings = LauncherSettings.Parse(LauncherSettings.MergeHandEdits(settingsWritten, ctx.Settings.Serialize(), disk));
                        ctx.Log.Info("launcher.ini was edited outside the launcher; those edits are kept");
                        LoadSettingsIntoControls();
                    }
                    catch (SettingsException e)
                    {
                        // A hand edit that does not parse would otherwise stop every later save.
                        ctx.Log.Error("launcher.ini was edited outside the launcher but could not be read (" + e.Message + "); the launcher's settings are saved instead");
                    }
                }
                ctx.Settings.Save(path);
                settingsWritten = File.ReadAllText(path);
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException || e is SettingsException)
            {
                ctx.Log.Error("saving settings failed: " + e.Message);
            }
        }

        /// <summary>Greys out the rows that do not apply and puts the reason first in their tooltip.</summary>
        private void UpdateRules()
        {
            foreach (var r in rows)
            {
                var why = SettingRules.WhyNot(r.Id, ctx.Settings, parallelEyesGame);
                var tip = Wrap((r.Text ?? SettingTexts.For(r.Id)).Tooltip);
                r.Control.Enabled = why == null;
                r.Label.ForeColor = why == null ? SystemColors.ControlText : SystemColors.GrayText;
                tips.SetToolTip(r.Label, why == null ? tip : Wrap(why) + Environment.NewLine + Environment.NewLine + tip);
                SetTip(r.Control, tip);
            }
            UpdateRowParts();
        }

        private void SetTip(Control c, string tip)
        {
            tips.SetToolTip(c, ownTips.TryGetValue(c, out var own) ? own : tip);
            if (c is FlowLayoutPanel) foreach (Control child in c.Controls) SetTip(child, tip);
        }

        /// <summary>Breaks a tooltip into lines of about 70 characters (a tooltip does not wrap by itself).</summary>
        private static string Wrap(string text, int width = 70)
        {
            var sb = new StringBuilder();
            int line = 0;
            foreach (var word in text.Split(' '))
            {
                if (line > 0 && line + 1 + word.Length > width)
                {
                    sb.Append(Environment.NewLine);
                    line = 0;
                }
                else if (line > 0)
                {
                    sb.Append(' ');
                    line++;
                }
                sb.Append(word);
                line += word.Length;
            }
            return sb.ToString();
        }

        private void ResetToDefaults()
        {
            var answer = MessageBox.Show(this,
                "Put every setting on the Play and Advanced tabs back to its default?\n\nThe game folder, the layer folder and the OpenXR runtime are kept.",
                "Reset to defaults", MessageBoxButtons.OKCancel, MessageBoxIcon.Question, MessageBoxDefaultButton.Button2);
            if (answer != DialogResult.OK) return;
            ctx.Settings = ctx.Settings.WithDefaults();
            LoadSettingsIntoControls();
            SaveSettings();
            MarkProfileChanged();
            ctx.Log.Info("settings reset to the defaults (folders and runtime kept)");
            RunPreflight();
        }
    }
}
