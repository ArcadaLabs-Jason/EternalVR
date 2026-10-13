using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows.Forms;

namespace EternalVR.Launcher.UiShots
{
    /// <summary>
    /// A window's pixels as Windows composes them (PrintWindow with PW_RENDERFULLCONTENT, cropped to the visible frame) and
    /// a list of the layout faults a look at the picture is for (controls cut off by their parent, button text wider than its button, uneven
    /// buttons in one row, a table whose hidden controls move the others to rows sized for something else, a sizable window whose
    /// content stops well above its bottom).
    /// </summary>
    internal static class Capture
    {
        public static Bitmap Window(Form form)
        {
            GetWindowRect(form.Handle, out var win);
            if (DwmGetWindowAttribute(form.Handle, DwmwaExtendedFrameBounds, out var frame, Marshal.SizeOf(typeof(Rect))) != 0) frame = win;
            using (var full = new Bitmap(win.Width, win.Height, PixelFormat.Format32bppArgb))
            {
                using (var g = Graphics.FromImage(full))
                {
                    var hdc = g.GetHdc();
                    try { PrintWindow(form.Handle, hdc, PwRenderFullContent); }
                    finally { g.ReleaseHdc(hdc); }
                }
                var crop = new Rectangle(frame.Left - win.Left, frame.Top - win.Top, frame.Width, frame.Height);
                crop.Intersect(new Rectangle(Point.Empty, full.Size));
                return full.Clone(crop, PixelFormat.Format32bppArgb);
            }
        }

        /// <summary>What the picture should be checked for, one line each; empty when nothing was found.</summary>
        public static IReadOnlyList<string> Faults(Form form)
        {
            var faults = new List<string>();
            Walk(form, faults);
            Unfilled(form, faults);
            return faults;
        }

        public static string Describe(Form form)
        {
            var sb = new StringBuilder();
            sb.AppendLine($"{form.GetType().Name} \"{form.Text}\" dpi {form.DeviceDpi} size {form.Size} client {form.ClientSize} font {form.Font.SizeInPoints}pt/{form.Font.Height}px");
            foreach (var c in All(form).Where(c => c.Visible && (c is Button || c is ProgressBar || c is Label || c is ComboBox || c is TextBox)))
                sb.AppendLine($"  {c.GetType().Name,-12} {Short(c.Text),-34} {c.Bounds} enabled={c.Enabled}");
            sb.AppendLine("containers:");
            foreach (var c in All(form).Where(c => c.Visible && (c is Panel || c is GroupBox || c is TabControl)))
                sb.AppendLine($"  {c.GetType().Name,-16} dock={c.Dock,-6} {c.Bounds} client {c.ClientSize} in {c.Parent.GetType().Name}"
                    + (c is TableLayoutPanel t ? " rows " + string.Join(",", t.GetRowHeights()) : string.Empty));
            return sb.ToString();
        }

        private static void Walk(Control parent, List<string> faults)
        {
            var visible = parent.Controls.Cast<Control>().Where(c => c.Visible).ToList();
            bool scrolls = parent is ScrollableControl s && s.AutoScroll;
            foreach (var c in visible)
            {
                if (!scrolls && !(parent is TabControl) && !parent.ClientRectangle.Contains(c.Bounds) && c.Width > 0 && c.Height > 0)
                    faults.Add($"cut off by its parent {Name(parent)}: {Name(c)} at {c.Bounds}, parent client {parent.ClientSize}");
                if (c is ButtonBase b && b.Text.Length > 0)
                {
                    var need = TextRenderer.MeasureText(b.Text, b.Font);
                    if (need.Width > b.ClientSize.Width - 4 || need.Height > b.ClientSize.Height)
                        faults.Add($"text larger than its button: {Name(b)} {b.Size}, text {need}");
                }
                if (c is Label l && !l.AutoSize && l.Text.Length > 0 && !l.AutoEllipsis)
                {
                    var need = l.GetPreferredSize(new Size(l.Width, 0));
                    if (need.Height > l.Height + 1) faults.Add($"label text cut off: {Name(l)} {l.Size}, needs {need}");
                }
            }
            if (parent is TableLayoutPanel table) ShiftedRows(table, faults);
            var buttons = visible.OfType<Button>().ToList();
            if (parent is FlowLayoutPanel && buttons.Count > 1 && buttons.Select(b => b.Height).Distinct().Count() > 1)
                faults.Add($"buttons of uneven height in {Name(parent)}: " + string.Join(", ", buttons.Select(b => $"{Short(b.Text)} {b.Size}")));
            foreach (var c in visible) Walk(c, faults);
        }

        /// <summary>
        /// A table places the controls added without a cell in the order they were added, skipping the hidden ones: a hidden
        /// control moves every later one up a cell, onto a row sized for another (the list's stretching row given to the
        /// buttons). Reported for each control laid out in a row of another size than the one it was added for.
        /// </summary>
        private static void ShiftedRows(TableLayoutPanel table, List<string> faults)
        {
            if (table.RowStyles.Count == 0) return;
            var children = table.Controls.Cast<Control>().ToList();
            if (children.Any(c => table.GetCellPosition(c).Row >= 0 || table.GetColumnSpan(c) != 1 || table.GetRowSpan(c) != 1)) return;
            int columns = Math.Max(1, table.ColumnCount);
            for (int i = 0; i < children.Count; i++)
            {
                var c = children[i];
                int meant = i / columns, row = table.GetPositionFromControl(c).Row;
                if (c.Visible && row != meant && RowSize(table, row) != RowSize(table, meant))
                    faults.Add($"row moved by a hidden control in {Name(table)}: {Name(c)} is in row {row} ({RowSize(table, row)}), "
                        + $"added for row {meant} ({RowSize(table, meant)})");
            }
        }

        private static string RowSize(TableLayoutPanel table, int row)
        {
            if (row < 0 || row >= table.RowStyles.Count) return "AutoSize";
            var style = table.RowStyles[row];
            return style.SizeType == SizeType.AutoSize ? "AutoSize" : style.SizeType + " " + style.Height;
        }

        /// <summary>
        /// A sizable window is filled by its layout: a filling control that stops short of the window's edge, or content that
        /// ends well above the window's bottom (an empty band under the last row), is reported.
        /// </summary>
        private static void Unfilled(Form form, List<string> faults)
        {
            if (form.AutoSize || (form.FormBorderStyle != FormBorderStyle.Sizable && form.FormBorderStyle != FormBorderStyle.SizableToolWindow)) return;
            var client = form.ClientRectangle;
            foreach (var fill in form.Controls.Cast<Control>().Where(c => c.Visible && c.Dock == DockStyle.Fill))
                if (fill.Bottom < client.Bottom - form.Padding.Bottom || fill.Right < client.Right - form.Padding.Right)
                    faults.Add($"a filling layout that does not fill its window: {Name(fill)} at {fill.Bounds}, window client {client.Size}");
            int bottom = Shown(form, form).Select(r => r.Bottom).DefaultIfEmpty(0).Max();
            int band = client.Bottom - bottom;
            if (band > Math.Max(form.LogicalToDeviceUnits(40), client.Height / 6))
                faults.Add($"empty band below the last row: content ends at {bottom}, {band} px above the bottom of the window client {client.Size}");
        }

        /// <summary>
        /// Where each control a player sees (not the panels that only place others; a scrolling panel is seen) shows, in the
        /// window's client coordinates, cut to its parents.
        /// </summary>
        private static IEnumerable<Rectangle> Shown(Form form, Control parent)
        {
            foreach (Control c in parent.Controls)
            {
                if (!c.Visible || c.Width <= 0 || c.Height <= 0) continue;
                bool placesOnly = c is TableLayoutPanel || c is FlowLayoutPanel || (c is Panel p && !p.AutoScroll && !(c is TabPage));
                if (!placesOnly)
                {
                    var r = c.Parent.RectangleToScreen(c.Bounds);
                    for (var a = c.Parent; a != null && !(a is Form); a = a.Parent) r.Intersect(a.RectangleToScreen(a.ClientRectangle));
                    r.Intersect(form.RectangleToScreen(form.ClientRectangle));
                    if (r.Width > 0 && r.Height > 0) yield return form.RectangleToClient(r);
                }
                foreach (var d in Shown(form, c)) yield return d;
            }
        }

        private static IEnumerable<Control> All(Control parent)
        {
            foreach (Control c in parent.Controls)
            {
                yield return c;
                foreach (var d in All(c)) yield return d;
            }
        }

        private static string Name(Control c) => c.GetType().Name + (c.Text.Length > 0 && !(c is Form) ? " \"" + Short(c.Text) + "\"" : string.Empty);

        private static string Short(string s)
        {
            s = (s ?? string.Empty).Replace("\r", " ").Replace("\n", " ");
            return s.Length > 32 ? s.Substring(0, 29) + "..." : s;
        }

        private const int DwmwaExtendedFrameBounds = 9;
        private const uint PwRenderFullContent = 2;

        [StructLayout(LayoutKind.Sequential)]
        private struct Rect
        {
            public int Left, Top, Right, Bottom;
            public int Width => Right - Left;
            public int Height => Bottom - Top;
        }

        [DllImport("user32.dll")]
        private static extern bool GetWindowRect(IntPtr hwnd, out Rect rect);

        [DllImport("dwmapi.dll")]
        private static extern int DwmGetWindowAttribute(IntPtr hwnd, int attribute, out Rect value, int size);

        [DllImport("user32.dll")]
        private static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
    }
}
