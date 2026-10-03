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
    /// buttons in one row).
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
            return faults;
        }

        public static string Describe(Form form)
        {
            var sb = new StringBuilder();
            sb.AppendLine($"{form.GetType().Name} \"{form.Text}\" dpi {form.DeviceDpi} size {form.Size} client {form.ClientSize} font {form.Font.SizeInPoints}pt/{form.Font.Height}px");
            foreach (var c in All(form).Where(c => c.Visible && (c is Button || c is ProgressBar || c is Label || c is ComboBox || c is TextBox)))
                sb.AppendLine($"  {c.GetType().Name,-12} {Short(c.Text),-34} {c.Bounds} enabled={c.Enabled}");
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
            var buttons = visible.OfType<Button>().ToList();
            if (parent is FlowLayoutPanel && buttons.Count > 1 && buttons.Select(b => b.Height).Distinct().Count() > 1)
                faults.Add($"buttons of uneven height in {Name(parent)}: " + string.Join(", ", buttons.Select(b => $"{Short(b.Text)} {b.Size}")));
            foreach (var c in visible) Walk(c, faults);
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
