using System;
using System.Collections.Generic;
using System.Drawing;
using System.Linq;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace EternalVR.Launcher
{
    /// <summary>
    /// A window made at the sign-in display's scale and placed on a display with another one before it is shown (centred on
    /// the display with the pointer, or over its owner there) gets no scale change from Windows, and stays at the sign-in
    /// scale: as large as on a 200 % display on a 125 % one. <see cref="Match"/> gives it the change Windows gives a window
    /// dragged there; <see cref="Follow"/> does so for a dialog when it loads and when it is shown.
    /// </summary>
    internal static class DisplayScale
    {
        /// <summary>Calls <see cref="Match"/> on the dialog's Load and Shown, keeping it centred where it was placed.</summary>
        public static void Follow(Form dialog)
        {
            dialog.Load += (s, e) => Match(dialog, keepCentre: true);
            dialog.Shown += (s, e) => Match(dialog, keepCentre: true);
        }

        /// <summary>Sends the window the scale change of the display it is on, when WinForms laid it out for another.</summary>
        public static void Match(Form form, bool keepCentre)
        {
            if (!form.IsHandleCreated) return;
            int dpi = (int)GetDpiForWindow(form.Handle);
            int from = form.DeviceDpi;
            if (dpi <= 0 || dpi == from) return;
            var centre = new Point(form.Left + form.Width / 2, form.Top + form.Height / 2);
            var size = new Size(form.Width * dpi / from, form.Height * dpi / from);
            var at = keepCentre ? new Point(centre.X - size.Width / 2, centre.Y - size.Height / 2) : form.Location;
            var rect = new Rect { Left = at.X, Top = at.Y, Right = at.X + size.Width, Bottom = at.Y + size.Height };
            // A control with a font of its own (bold text) is not rescaled by the window's change (on a drag Windows tells each
            // control itself): one already made is given its font's change here; one made later (on a tab not shown yet)
            // takes the display's scale, font included, when WinForms makes it.
            var ownFonts = Descendants(form).Where(c => c.IsHandleCreated && !ReferenceEquals(c.Font, c.Parent.Font)).ToList();
            SendMessage(form.Handle, WM_DPICHANGED, (IntPtr)((dpi << 16) | dpi), ref rect);
            foreach (var c in ownFonts)
            {
                var f = c.Font;
                c.Font = new Font(f.FontFamily, f.Size * dpi / from, f.Style, f.Unit, f.GdiCharSet, f.GdiVerticalFont);
            }
            if (!keepCentre) return;
            // An auto-sized dialog takes its own size after the change: centred again on the same point.
            form.PerformLayout();
            form.Location = new Point(centre.X - form.Width / 2, centre.Y - form.Height / 2);
        }

        /// <summary>Every control under <paramref name="parent"/>, each before its own children.</summary>
        private static IEnumerable<Control> Descendants(Control parent)
        {
            foreach (Control c in parent.Controls)
            {
                yield return c;
                foreach (var d in Descendants(c)) yield return d;
            }
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
    }
}
