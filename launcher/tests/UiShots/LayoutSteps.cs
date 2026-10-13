using System;
using System.Collections.Generic;
using System.Drawing;
using System.Windows.Forms;

namespace EternalVR.Launcher.UiShots
{
    /// <summary>
    /// What a player can do to a window once it is open, each step shot in turn: the scale change of a display of another
    /// scale (DisplayScale.Rescale, as when it opens on or is dragged to one: 96, 120, 144 and 168 DPI, up and down, then back
    /// to the display's own); for a sizable window a resize smaller and larger, maximise and restore; and the controls
    /// editor's notes shown and hidden again. An auto-sized dialog gets none: WinForms sizes it with its frame at the new
    /// scale while Windows still draws the frame at this display's, so a faked change cuts it off where a real one does not
    /// (README.md: --drag on displays of two scales shows it).
    /// </summary>
    internal static class LayoutSteps
    {
        public static IReadOnlyList<Step> For(Form form, bool controlsNotes)
        {
            int own = form.DeviceDpi;
            var steps = new List<Step>();
            if (form.AutoSize) return steps;
            int from = own;
            int n = 0;
            foreach (int dpi in new[] { 96, 144, 96, 120, 168, 96, own })
            {
                if (dpi == from) continue;
                int to = dpi;
                // Numbered: on a 120, 144 or 168 DPI display the same change comes up twice.
                steps.Add(new Step($"scale{++n}-{from}-{to}", f => Rescale(f, to)));
                from = to;
            }
            if (form.FormBorderStyle != FormBorderStyle.Sizable) return steps;
            steps.Add(new Step("resize-smaller", f => f.Size = new Size(Math.Max(f.MinimumSize.Width, f.Width * 3 / 4), Math.Max(f.MinimumSize.Height, f.Height * 3 / 4))));
            steps.Add(new Step("resize-larger", f =>
            {
                var area = Screen.FromControl(f).WorkingArea;
                f.Bounds = new Rectangle(area.Left, area.Top, area.Width * 9 / 10, area.Height * 9 / 10);
            }));
            steps.Add(new Step("maximised", f => f.WindowState = FormWindowState.Maximized));
            steps.Add(new Step("restored", f => f.WindowState = FormWindowState.Normal));
            if (controlsNotes)
            {
                steps.Add(new Step("notes-shown", f => ShowNotes(f, true)));
                steps.Add(new Step("notes-hidden", f => ShowNotes(f, false)));
            }
            return steps;
        }

        /// <summary>
        /// DisplayScale.Rescale to <paramref name="dpi"/>, then the window's client area as large as Windows makes it on a real
        /// change (its own scaled by the change): the frame Windows draws keeps this display's scale, so the scaled window
        /// alone leaves a client area smaller by the frame's difference, which a real display of that scale does not.
        /// </summary>
        private static void Rescale(Form f, int dpi)
        {
            int from = f.DeviceDpi;
            var client = f.ClientSize;
            DisplayScale.Rescale(f, dpi, keepCentre: true);
            if (f.DeviceDpi != dpi) return;
            var centre = new Point(f.Left + f.Width / 2, f.Top + f.Height / 2);
            // By the window's size: WinForms makes a client size into a window size with the frame at the new scale.
            var want = new Size(client.Width * dpi / from, client.Height * dpi / from);
            for (int i = 0; i < 2 && f.ClientSize != want; i++) f.Size += want - f.ClientSize;
            f.Location = new Point(centre.X - f.Width / 2, centre.Y - f.Height / 2);
        }

        /// <summary>The controls editor's thumb-rest note, problems and lines not used, shown with a text or hidden.</summary>
        private static void ShowNotes(Form f, bool shown)
        {
            foreach (var name in new[] { "restNote", "problems", "notUsed" })
            {
                var label = Reflect.Get<Label>(f, name);
                label.Text = shown ? "A note the editor shows here (" + name + "), for the layout check." : string.Empty;
                label.Visible = shown;
            }
        }
    }

    internal sealed class Step
    {
        public Step(string name, Action<Form> apply)
        {
            Name = name;
            Apply = apply;
        }

        public string Name { get; }
        public Action<Form> Apply { get; }
    }
}
