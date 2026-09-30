using System;
using System.Drawing;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace EternalVR.Launcher
{
    /// <summary>
    /// The mouse wheel over a closed list, or over a number box without the focus, scrolls the page it is on instead of
    /// changing the setting: scrolling down a tab changed every setting that passed under the pointer. An open list and a
    /// number box being typed in still take the wheel.
    /// </summary>
    internal sealed class WheelGuard : IMessageFilter
    {
        private const int WM_MOUSEWHEEL = 0x020A;

        public bool PreFilterMessage(ref Message m)
        {
            if (m.Msg != WM_MOUSEWHEEL) return false;
            // The control under the pointer: with "Scroll inactive windows" off, the message goes to the focused one.
            var control = Control.FromChildHandle(WindowFromPoint(Cursor.Position)) ?? Control.FromChildHandle(m.HWnd);
            var setting = SettingControlOf(control);
            if (setting == null) return false;
            if (setting is ComboBox list && list.DroppedDown) return false;
            if (setting is UpDownBase number && number.ContainsFocus) return false;
            var page = ScrollingParentOf(setting);
            if (page != null) SendMessage(page.Handle, WM_MOUSEWHEEL, m.WParam, m.LParam);
            return true;
        }

        /// <summary>The list or number box that holds <paramref name="c"/> (a number box's text part is a control of its own).</summary>
        private static Control SettingControlOf(Control c)
        {
            for (; c != null; c = c.Parent)
                if (c is ComboBox || c is UpDownBase) return c;
            return null;
        }

        private static Control ScrollingParentOf(Control c)
        {
            for (var p = c.Parent; p != null; p = p.Parent)
                if (p is ScrollableControl s && s.AutoScroll && s.VerticalScroll.Visible) return p;
            return null;
        }

        [DllImport("user32.dll")]
        private static extern IntPtr WindowFromPoint(Point point);

        [DllImport("user32.dll")]
        private static extern IntPtr SendMessage(IntPtr hWnd, int msg, IntPtr wParam, IntPtr lParam);
    }
}
