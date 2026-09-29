using System.Collections.Generic;
using System.Linq;
using System.Windows.Forms;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;

namespace EternalVR.Launcher
{
    /// <summary>
    /// Texture streaming and the processor saver on the Play tab: one checkbox per item of <c>data\cpu-saver.txt</c>
    /// (<see cref="CpuSaver"/>), each with its own tooltip. The window sets a choice for every item it shows.
    /// </summary>
    public sealed partial class MainForm
    {
        /// <summary>The Picture group's "Texture streaming" row: its items as a list of checkboxes, each with its note
        /// below; null when the data file puts no item there.</summary>
        private SettingRow StreamingRow()
        {
            var items = ctx.Data.CpuSaver.InRow(Setting.TextureStreaming).ToList();
            if (items.Count == 0) return null;
            var panel = new FlowLayoutPanel { AutoSize = true, Margin = Padding.Empty, WrapContents = false, FlowDirection = FlowDirection.TopDown };
            var boxes = new List<CheckBox>();
            foreach (var item in items)
            {
                var box = new CheckBox { Text = item.Label, AutoSize = true, AccessibleName = item.Label, Margin = new Padding(3, 3, 3, 0) };
                ownTips[box] = Wrap(SaverTip(item));
                boxes.Add(box);
                panel.Controls.Add(box);
                if (item.Note.Length > 0)
                {
                    // Under the checkbox's text.
                    var note = new Label { Text = item.Note, AutoSize = true, Margin = new Padding(20, 0, 3, 3) };
                    ownTips[note] = ownTips[box];
                    panel.Controls.Add(note);
                }
            }
            var row = Row(Setting.TextureStreaming, panel, s => LoadSaver(items, boxes, s), s => ReadSaver(items, boxes, s));
            // The label beside the first checkbox.
            row.Label.Anchor = AnchorStyles.Top | AnchorStyles.Left;
            row.Label.Padding = new Padding(0, 4, 0, 0);
            return row;
        }

        /// <summary>The "Processor saver" group: a row per item with its label on the left, as in the other groups; null
        /// when the data file has none.</summary>
        private GroupBox SaverGroup()
        {
            var items = ctx.Data.CpuSaver.InRow(Setting.ProcessorSaver).ToList();
            if (items.Count == 0) return null;
            var groupRows = items.Select(item =>
            {
                var one = new[] { item };
                var boxes = new[] { new CheckBox { AutoSize = true } };
                return Row(Setting.ProcessorSaver, boxes[0], s => LoadSaver(one, boxes, s), s => ReadSaver(one, boxes, s),
                    text: new SettingTexts.Text(item.Label, SaverTip(item)));
            }).ToArray();
            var text = SettingTexts.For(Setting.ProcessorSaver);
            var group = Group(text.Label, groupRows);
            tips.SetToolTip(group, Wrap(text.Tooltip));
            return group;
        }

        /// <summary>The item's tooltip and the game settings it changes.</summary>
        private static string SaverTip(CpuSaverItem item) => item.Tooltip + " Game settings: " + item.CvarText + ".";

        private static void LoadSaver(IReadOnlyList<CpuSaverItem> items, IReadOnlyList<CheckBox> boxes, LauncherSettings s)
        {
            for (int i = 0; i < items.Count; i++) boxes[i].Checked = CpuSaver.IsOn(items[i], s);
        }

        private static void ReadSaver(IReadOnlyList<CpuSaverItem> items, IReadOnlyList<CheckBox> boxes, LauncherSettings s) =>
            s.SetCpuSaverChoices(items.Select((item, i) => new KeyValuePair<string, bool>(item.Id, boxes[i].Checked)));
    }
}
