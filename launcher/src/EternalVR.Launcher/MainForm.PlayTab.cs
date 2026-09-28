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
        private readonly NumericUpDown renderScale = Number(LauncherSettings.MinRenderScale, LauncherSettings.MaxRenderScale, 0.05, 2);
        private readonly ComboBox antiAliasing = Choices(Setting.AntiAliasing);

        /// <summary>A hand-set smoothing that is none of the named steps: shown as a fifth, "Custom" choice.</summary>
        private double customSmoothing = -1;

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
                Row(Setting.AimDot, aimDot, s => aimDot.Checked = s.AimDot, s => s.AimDot = aimDot.Checked));
            var picture = Group("Picture",
                Row(Setting.Resolution, WithUnit(renderScale, "× the headset's size"),
                    s => renderScale.Value = (decimal)LauncherSettings.ClampRenderScale(s.RenderScale),
                    s => s.RenderScale = LauncherSettings.ClampRenderScale((double)renderScale.Value)),
                Row(Setting.AntiAliasing, antiAliasing,
                    s => antiAliasing.SelectedIndex = (int)s.AntiAliasing,
                    s => s.AntiAliasing = (AntiAliasingMode)antiAliasing.SelectedIndex));
            return Page("Play", new Control[] { comfort, body }, new Control[] { controls, picture });
        }

        private void LoadSteadiness(LauncherSettings s)
        {
            var smoothing = LauncherSettings.Clamp(s.AimSmoothing, 0.0, 1.0, LauncherSettings.DefaultAimSmoothing);
            int index = AimSteadiness.IndexOf(smoothing);
            while (steadiness.Items.Count > AimSteadiness.Names.Length) steadiness.Items.RemoveAt(steadiness.Items.Count - 1);
            customSmoothing = index < 0 ? smoothing : -1;
            if (index < 0)
            {
                steadiness.Items.Add("Custom (" + smoothing.ToString("0.00", System.Globalization.CultureInfo.InvariantCulture) + ")");
                index = steadiness.Items.Count - 1;
            }
            steadiness.SelectedIndex = index;
        }

        private void ReadSteadiness(LauncherSettings s)
        {
            int i = steadiness.SelectedIndex;
            if (i >= 0 && i < AimSteadiness.Values.Length) s.AimSmoothing = AimSteadiness.Values[i];
            else if (customSmoothing >= 0) s.AimSmoothing = customSmoothing;
        }
    }
}
