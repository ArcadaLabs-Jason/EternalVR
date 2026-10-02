using System;
using System.Collections.Generic;
using System.Drawing;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace EternalVR.Launcher
{
    /// <summary>
    /// How the Play and Advanced tabs are laid out: titled groups of rows in two columns, every group with the same label
    /// column (longer labels wrap), so each control of a tab column starts at the same x; lists fill the control column up to
    /// <see cref="ListCap"/>, values wrap in it, and a yes/no row is its checkbox and label across both columns. The widths
    /// are set again whenever a group changes size (the window, the display's scale, the page's scrollbar).
    /// </summary>
    public sealed partial class MainForm
    {
        /// <summary>The label column's width at 96 DPI, the same in every group of both tabs.</summary>
        private const int LabelColumn = 124;
        /// <summary>The widest a list grows to at 96 DPI.</summary>
        private const int ListCap = 260;
        /// <summary>The window's smallest size at 96 DPI: the control column keeps room for a list.</summary>
        private static readonly Size MinWindow = new Size(640, 480);
        /// <summary>The two button heights at 96 DPI: the bar under the tabs, and inside the window's parts.</summary>
        internal const int BarButtonHeight = 36, ButtonHeight = 26;

        /// <summary>Every group with its grid and rows, fitted together (<see cref="FitGroups"/>).</summary>
        private readonly List<Action> groupFits = new List<Action>();
        /// <summary>Set once the window loads: before, the groups change size many times as it is built and scaled.</summary>
        private bool groupsReady;
        private bool fitQueued;

        /// <summary>A titled box of rows: labels on the left, controls on the right; a wide row's control under its label.</summary>
        private GroupBox Group(string title, params SettingRow[] groupRows)
        {
            var box = new GroupBox
            {
                Text = title, Dock = DockStyle.Fill, AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, Padding = new Padding(6, 4, 6, 4),
            };
            // Docked at the top of the box. The label column is as wide as its labels, which FitGroup sets to the shared width.
            var grid = new TableLayoutPanel { Dock = DockStyle.Top, ColumnCount = 2, AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink };
            grid.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            grid.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            foreach (var r in groupRows)
            {
                if (r.Check)
                {
                    var line = CheckLine(r);
                    grid.Controls.Add(line);
                    grid.SetColumnSpan(line, 2);
                    continue;
                }
                grid.Controls.Add(r.Label);
                if (r.Wide)
                {
                    // The label alone on its line, the control under it across both columns.
                    grid.SetColumnSpan(r.Label, 2);
                    grid.Controls.Add(r.Control);
                    grid.SetColumnSpan(r.Control, 2);
                    continue;
                }
                grid.Controls.Add(r.Control);
            }
            box.Controls.Add(grid);
            groupFits.Add(() => FitGroup(box, grid, groupRows));
            box.SizeChanged += (s, e) => QueueFitGroups();
            return box;
        }

        /// <summary>
        /// A yes/no row: the checkbox, then the row's label as its text. The label stays enabled when the checkbox is not, so it
        /// still shows why in its tooltip; clicking it ticks the box as a checkbox's own text would. A plain panel placed by
        /// <see cref="FitCheckLine"/>: as auto-sized flow panels, the 23 lines made the window take seconds longer to open.
        /// </summary>
        private static Panel CheckLine(SettingRow r)
        {
            var box = (CheckBox)r.Control;
            box.Margin = new Padding(3, 3, 0, 3);
            r.Label.Margin = new Padding(0, 3, 3, 3);
            r.Label.Click += (s, e) => { if (box.Enabled) box.Checked = !box.Checked; };
            // Placed by hand: anchored at the top left, so a change of the line's size never moves them.
            box.Anchor = r.Label.Anchor = AnchorStyles.Top | AnchorStyles.Left;
            var line = new Panel { Margin = Padding.Empty };
            line.Controls.AddRange(new Control[] { box, r.Label });
            return line;
        }

        /// <summary>
        /// Two columns of group boxes, with <paramref name="top"/> (null: none) across both above them and <paramref name="below"/>
        /// across both below, the page scrolling when the window is smaller than they are.
        /// </summary>
        private static TabPage Page(string title, Control top, Control[] left, Control[] right, params Control[] below)
        {
            var page = new TabPage(title) { AutoScroll = true, Padding = new Padding(6) };
            var grid = new TableLayoutPanel { Dock = DockStyle.Top, ColumnCount = 2, AutoSize = true };
            grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
            grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
            int row = 0;
            if (top != null)
            {
                grid.Controls.Add(top, 0, row);
                grid.SetColumnSpan(top, 2);
                row++;
            }
            grid.Controls.Add(Column(left), 0, row);
            grid.Controls.Add(Column(right), 1, row);
            for (int i = 0; i < below.Length; i++)
            {
                grid.Controls.Add(below[i], 0, row + 1 + i);
                grid.SetColumnSpan(below[i], 2);
            }
            page.Controls.Add(grid);
            return page;
        }

        /// <summary>A column of groups, at the top of its cell and only as tall as they are (not stretched to the other column's height).</summary>
        private static Control Column(Control[] groups)
        {
            var column = new TableLayoutPanel
            {
                Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right, ColumnCount = 1, AutoSize = true, Margin = Padding.Empty,
            };
            column.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            foreach (var g in groups) column.Controls.Add(g);
            return column;
        }

        /// <summary>A group changed size: every group is fitted once the change is over (a resize moves several at once).</summary>
        private void QueueFitGroups()
        {
            if (!groupsReady || fitQueued || !IsHandleCreated) return;
            fitQueued = true;
            BeginInvoke(new Action(FitGroups));
        }

        /// <summary>
        /// Fits every group with both tabs' layouts held, then lays each tab out once: a group fitted alone laid out the whole
        /// tab after it, which made the window take seconds to open.
        /// </summary>
        private void FitGroups()
        {
            fitQueued = false;
            groupsReady = true;
            playPage.SuspendLayout();
            advancedPage.SuspendLayout();
            try
            {
                foreach (var fit in groupFits) fit();
            }
            finally
            {
                advancedPage.ResumeLayout(true);
                playPage.ResumeLayout(true);
            }
        }

        /// <summary>
        /// The group's rows at its width: every label as wide as the label column (wrapping), each control fitted to the control
        /// column (<see cref="SettingRow.Fit"/>, given the width with the control's margins), a wide row's to the whole width, and
        /// each label beside its control's first line.
        /// </summary>
        private void FitGroup(GroupBox box, TableLayoutPanel grid, SettingRow[] groupRows)
        {
            int inner = box.DisplayRectangle.Width - grid.Padding.Horizontal - grid.Margin.Horizontal;
            int labels = LogicalToDeviceUnits(LabelColumn);
            if (inner <= labels) return;
            // Only when the width or the scale changed: a change of height alone (a row's text, the other column) needs nothing.
            var key = new Size(inner, labels * 1000 + box.Font.Height);
            if (box.Tag is Size done && done == key) return;
            box.Tag = key;
            // One layout of the group for all its changes, not one per change.
            grid.SuspendLayout();
            try { FitRows(groupRows, inner, labels); }
            finally { grid.ResumeLayout(true); }
        }

        private void FitRows(SettingRow[] groupRows, int inner, int labels)
        {
            foreach (var r in groupRows)
            {
                if (r.Check)
                {
                    FitCheckLine(r, inner);
                    continue;
                }
                if (r.Wide)
                {
                    WrapIn(r.Label, inner);
                    r.Fit?.Invoke(inner);
                    continue;
                }
                var width = new Size(labels - r.Label.Margin.Horizontal, 0);
                if (r.Label.MaximumSize != width) r.Label.MaximumSize = width;
                if (r.Label.MinimumSize != width) r.Label.MinimumSize = width;
                r.Fit?.Invoke(inner - labels);
                // The label's first line level with the control's (both at the top of the row).
                int top = Math.Max(0, r.Control.Margin.Top + FirstLineCenter(r.Control) - r.Label.Font.Height / 2);
                if (r.Label.Margin.Top != top) r.Label.Margin = new Padding(r.Label.Margin.Left, top, r.Label.Margin.Right, r.Label.Margin.Bottom);
            }
        }

        /// <summary>A yes/no row's line at <paramref name="width"/>: the label wraps after the box, the box level with its first line.</summary>
        private static void FitCheckLine(SettingRow r, int width)
        {
            Control box = r.Control, label = r.Label, line = box.Parent;
            // Its own size: a checkbox on a tab not shown yet still has the default one.
            var boxSize = box.GetPreferredSize(Size.Empty);
            if (box.Size != boxSize) box.Size = boxSize;
            int textLeft = box.Margin.Left + box.Width + box.Margin.Right + label.Margin.Left;
            WrapIn(r.Label, width - textLeft + label.Margin.Left);
            int center = label.Margin.Top + label.Font.Height / 2;
            int boxTop = Math.Max(0, center - box.Height / 2);
            int labelTop = label.Margin.Top + Math.Max(0, box.Height / 2 - center);
            var size = new Size(width, Math.Max(boxTop + box.Height + box.Margin.Bottom, labelTop + label.Height + label.Margin.Bottom));
            if (line.Size != size) line.Size = size;
            box.Location = new Point(box.Margin.Left, boxTop);
            label.Location = new Point(textLeft, labelTop);
        }

        /// <summary>The middle of a control's first line of text, from its top: a label's first line, a panel's first control's.</summary>
        private static int FirstLineCenter(Control c)
        {
            if (c is Label) return c.Padding.Top + c.Font.Height / 2;
            if ((c is FlowLayoutPanel || c is TableLayoutPanel) && c.Controls.Count > 0)
            {
                var first = c.Controls[0];
                return c.Padding.Top + first.Margin.Top + FirstLineCenter(first);
            }
            return c.Height / 2;
        }

        /// <summary>A list as wide as the control column (<paramref name="width"/>, its margins included), up to <see cref="ListCap"/>.</summary>
        private void FitList(ComboBox list, int width) => SetWidth(list, Math.Min(LogicalToDeviceUnits(ListCap), width - list.Margin.Horizontal));

        private static void SetWidth(Control c, int width)
        {
            width = Math.Max(40, width);
            if (c.Width != width) c.Width = width;
        }

        /// <summary>A label that wraps at <paramref name="width"/> (its margins included).</summary>
        private static void WrapIn(Label label, int width)
        {
            var max = new Size(Math.Max(40, width - label.Margin.Horizontal), 0);
            if (label.MaximumSize != max) label.MaximumSize = max;
        }

        /// <summary>
        /// The window's smallest size at the window's scale. Set once it is shown, and again after each change of scale: a window
        /// made at the sign-in scale and shown on a display with another one is rescaled after it loads, and a minimum set in
        /// the old scale would keep it that size.
        /// </summary>
        private void FitWindow()
        {
            if (!IsHandleCreated) return;
            var area = Screen.FromControl(this).WorkingArea;
            var min = new Size(Math.Min(area.Width, LogicalToDeviceUnits(MinWindow.Width)), Math.Min(area.Height, LogicalToDeviceUnits(MinWindow.Height)));
            if (MinimumSize != min) MinimumSize = min;
            FitStatus();
        }

        /// <summary>
        /// A window made at the sign-in display's scale and placed on a display with another one before it is shown (centred on
        /// the display with the pointer) gets no scale change from Windows, and stayed at the sign-in scale: as large as on a
        /// 200 % display on a 125 % one. It is given the change Windows gives a window dragged there.
        /// </summary>
        private void MatchDisplayScale()
        {
            if (!IsHandleCreated) return;
            int dpi = (int)GetDpiForWindow(Handle);
            if (dpi <= 0 || dpi == DeviceDpi) return;
            var size = new Size(Width * dpi / DeviceDpi, Height * dpi / DeviceDpi);
            var rect = new Rect { Left = Left, Top = Top, Right = Left + size.Width, Bottom = Top + size.Height };
            SendMessage(Handle, WM_DPICHANGED, (IntPtr)((dpi << 16) | dpi), ref rect);
        }

        private const int WM_DPICHANGED = 0x02E0;

        [StructLayout(LayoutKind.Sequential)]
        private struct Rect
        {
            public int Left, Top, Right, Bottom;
        }

        [DllImport("user32.dll")]
        private static extern uint GetDpiForWindow(IntPtr hwnd);

        [DllImport("user32.dll")]
        private static extern IntPtr SendMessage(IntPtr hwnd, int msg, IntPtr wParam, ref Rect lParam);

        /// <summary>The status line wraps at the window's width.</summary>
        private void FitStatus() => WrapIn(statusLabel, ClientSize.Width - LogicalToDeviceUnits(24));
    }
}
