using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Windows.Forms;
using EternalVR.Launcher.Core.Settings;

namespace EternalVR.Launcher
{
    /// <summary>The Play tab: comfort, body, controls and picture, the settings most players change.</summary>
    public sealed partial class MainForm
    {
        private readonly ComboBox turn = Choices(Setting.Turning);
        private readonly NumericUpDown turnRate = Number(LauncherSettings.MinTurnRate, LauncherSettings.MaxTurnRate, 10, 0);
        private readonly NumericUpDown snapDegrees = Number(LauncherSettings.MinSnapDegrees, LauncherSettings.MaxSnapDegrees, 15, 0);
        private readonly ComboBox vignette = Choices(Setting.Vignette);
        private readonly CheckBox bodyFollow = new CheckBox { AutoSize = true };
        private readonly CheckBox recenter = new CheckBox { AutoSize = true };
        private readonly CheckBox skipCinematics = new CheckBox { AutoSize = true };
        private readonly ComboBox posture = Choices(Setting.PlayPosition);
        private readonly ComboBox height = Choices(Setting.EyeHeight);
        private readonly ComboBox aim = Choices(Setting.AimWith);
        private readonly ComboBox hand = Choices(Setting.WeaponHand);
        private readonly ComboBox locomotion = Choices(Setting.MoveToward);
        private readonly ComboBox xButton = Choices(Setting.XButton);
        private readonly ComboBox steadiness = Choices(Setting.AimSteadiness);
        private readonly CheckBox aimDot = new CheckBox { AutoSize = true };
        private readonly ComboBox vibration = Choices(Setting.Vibration);
        /// <summary>Whether the controls folder holds maps of the player's own (<see cref="ShowControlsState"/>).</summary>
        private readonly Label controlsState = Caption(string.Empty);
        private readonly NumericUpDown renderScale = Number(LauncherSettings.MinRenderScale, LauncherSettings.MaxRenderScale, 0.05, 2);
        private readonly ComboBox antiAliasing = Choices(Setting.AntiAliasing, 200);

        /// <summary>A hand-set smoothing that is none of the named steps: shown as a fifth, "Custom" choice.</summary>
        private double customSmoothing = -1;
        /// <summary>A hand-set vibration strength that is none of the named steps, shown the same way.</summary>
        private double customVibration = -1;

        private TabPage PlayTab()
        {
            var comfort = Group("Comfort",
                Row(Setting.Turning, turn,
                    s => turn.SelectedIndex = (int)s.Turn,
                    s => s.Turn = (TurnMode)turn.SelectedIndex),
                Row(Setting.TurnSpeed, WithUnit(turnRate, "°/s"),
                    s => turnRate.Value = (decimal)LauncherSettings.ClampTurnRate(s.TurnRate),
                    s => s.TurnRate = (double)turnRate.Value),
                Row(Setting.SnapAngle, WithUnit(snapDegrees, "°"),
                    s => snapDegrees.Value = (decimal)LauncherSettings.ClampSnapDegrees(s.SnapDegrees),
                    s => s.SnapDegrees = (double)snapDegrees.Value),
                Row(Setting.Vignette, vignette,
                    s => vignette.SelectedIndex = (int)s.Vignette,
                    s => s.Vignette = (VignetteMode)vignette.SelectedIndex),
                Row(Setting.WalkInRoom, bodyFollow, s => bodyFollow.Checked = s.BodyFollow, s => s.BodyFollow = bodyFollow.Checked),
                Row(Setting.RecenterHold, recenter, s => recenter.Checked = s.RecenterLongPress, s => s.RecenterLongPress = recenter.Checked),
                Row(Setting.SkipCutscenes, skipCinematics, s => skipCinematics.Checked = s.SkipCinematics, s => s.SkipCinematics = skipCinematics.Checked));
            var body = Group("Body",
                Row(Setting.PlayPosition, posture,
                    s => posture.SelectedIndex = (int)s.Posture,
                    s => s.Posture = (PostureMode)posture.SelectedIndex),
                Row(Setting.EyeHeight, height,
                    s => height.SelectedIndex = (int)s.Height,
                    s => s.Height = (HeightMode)height.SelectedIndex));
            var controls = Group("Controls",
                Row(Setting.AimWith, aim,
                    s => aim.SelectedIndex = (int)s.Aim,
                    s => s.Aim = (AimMode)aim.SelectedIndex),
                Row(Setting.WeaponHand, hand,
                    s => hand.SelectedIndex = (int)s.Hand,
                    s => s.Hand = (Handedness)hand.SelectedIndex),
                Row(Setting.MoveToward, locomotion,
                    s => locomotion.SelectedIndex = (int)s.Locomotion,
                    s => s.Locomotion = (LocomotionMode)locomotion.SelectedIndex),
                Row(Setting.XButton, xButton,
                    s => xButton.SelectedIndex = (int)s.Dossier,
                    s => s.Dossier = (DossierPress)xButton.SelectedIndex),
                Row(Setting.AimSteadiness, steadiness, LoadSteadiness, ReadSteadiness),
                Row(Setting.AimDot, aimDot, s => aimDot.Checked = s.AimDot, s => s.AimDot = aimDot.Checked),
                Row(Setting.Vibration, vibration, LoadVibration, ReadVibration),
                // Nothing to save: the button opens the controls folder, the caption says what it holds.
                Row(Setting.ButtonLayout, ControlsRow(), s => ShowControlsState(), s => { }));
            var picture = Group("Picture",
                Row(Setting.Resolution, WithUnit(renderScale, "× the headset's size"),
                    s => renderScale.Value = (decimal)LauncherSettings.ClampRenderScale(s.RenderScale),
                    s => s.RenderScale = LauncherSettings.ClampRenderScale((double)renderScale.Value)),
                Row(Setting.AntiAliasing, antiAliasing,
                    s => antiAliasing.SelectedIndex = LauncherSettings.AntiAliasingChoice(s),
                    s => LauncherSettings.SetAntiAliasingChoice(s, antiAliasing.SelectedIndex)));
            var page = Page("Play", new Control[] { comfort, body }, new Control[] { controls, picture });
            // The player edits the folder in Explorer: looked at again on coming back to the window or the tab.
            tabs.Selected += (s, e) => { if (e.TabPage == page) ShowControlsState(); };
            Activated += (s, e) => ShowControlsState();
            return page;
        }

        private FlowLayoutPanel ControlsRow()
        {
            var edit = new Button { Text = "Edit controls...", Width = 110, Height = 26 };
            edit.Click += (s, e) => EditControls();
            // Wraps to a second line rather than widening the column.
            controlsState.MaximumSize = new Size(130, 0);
            var row = new FlowLayoutPanel { AutoSize = true, Margin = Padding.Empty, WrapContents = false };
            row.Controls.AddRange(new Control[] { edit, controlsState });
            return row;
        }

        private void ShowControlsState()
        {
            controlsState.Text = ctx.Controls.HasPlayerMaps ? "Using your own controls" : "Built-in controls";
        }

        /// <summary>Refreshes the controls folder (its README and the copy of the built-in maps), then opens it in Explorer.</summary>
        private void EditControls()
        {
            var controls = ctx.Controls;
            try
            {
                controls.Prepare(ctx.DefaultControlsDir);
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException)
            {
                ctx.Log.Error("preparing the controls folder failed: " + e.Message);
                MessageBox.Show(this, "The controls folder could not be prepared:\n\n" + e.Message, "Edit controls",
                    MessageBoxButtons.OK, MessageBoxIcon.Warning);
                ShowControlsState();
                return;
            }
            ctx.Log.Info("controls folder ready: " + controls.Dir);
            Process.Start("explorer.exe", "\"" + controls.Dir + "\"");
            ShowControlsState();
        }

        private void LoadSteadiness(LauncherSettings s)
        {
            var smoothing = LauncherSettings.Clamp(s.AimSmoothing, 0.0, 1.0, LauncherSettings.DefaultAimSmoothing);
            customSmoothing = SelectStep(steadiness, AimSteadiness.Names.Length, AimSteadiness.IndexOf(smoothing), smoothing);
        }

        private void ReadSteadiness(LauncherSettings s)
        {
            int i = steadiness.SelectedIndex;
            if (i >= 0 && i < AimSteadiness.Values.Length) s.AimSmoothing = AimSteadiness.Values[i];
            else if (customSmoothing >= 0) s.AimSmoothing = customSmoothing;
        }

        private void LoadVibration(LauncherSettings s)
        {
            var strength = LauncherSettings.Clamp(s.Vibration, 0.0, 1.0, LauncherSettings.DefaultVibration);
            customVibration = SelectStep(vibration, Vibration.Names.Length, Vibration.IndexOf(strength), strength);
        }

        private void ReadVibration(LauncherSettings s)
        {
            int i = vibration.SelectedIndex;
            if (i >= 0 && i < Vibration.Values.Length) s.Vibration = Vibration.Values[i];
            else if (customVibration >= 0) s.Vibration = customVibration;
        }

        /// <summary>Selects step <paramref name="index"/> of a list of named steps, or a "Custom" choice added after them for a
        /// <paramref name="value"/> that is none of them (-1). Returns that custom value, or -1.</summary>
        private static double SelectStep(ComboBox list, int steps, int index, double value)
        {
            while (list.Items.Count > steps) list.Items.RemoveAt(list.Items.Count - 1);
            if (index < 0)
            {
                list.Items.Add("Custom (" + value.ToString("0.00", System.Globalization.CultureInfo.InvariantCulture) + ")");
                list.SelectedIndex = list.Items.Count - 1;
                return value;
            }
            list.SelectedIndex = index;
            return -1;
        }
    }
}
