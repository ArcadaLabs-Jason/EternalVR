using System;
using System.Collections.Generic;
using System.Linq;
using System.Windows.Forms;
using EternalVR.Launcher.Core.Settings;

namespace EternalVR.Launcher
{
    /// <summary>
    /// The settings as a preset plus the player's own changes (REQ-10, <see cref="ProfileEdits"/>): each row whose value
    /// differs from the preset has a dot after its name and says so in its tooltip, right-clicking its name resets it to
    /// the preset (or all of them), and the profile strip says how many settings differ. Recommended, the
    /// launcher's defaults, is the only preset for now (<see cref="SettingsPresets"/>).
    /// </summary>
    public sealed partial class MainForm
    {
        /// <summary>After the name of a setting that differs from the preset.</summary>
        private const string ChangedMark = " •";
        private readonly Label presetState = new Label { AutoSize = true, Margin = new Padding(12, 8, 3, 3) };
        private readonly ContextMenuStrip rowMenu = new ContextMenuStrip();
        private readonly ToolStripMenuItem resetOne = new ToolStripMenuItem();
        private readonly ToolStripMenuItem resetAll = new ToolStripMenuItem();
        /// <summary>The row of each name (and yes/no box) that opens <see cref="rowMenu"/>.</summary>
        private readonly Dictionary<Control, SettingRow> rowOf = new Dictionary<Control, SettingRow>();
        /// <summary>The row the menu was opened on.</summary>
        private SettingRow menuRow;

        private SettingsPreset Preset => SettingsPresets.Of(ctx.Settings);

        /// <summary>The menu on the row's name (and its box on a yes/no row), for a row that holds a setting.</summary>
        private void OfferReset(SettingRow row)
        {
            if (row.Keys.Count == 0) return;
            if (rowMenu.Items.Count == 0)
            {
                rowMenu.Items.AddRange(new ToolStripItem[] { resetOne, new ToolStripSeparator(), resetAll });
                rowMenu.Opening += (s, e) => e.Cancel = !PrepareRowMenu(rowMenu.SourceControl);
                resetOne.Click += (s, e) => { if (menuRow != null) ResetRow(menuRow); };
                resetAll.Click += (s, e) => ResetToPreset();
            }
            row.Label.ContextMenuStrip = rowMenu;
            rowOf[row.Label] = row;
            if (row.Control is CheckBox box)
            {
                box.ContextMenuStrip = rowMenu;
                rowOf[box] = row;
            }
        }

        private bool PrepareRowMenu(Control source)
        {
            if (source == null || !rowOf.TryGetValue(source, out menuRow)) return false;
            var preset = Preset;
            resetOne.Text = "Reset to " + preset.Name;
            resetOne.Enabled = IsChanged(menuRow, ProfileEdits.Changed(ctx.Settings, preset.Values(), ctx.Data.CpuSaver));
            resetAll.Text = "Reset all to " + preset.Name + "...";
            return true;
        }

        private static bool IsChanged(SettingRow row, ISet<string> changed) => row.Keys.Any(changed.Contains);

        /// <summary>
        /// Marks every row whose value differs from the preset, and says in the profile strip how many do. The line for each
        /// marked row's tooltip.
        /// </summary>
        private Dictionary<SettingRow, string> ShowChanges()
        {
            var preset = Preset;
            var changed = ProfileEdits.Changed(ctx.Settings, preset.Values(), ctx.Data.CpuSaver);
            var notes = new Dictionary<SettingRow, string>();
            var relabel = new List<KeyValuePair<Label, string>>();
            foreach (var r in rows)
            {
                bool edited = IsChanged(r, changed);
                var text = edited ? r.Name + ChangedMark : r.Name;
                if (r.Label.Text != text) relabel.Add(new KeyValuePair<Label, string>(r.Label, text));
                if (edited) notes[r] = "Changed from " + preset.Name + ". Right-click its name to reset it to " + preset.Name + ".";
            }
            // Each label's change lays its group out again: a profile's many are made with both tabs' layouts held, then
            // each laid out once (as the window does when it opens).
            bool hold = relabel.Count > 3 && playPage != null && advancedPage != null;
            if (hold)
            {
                SuspendTree(playPage);
                SuspendTree(advancedPage);
            }
            foreach (var kv in relabel) kv.Key.Text = kv.Value;
            if (hold)
            {
                ResumeTree(advancedPage, true);
                ResumeTree(playPage, true);
            }
            int count = notes.Count;
            presetState.Text = count == 0 ? preset.Name + " settings"
                : preset.Name + " with " + count + (count == 1 ? " change" : " changes") + " (marked" + ChangedMark + ")";
            return notes;
        }

        private string PresetStateTip() =>
            "Every VR settings profile starts from " + Preset.Name + ", the launcher's defaults, and keeps only your changes from it. "
            + "A setting you changed has a dot after its name on the Play and Advanced tabs; right-click the name to reset it to "
            + Preset.Name + ", or press Reset to " + Preset.Name + " (Advanced tab) to reset all of them. "
            + "Your controls are not part of it.";

        /// <summary>Puts the preset's value back in one row.</summary>
        private void ResetRow(SettingRow row)
        {
            var preset = Preset;
            ReadControlsIntoSettings();
            ctx.Settings = ProfileEdits.Reset(ctx.Settings, preset.Values(), row.Keys, ctx.Data.CpuSaver);
            LoadSettingsIntoControls();
            SaveSettings();
            MarkProfileChanged();
            ctx.Log.Info("setting reset to " + preset.Name + ": " + row.Name);
            if (row.Preflight) RunPreflight();
        }

        /// <summary>Puts every setting of both tabs back to the preset, the folders, the runtime and the controls kept.</summary>
        private void ResetToPreset()
        {
            var preset = Preset;
            var answer = MessageBox.Show(this,
                "Put every setting on the Play and Advanced tabs back to " + preset.Name + "?\n\nThe game folder, the layer folder, the OpenXR runtime, the graphics card choice and your controls are kept.",
                "Reset to " + preset.Name, MessageBoxButtons.OKCancel, MessageBoxIcon.Question, MessageBoxDefaultButton.Button2);
            if (answer != DialogResult.OK) return;
            ReadControlsIntoSettings();
            ctx.Settings = ProfileEdits.ResetAll(ctx.Settings, preset.Values(), ctx.Data.CpuSaver);
            LoadSettingsIntoControls();
            SaveSettings();
            MarkProfileChanged();
            ctx.Log.Info("settings reset to " + preset.Name + " (folders, runtime, graphics card and controls kept)");
            RunPreflight();
        }
    }
}
