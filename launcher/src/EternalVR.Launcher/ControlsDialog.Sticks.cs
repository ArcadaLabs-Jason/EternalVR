using System.Linq;
using System.Windows.Forms;
using EternalVR.Launcher.Core.Controls;

namespace EternalVR.Launcher
{
    /// <summary>The controls editor's sticks: each stick's role and the turn stick's gestures, laid out as a compass.</summary>
    internal sealed partial class ControlsDialog
    {
        private GroupBox SticksGroup()
        {
            var grid = Grid();
            grid.SuspendLayout();
            grid.Controls.Add(new Label { AutoSize = true });
            grid.Controls.Add(Heading("Role", "Move walks you where the Move toward setting says; Turn turns you and takes the "
                + "stick gestures below. The other stick swaps roles when you pick one it has."));
            grid.SetColumnSpan(grid.Controls[grid.Controls.Count - 1], 3);
            foreach (var h in Hands)
            {
                var name = new Label { AutoSize = true, Anchor = AnchorStyles.Left };
                var role = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = ActionWidth, Anchor = AnchorStyles.Left, Margin = CellMargin };
                foreach (StickRole r in new[] { StickRole.Move, StickRole.Turn, StickRole.None }) role.Items.Add(new RoleItem(r));
                var stick = h;
                role.SelectionChangeCommitted += (s, e) =>
                {
                    if (edit == null || !(role.SelectedItem is RoleItem item)) return;
                    edit.SetStickRole(WeaponHand, stick, item.Role);
                    ShowControls();
                };
                stickRows[h] = (name, role);
                grid.Controls.Add(name);
                grid.Controls.Add(role);
                grid.SetColumnSpan(role, 3);
            }
            for (int i = 0; i < gestures.Length; i++)
            {
                var gesture = AllGestures[i];
                var box = gestures[i];
                box.SelectionChangeCommitted += (s, e) =>
                {
                    var turn = edit?.TurnStick(WeaponHand);
                    if (turn.HasValue) Bind(BindingKey.StickGestureOf(turn.Value, gesture), box);
                };
            }
            // The turn stick's directions as a compass, its name beside the top row, then the down tap and hold.
            const string side = " Binding a side or a diagonal splits the stick by direction (eight parts of 45 degrees when all "
                + "are bound, a direction left free shared by its neighbours), and the stick then turns you only straight left or "
                + "right where those are free.";
            var rows = new[]
            {
                new StickGesture?[] { StickGesture.UpLeft, StickGesture.Up, StickGesture.UpRight },
                new StickGesture?[] { StickGesture.Left, null, StickGesture.Right },
                new StickGesture?[] { StickGesture.DownLeft, StickGesture.Down, StickGesture.DownRight },
                new StickGesture?[] { StickGesture.DownTap, StickGesture.DownHold, null },
            };
            for (int r = 0; r < rows.Length; r++)
            {
                grid.Controls.Add(new Label { AutoSize = true });
                foreach (var g in rows[r])
                    grid.Controls.Add(g.HasValue ? Heading(GestureHeading(g.Value), GestureTip(g.Value) + (ControlNames.SideGestures.Contains(g.Value) ? side : string.Empty))
                        : (Control)new Label { AutoSize = true });
                grid.Controls.Add(r == 0 ? (Control)gestureName : new Label { AutoSize = true });
                foreach (var g in rows[r])
                    grid.Controls.Add(g.HasValue ? (Control)gestures[(int)g.Value] : new Label { AutoSize = true });
            }
            grid.Controls.Add(stickNote);
            grid.SetColumnSpan(stickNote, 4);
            grid.ResumeLayout(true);
            return Box("Sticks", grid);
        }

        private static string GestureHeading(StickGesture gesture)
        {
            var label = ControlNames.GestureLabel(gesture);
            return char.ToUpperInvariant(label[0]) + label.Substring(1);
        }

        private static string GestureTip(StickGesture gesture)
        {
            switch (gesture)
            {
                case StickGesture.DownTap: return "A short push down on the turn stick.";
                case StickGesture.DownHold: return "The turn stick held down: active until it springs back.";
                case StickGesture.Down: return "Pushing the turn stick down: active for as long as it is pushed. It cannot share the stick's down with Down, tap or Down, hold.";
                default: return "Pushing the turn stick " + ControlNames.GestureLabel(gesture) + ": active for as long as it is pushed.";
            }
        }
    }
}
