using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Windows.Forms;
using EternalVR.Launcher.Core.Controls;
using EternalVR.Launcher.Core.Settings;

namespace EternalVR.Launcher
{
    /// <summary>
    /// Edit controls: which button does what, for one kind of controller and one weapon hand at a time. A thin view over
    /// <see cref="ControlsEdit"/>, which reads, checks and saves the player's controller file.
    /// </summary>
    internal sealed class ControlsDialog : Form
    {
        /// <summary>A list's width at 96 DPI: the longest action, "Quick switch (last weapon)", shows whole.</summary>
        private const int ActionWidth = 190;
        /// <summary>Room on the right of each list for the problem icon (<see cref="errors"/>).</summary>
        private static readonly Padding CellMargin = new Padding(3, 3, 20, 3);
        private static readonly ButtonInput[] AllInputs = (ButtonInput[])Enum.GetValues(typeof(ButtonInput));
        private static readonly PressKind[] AllPresses = (PressKind[])Enum.GetValues(typeof(PressKind));
        private static readonly StickGesture[] AllGestures = (StickGesture[])Enum.GetValues(typeof(StickGesture));
        private static readonly ControlHand[] Hands = { ControlHand.Left, ControlHand.Right };

        private readonly ControlsFolder folder;
        private readonly Log log;
        private readonly ToolTip tips = new ToolTip { AutoPopDelay = 30000, InitialDelay = 400, ReshowDelay = 100 };
        private readonly ErrorProvider errors = new ErrorProvider { BlinkStyle = ErrorBlinkStyle.NeverBlink };
        private readonly ComboBox family = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 240 };
        private readonly ComboBox hand = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 180 };
        private readonly Label fileState = new Label { AutoSize = true, Margin = new Padding(3, 6, 3, 6) };
        private readonly Label problems = new Label { AutoSize = true, ForeColor = Color.Firebrick, Margin = new Padding(3, 6, 3, 6) };
        private readonly Button save = new Button { Text = "Save", Width = 90, Height = 30 };
        private readonly Button close = new Button { Text = "Close", Width = 90, Height = 30, DialogResult = DialogResult.Cancel };
        private readonly Button reset = new Button { Text = "Reset to defaults", Width = 130, Height = 30 };
        private readonly Button openFolder = new Button { Text = "Open folder", Width = 100, Height = 30 };

        /// <summary>One row per hand and button, shown for the buttons the controller has.</summary>
        private readonly Dictionary<(ControlHand, ButtonInput), (Label Name, ComboBox[] Presses)> buttonRows = new Dictionary<(ControlHand, ButtonInput), (Label, ComboBox[])>();
        private readonly Dictionary<ControlHand, (Label Name, ComboBox Role)> stickRows = new Dictionary<ControlHand, (Label, ComboBox)>();
        private readonly Label gestureName = new Label { AutoSize = true, Anchor = AnchorStyles.Left };
        private readonly ComboBox[] gestures = AllGestures.Select(g => ActionList()).ToArray();

        /// <summary>The button and stick grids, held still while their rows are made and refilled.</summary>
        private readonly List<TableLayoutPanel> grids = new List<TableLayoutPanel>();
        /// <summary>From the editor's creation to the first show, for the log (the editor once took about 8 s to open).</summary>
        private readonly Stopwatch opening = Stopwatch.StartNew();

        private IReadOnlyList<ControllerFamily> families;
        private ControlsEdit edit;
        private int familyIndex = -1;
        private bool filling;

        /// <param name="lastProfile">The interaction profile of the controllers the last game reported, or null.</param>
        public ControlsDialog(ControlsFolder folder, Handedness weaponHand, string lastProfile, Log log)
        {
            this.folder = folder;
            this.log = log;
            Text = "Edit controls: " + (folder.Profile == null ? "no VR settings profile" : "VR settings profile " + folder.Profile);
            SuspendLayout();
            AutoScaleDimensions = new SizeF(96F, 96F);
            AutoScaleMode = AutoScaleMode.Dpi;
            StartPosition = FormStartPosition.CenterParent;
            MinimizeBox = false;
            ShowInTaskbar = false;
            // Fits a 1080p display at 150 %, as the main window does; the lists scroll when the window is smaller.
            Width = 860;
            Height = 660;
            MinimumSize = new Size(560, 420);
            Icon = Branding.WindowIcon() ?? Icon;
            BuildLayout();
            Font = MainForm.UiFont();
            MainForm.FitListHeights(this);
            DisplayScale.Follow(this);
            ResumeLayout(false);
            PerformLayout();

            foreach (var c in SettingTexts.For(Setting.WeaponHand).Choices) hand.Items.Add(c);
            hand.SelectedIndex = Math.Max(0, Math.Min((int)weaponHand, hand.Items.Count - 1));
            families = folder.Families();
            foreach (var f in families) family.Items.Add(f);
            // The controllers of the last game (the game uses their file), else the kind the player has a file for, else
            // the first.
            var mine = families.FirstOrDefault(f => lastProfile != null && f.ProfilePath == lastProfile)
                ?? families.FirstOrDefault(f => folder.PlayerFileFor(f.ProfilePath) != null);
            if (families.Count > 0) SelectFamily(mine != null ? families.ToList().IndexOf(mine) : 0);

            family.SelectionChangeCommitted += (s, e) => SelectFamily(family.SelectedIndex);
            hand.SelectionChangeCommitted += (s, e) => ShowControls();
            save.Click += (s, e) => Save();
            reset.Click += (s, e) => { edit?.ResetToDefaults(); ShowControls(); };
            openFolder.Click += (s, e) => Process.Start("explorer.exe", "\"" + folder.Dir + "\"");
            FormClosing += (s, e) => { if (!KeepOrDiscardChanges()) e.Cancel = true; };
            Resize += (s, e) => WrapProblems();
            Load += (s, e) => { FitToScreen(); WrapProblems(); };
            log.Info("controls editor: made in " + opening.ElapsedMilliseconds + " ms");
            Shown += (s, e) => log.Info("controls editor: shown " + opening.ElapsedMilliseconds + " ms after it was asked for");
            Disposed += (s, e) => { errors.Dispose(); tips.Dispose(); };
        }

        private Handedness WeaponHand => (Handedness)Math.Max(0, hand.SelectedIndex);
        private string Section => ControlNames.MapSection(WeaponHand);

        // ---- Layout ----

        private void BuildLayout()
        {
            var root = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, Padding = new Padding(8) };
            root.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            root.RowStyles.Add(new RowStyle(SizeType.AutoSize));

            var pickers = new TableLayoutPanel { AutoSize = true, ColumnCount = 4, Dock = DockStyle.Fill, Margin = Padding.Empty };
            for (int i = 0; i < 4; i++) pickers.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            pickers.Controls.AddRange(new Control[] { Caption("Controllers"), family, Caption("Weapon hand"), hand });
            tips.SetToolTip(family, "The kind of controller to change the controls of. Each kind has its own controls file, and "
                + "the game uses only the file of the controllers you play with. The list starts on the controllers of your last game.");
            tips.SetToolTip(hand, "Which of the three maps to show: each Weapon hand setting has its own. The game uses the one "
                + "for the Weapon hand chosen on the Play tab.");

            var scroll = new Panel { Dock = DockStyle.Fill, AutoScroll = true };
            var groups = new TableLayoutPanel { Dock = DockStyle.Top, AutoSize = true, ColumnCount = 1 };
            groups.Controls.Add(ButtonsGroup());
            groups.Controls.Add(SticksGroup());
            scroll.Controls.Add(groups);

            var left = new FlowLayoutPanel { AutoSize = true, WrapContents = false, Margin = Padding.Empty };
            left.Controls.AddRange(new Control[] { openFolder, reset });
            var right = new FlowLayoutPanel { AutoSize = true, WrapContents = false, FlowDirection = FlowDirection.RightToLeft, Dock = DockStyle.Right, Margin = Padding.Empty };
            right.Controls.AddRange(new Control[] { close, save });
            var bottom = new TableLayoutPanel { AutoSize = true, ColumnCount = 2, Dock = DockStyle.Fill, Margin = Padding.Empty };
            bottom.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            bottom.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            bottom.Controls.Add(left, 0, 0);
            bottom.Controls.Add(right, 1, 0);
            tips.SetToolTip(openFolder, "Opens your controls folder, to edit a controls file by hand or delete it.");
            tips.SetToolTip(reset, "Puts back the built-in controls for these controllers, for every weapon hand. Nothing changes "
                + "until you press Save.");
            tips.SetToolTip(save, "Saves these controls to your controls folder. The game reads them the next time it starts.");

            root.Controls.Add(pickers);
            root.Controls.Add(fileState);
            root.Controls.Add(scroll);
            root.Controls.Add(problems);
            root.Controls.Add(bottom);
            Controls.Add(root);
            CancelButton = close;
        }

        private GroupBox ButtonsGroup()
        {
            var grid = Grid();
            grid.SuspendLayout();
            grid.Controls.Add(new Label { AutoSize = true });
            foreach (var press in AllPresses) grid.Controls.Add(Heading(PressHeading(press), PressTip(press)));
            foreach (var h in Hands)
                foreach (var input in AllInputs)
                {
                    var name = new Label { AutoSize = true, Anchor = AnchorStyles.Left };
                    var cells = AllPresses.Select(press =>
                    {
                        var box = ActionList();
                        var key = BindingKey.Button(h, input, press);
                        box.SelectionChangeCommitted += (s, e) => Bind(key, box);
                        return box;
                    }).ToArray();
                    buttonRows[(h, input)] = (name, cells);
                    grid.Controls.Add(name);
                    grid.Controls.AddRange(cells);
                }
            grid.ResumeLayout(true);
            return Box("Buttons", grid);
        }

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
            grid.Controls.Add(new Label { AutoSize = true });
            grid.Controls.Add(Heading("Up", "Pushing the turn stick up: active for as long as it is pushed."));
            grid.Controls.Add(Heading("Down, tap", "A short push down on the turn stick."));
            grid.Controls.Add(Heading("Down, hold", "The turn stick held down: active until it springs back."));
            grid.Controls.Add(gestureName);
            for (int i = 0; i < gestures.Length; i++)
            {
                var gesture = AllGestures[i];
                var box = gestures[i];
                box.SelectionChangeCommitted += (s, e) =>
                {
                    var turn = edit?.TurnStick(WeaponHand);
                    if (turn.HasValue) Bind(BindingKey.StickGestureOf(turn.Value, gesture), box);
                };
                grid.Controls.Add(box);
            }
            grid.ResumeLayout(true);
            return Box("Sticks", grid);
        }

        private TableLayoutPanel Grid()
        {
            var grid = new TableLayoutPanel { Dock = DockStyle.Top, AutoSize = true, ColumnCount = 4 };
            for (int i = 0; i < 4; i++) grid.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            grids.Add(grid);
            return grid;
        }

        private static GroupBox Box(string title, Control content)
        {
            var box = new GroupBox { Text = title, Dock = DockStyle.Fill, AutoSize = true, Padding = new Padding(6, 4, 6, 4) };
            box.Controls.Add(content);
            return box;
        }

        private Label Heading(string text, string tip)
        {
            var label = new Label { Text = text, AutoSize = true, Anchor = AnchorStyles.Left, Font = MainForm.UiFont(FontStyle.Bold) };
            tips.SetToolTip(label, tip);
            return label;
        }

        private static Label Caption(string text) =>
            new Label { Text = text, AutoSize = true, Anchor = AnchorStyles.Left, Margin = new Padding(3, 3, 6, 3) };

        private static string PressHeading(PressKind press) => press == PressKind.Press ? "Press" : press == PressKind.Tap ? "Tap" : "Hold";

        private static string PressTip(PressKind press) =>
            press == PressKind.Press ? "Active for as long as the button is down. A button with a press cannot also have a tap or a hold."
            : press == PressKind.Tap ? "A short press, let go within a quarter of a second. A button can have a tap and a hold."
            : "The button held for a quarter of a second: active until you let go.";

        private static ComboBox ActionList()
        {
            var box = new ComboBox
            {
                DropDownStyle = ComboBoxStyle.DropDownList, Width = ActionWidth, DropDownWidth = ActionWidth + 40, Anchor = AnchorStyles.Left,
                MaxDropDownItems = 16, Margin = CellMargin,
            };
            box.Enter += (s, e) => ShowAllActions(box);
            box.DropDown += (s, e) => ShowAllActions(box);
            return box;
        }

        // ---- Showing the controls ----

        /// <summary>An action in a list: null for a free button; an unknown name from the file is shown as it is.</summary>
        private sealed class ActionItem
        {
            public ActionItem(string name, string label) { Name = name; Label = label; }
            public string Name { get; }
            public string Label { get; }
            public override string ToString() => Label;
        }

        private sealed class RoleItem
        {
            public RoleItem(StickRole role) { Role = role; }
            public StickRole Role { get; }
            public override string ToString() => ControlNames.RoleLabel(Role);
        }

        private const string FreeLabel = "(none)";

        /// <summary>The free item and every action, shared by all the lists.</summary>
        private static readonly object[] KnownActions = new object[] { new ActionItem(null, FreeLabel) }
            .Concat(ControlNames.Actions.Select(a => (object)new ActionItem(a.Name, a.Label))).ToArray();

        private static ActionItem UnknownItem(string name) => new ActionItem(name, "'" + name + "' (not an action)");

        /// <summary>
        /// A list holds only the action it shows until the player reaches it (the editor's ~70 full lists took about a second
        /// to make when it opened); then it holds every action.
        /// </summary>
        private static bool HoldsAll(ComboBox box) => box.Items.Count > 1;

        /// <summary>
        /// Fills a list with every action, plus <paramref name="unknown"/> (a value the file holds that is not an action) so
        /// it can be shown; a full list is only refilled when that extra item changes.
        /// </summary>
        private static void FillActions(ComboBox box, string unknown)
        {
            var extra = box.Items.Count > ControlNames.Actions.Count + 1 ? ((ActionItem)box.Items[box.Items.Count - 1]).Name : null;
            if (HoldsAll(box) && extra == unknown) return;
            box.BeginUpdate();
            box.Items.Clear();
            box.Items.AddRange(KnownActions);
            if (unknown != null) box.Items.Add(UnknownItem(unknown));
            box.EndUpdate();
        }

        /// <summary>Gives a list every action when the player first reaches it, keeping the action it shows.</summary>
        private static void ShowAllActions(ComboBox box)
        {
            if (HoldsAll(box)) return;
            var shown = box.Items.Count == 1 ? (ActionItem)box.Items[0] : null;
            FillActions(box, shown?.Name != null && ControlNames.FindAction(shown.Name) == null ? shown.Name : null);
            if (shown != null) SelectAction(box, shown.Name);
        }

        private static void ShowAction(ComboBox box, string value)
        {
            var name = value == ControlNames.Unbound ? null : value;
            var known = name == null || ControlNames.FindAction(name) != null;
            if (HoldsAll(box)) FillActions(box, known ? null : name);
            else if (box.Items.Count == 0 || ((ActionItem)box.Items[0]).Name != name)
            {
                box.BeginUpdate();
                box.Items.Clear();
                box.Items.Add(known ? KnownActions.Cast<ActionItem>().First(i => i.Name == name) : UnknownItem(name));
                box.EndUpdate();
            }
            SelectAction(box, name);
        }

        private static void SelectAction(ComboBox box, string name) =>
            box.SelectedIndex = box.Items.Cast<ActionItem>().ToList().FindIndex(i => i.Name == name);

        private void SelectFamily(int index)
        {
            if (index == familyIndex || index < 0 || index >= families.Count) return;
            if (!KeepOrDiscardChanges())
            {
                family.SelectedIndex = familyIndex;
                return;
            }
            ControlsEdit opened;
            try { opened = ControlsEdit.Open(folder, families[index]); }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException)
            {
                log.Error("reading the controls of " + families[index].Name + " failed: " + e.Message);
                MessageBox.Show(this, "The controls of " + families[index].Name + " could not be read:\n\n" + e.Message, Text,
                    MessageBoxButtons.OK, MessageBoxIcon.Warning);
                family.SelectedIndex = familyIndex;
                return;
            }
            edit = opened;
            familyIndex = index;
            family.SelectedIndex = index;
            ShowControls();
        }

        /// <summary>Shows the map of the chosen weapon hand, and every problem the layer would find in the file.</summary>
        private void ShowControls()
        {
            if (edit == null || filling) return;
            filling = true;
            SuspendLayout();
            foreach (var g in grids) g.SuspendLayout();
            try
            {
                var weaponHand = WeaponHand;
                var inputs = new HashSet<(ControlHand, ButtonInput)>(edit.Inputs(weaponHand));
                foreach (var row in buttonRows)
                {
                    bool shown = inputs.Contains(row.Key);
                    row.Value.Name.Visible = shown;
                    row.Value.Name.Text = edit.InputName(row.Key.Item1, row.Key.Item2);
                    for (int p = 0; p < AllPresses.Length; p++)
                    {
                        var box = row.Value.Presses[p];
                        box.Visible = shown;
                        ShowAction(box, edit.Get(weaponHand, BindingKey.Button(row.Key.Item1, row.Key.Item2, AllPresses[p])));
                    }
                }
                foreach (var row in stickRows)
                {
                    row.Value.Name.Text = edit.StickName(row.Key);
                    var role = edit.Role(weaponHand, row.Key);
                    row.Value.Role.SelectedIndex = row.Value.Role.Items.Cast<RoleItem>().ToList().FindIndex(i => i.Role == role);
                }
                var turn = edit.TurnStick(weaponHand);
                gestureName.Text = turn.HasValue ? edit.StickName(turn.Value) + " (turn)" : "No turn stick";
                for (int i = 0; i < gestures.Length; i++)
                {
                    gestures[i].Enabled = turn.HasValue;
                    ShowAction(gestures[i], turn.HasValue ? edit.Get(weaponHand, BindingKey.StickGestureOf(turn.Value, AllGestures[i])) : null);
                }
                ShowState();
            }
            finally
            {
                foreach (var g in grids) g.ResumeLayout(true);
                ResumeLayout(true);
                filling = false;
            }
        }

        private void ShowState()
        {
            var file = edit.PlayerFile == null ? "Built-in controls" : "Your file: " + Path.GetFileName(edit.PlayerFile);
            fileState.Text = file + (edit.IsModified ? " (changed, not saved yet)" : string.Empty);

            var issues = edit.Check();
            errors.Clear();
            var lines = new List<string>();
            foreach (var issue in issues)
            {
                bool here = issue.Section == Section;
                var box = here ? BoxOf(issue.Key) : null;
                if (box != null && box.Visible && string.IsNullOrEmpty(errors.GetError(box))) errors.SetError(box, issue.Message);
                lines.Add("- " + (issue.Section == null || here ? string.Empty : MapName(issue.Section) + ": ") + issue.Message);
            }
            problems.Text = lines.Count == 0 ? string.Empty
                : "The game would not use these controls. Fix " + (lines.Count == 1 ? "this" : "these") + " to save:\n" + string.Join("\n", lines);
            problems.Visible = lines.Count > 0;
            save.Enabled = lines.Count == 0;
            WrapProblems();
        }

        /// <summary>"Left (buttons swapped)" for "map.left_button_swap": the Weapon hand choice the map belongs to.</summary>
        private static string MapName(string section)
        {
            var choices = SettingTexts.For(Setting.WeaponHand).Choices;
            for (int i = 0; i < choices.Count; i++)
                if (ControlNames.MapSection((Handedness)i) == section) return choices[i];
            return "[" + section + "]";
        }

        /// <summary>The list showing <paramref name="key"/> in the map on screen, or null.</summary>
        private ComboBox BoxOf(string key)
        {
            if (!BindingKey.TryParse(key, out var k)) return null;
            switch (k.Kind)
            {
                case BindingKeyKind.Button: return buttonRows[(k.Hand, k.Input)].Presses[(int)k.Press];
                case BindingKeyKind.StickRole: return stickRows[k.Hand].Role;
                case BindingKeyKind.Gesture: return edit.TurnStick(WeaponHand) == k.Hand ? gestures[(int)k.Gesture] : stickRows[k.Hand].Role;
                default: return null;
            }
        }

        /// <summary>A scaled window larger than the screen's working area is shrunk to it.</summary>
        private void FitToScreen()
        {
            var area = Screen.FromControl(this).WorkingArea;
            Width = Math.Min(Width, area.Width);
            Height = Math.Min(Height, area.Height);
        }

        private void WrapProblems()
        {
            int width = ClientSize.Width - Padding.Horizontal - 32;
            if (width > 100) problems.MaximumSize = new Size(width, 0);
        }

        // ---- Changing and saving ----

        private void Bind(BindingKey key, ComboBox box)
        {
            if (edit == null || filling || !(box.SelectedItem is ActionItem item)) return;
            edit.SetAction(WeaponHand, key, item.Name);
            ShowControls();
        }

        /// <summary>Saves; false when the controls could not be saved (the player has been told why).</summary>
        private bool Save()
        {
            if (edit == null) return true;
            var issues = edit.Check();
            if (issues.Count > 0)
            {
                MessageBox.Show(this, "These controls cannot be saved yet: the game would not use them.\n\n" + issues[0].Message, Text,
                    MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return false;
            }
            ControlsSaveResult result;
            try { result = edit.Save(); }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException || e is InvalidOperationException)
            {
                log.Error("saving the controls of " + edit.Family.Name + " failed: " + e.Message);
                MessageBox.Show(this, "The controls could not be saved:\n\n" + e.Message, Text, MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return false;
            }
            switch (result)
            {
                case ControlsSaveResult.Saved:
                    log.Info("controls of " + edit.Family.Name + " saved to " + edit.PlayerFile);
                    break;
                case ControlsSaveResult.Removed:
                    log.Info("controls of " + edit.Family.Name + " are the built-in ones again; the player's file was removed");
                    break;
            }
            ShowControls();
            return true;
        }

        /// <summary>Asks what to do with unsaved changes. False: stay on the controls shown.</summary>
        private bool KeepOrDiscardChanges()
        {
            if (edit == null || !edit.IsModified) return true;
            var answer = MessageBox.Show(this, "Save the changes to the " + edit.Family.Name + " controls?", Text,
                MessageBoxButtons.YesNoCancel, MessageBoxIcon.Question);
            if (answer == DialogResult.Cancel) return false;
            return answer == DialogResult.No || Save();
        }
    }
}
