using System;
using System.Drawing;
using System.Linq;
using System.Windows.Forms;
using EternalVR.Launcher.Core.Settings;

namespace EternalVR.Launcher
{
    /// <summary>
    /// Weapon directions: the weapon each stick direction picks under Weapon by direction, one list per direction laid out
    /// as the stick's compass (<see cref="WeaponDirectionTable"/>). Opens on the table the settings hold; OK gives the new one
    /// as <see cref="Value"/>.
    /// </summary>
    internal sealed class WeaponDirectionsDialog : Form
    {
        /// <summary>Each direction's place on the compass (column, row), in <see cref="WeaponDirectionTable.Directions"/> order.</summary>
        private static readonly Point[] Cells =
        {
            new Point(1, 0), new Point(2, 0), new Point(2, 1), new Point(2, 2), new Point(1, 2), new Point(0, 2), new Point(0, 1), new Point(0, 0),
        };
        private static readonly string[] DirectionNames = { "Up", "Up-right", "Right", "Down-right", "Down", "Down-left", "Left", "Up-left" };

        private readonly ComboBox[] lists;

        /// <summary>The table chosen, in the setting's text (empty for the game's wheel).</summary>
        public string Value => WeaponDirectionTable.Format(lists.Select(l => Math.Max(0, l.SelectedIndex)).ToArray());

        public WeaponDirectionsDialog(string value)
        {
            Text = "Weapon directions";
            // Sizes are at 96 DPI; the scaling pass at ResumeLayout scales them to the display's.
            SuspendLayout();
            AutoScaleDimensions = new SizeF(96F, 96F);
            AutoScaleMode = AutoScaleMode.Dpi;
            Font = MainForm.UiFont();
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = MinimizeBox = false;
            ShowInTaskbar = false;
            StartPosition = FormStartPosition.CenterParent;
            AutoSize = true;
            AutoSizeMode = AutoSizeMode.GrowAndShrink;
            Icon = Branding.WindowIcon() ?? Icon;

            var choices = new[] { "None" }.Concat(WeaponDirectionTable.Weapons.Select((w, i) => (i + 1) + " " + w)).ToArray();
            var compass = new TableLayoutPanel { AutoSize = true, ColumnCount = 3, RowCount = 3, Margin = new Padding(0, 0, 0, 6) };
            lists = new ComboBox[WeaponDirectionTable.Directions.Length];
            for (int i = 0; i < lists.Length; i++)
            {
                var list = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 150, AccessibleName = DirectionNames[i] };
                list.Items.AddRange(choices);
                lists[i] = list;
                var cell = new FlowLayoutPanel { AutoSize = true, FlowDirection = FlowDirection.TopDown, WrapContents = false, Margin = new Padding(4) };
                cell.Controls.Add(new Label { Text = DirectionNames[i], AutoSize = true, Margin = new Padding(3, 0, 3, 0) });
                cell.Controls.Add(list);
                compass.Controls.Add(cell, Cells[i].X, Cells[i].Y);
            }
            var hint = new Label { Text = "Directions set to None go to their nearest neighbours.", AutoSize = true, Margin = new Padding(7, 0, 3, 6) };

            var gameWheel = Sized(new Button { Text = "Game's wheel" }, 110);
            var four = Sized(new Button { Text = "Four directions" }, 110);
            var ok = Sized(new Button { Text = "OK", DialogResult = DialogResult.OK }, 80);
            var cancel = Sized(new Button { Text = "Cancel", DialogResult = DialogResult.Cancel }, 80);
            gameWheel.Click += (s, e) => ShowTable(WeaponDirectionTable.GameWheel());
            four.Click += (s, e) => ShowTable(WeaponDirectionTable.FourDirections());
            var tips = new ToolTip { AutoPopDelay = 30000, InitialDelay = 400, ReshowDelay = 100 };
            tips.SetToolTip(gameWheel, "Each direction picks the weapon the game's wheel shows there (the default).");
            tips.SetToolTip(four, "The game's wheel on up, right, down and left, nothing on the diagonals: each weapon takes a quarter "
                + "of the circle.");
            Disposed += (s, e) => tips.Dispose();
            var presets = new FlowLayoutPanel { AutoSize = true, WrapContents = false, Margin = Padding.Empty };
            presets.Controls.AddRange(new Control[] { gameWheel, four });
            var close = new FlowLayoutPanel { AutoSize = true, WrapContents = false, FlowDirection = FlowDirection.RightToLeft, Dock = DockStyle.Right, Margin = Padding.Empty };
            close.Controls.AddRange(new Control[] { cancel, ok });
            var bottom = new TableLayoutPanel { AutoSize = true, ColumnCount = 2, Dock = DockStyle.Fill, Margin = new Padding(0, 6, 0, 0) };
            bottom.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            bottom.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            bottom.Controls.Add(presets, 0, 0);
            bottom.Controls.Add(close, 1, 0);

            var layout = new TableLayoutPanel { AutoSize = true, ColumnCount = 1, Padding = new Padding(10) };
            layout.Controls.AddRange(new Control[] { compass, hint, bottom });
            Controls.Add(layout);
            AcceptButton = ok;
            CancelButton = cancel;
            MainForm.FitListHeights(this);
            DisplayScale.Follow(this);
            ResumeLayout(false);
            PerformLayout();

            ShowTable(WeaponDirectionTable.Parse(value));
        }

        private void ShowTable(int[] slots)
        {
            for (int i = 0; i < lists.Length; i++) lists[i].SelectedIndex = slots[i] >= 1 && slots[i] <= 8 ? slots[i] : 0;
        }

        /// <summary>A button of the main window's height, <paramref name="width"/> wide at 96 DPI, never squeezed by its table.</summary>
        private static Button Sized(Button button, int width)
        {
            button.Size = new Size(width, MainForm.ButtonHeight);
            button.MinimumSize = button.Size;
            return button;
        }
    }
}
