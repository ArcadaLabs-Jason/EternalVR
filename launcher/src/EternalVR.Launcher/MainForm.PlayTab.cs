using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Windows.Forms;
using EternalVR.Launcher.Core;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;

namespace EternalVR.Launcher
{
    /// <summary>The Play tab: the headset found, then comfort, body, controls, picture and DLSS, the settings most players change.</summary>
    public sealed partial class MainForm
    {
        private readonly ComboBox turn = Choices(Setting.Turning);
        private readonly NumericUpDown turnRate = Number(LauncherSettings.MinTurnRate, LauncherSettings.MaxTurnRate, 10, 0);
        private readonly NumericUpDown snapDegrees = Number(LauncherSettings.MinSnapDegrees, LauncherSettings.MaxSnapDegrees, 15, 0);
        private readonly ComboBox vignette = Choices(Setting.Vignette);
        private readonly ComboBox gloryKills = Choices(Setting.GloryKills);
        private readonly CheckBox bodyFollow = new CheckBox { AutoSize = true };
        private readonly CheckBox headFade = new CheckBox { AutoSize = true };
        private readonly CheckBox recenter = new CheckBox { AutoSize = true };
        private readonly CheckBox skipCinematics = new CheckBox { AutoSize = true };
        private readonly ComboBox posture = Choices(Setting.PlayPosition);
        private readonly ComboBox height = Choices(Setting.EyeHeight);
        private readonly CheckBox throwGesture = new CheckBox { AutoSize = true };
        private readonly CheckBox swingGesture = new CheckBox { AutoSize = true };
        private readonly CheckBox handsJump = new CheckBox { AutoSize = true };
        private readonly ComboBox aim = Choices(Setting.AimWith);
        private readonly ComboBox revenantAim = Choices(Setting.RevenantAimWith);
        private readonly ComboBox meleeAim = Choices(Setting.MeleeAimWith);
        private readonly ComboBox equipmentAim = Choices(Setting.EquipmentAimWith);
        private readonly ComboBox hand = Choices(Setting.WeaponHand);
        private readonly ComboBox locomotion = Choices(Setting.MoveToward);
        private readonly ComboBox xButton = Choices(Setting.XButton);
        private readonly ComboBox mapSticks = Choices(Setting.DossierMapSticks);
        private readonly ComboBox wheel = Choices(Setting.WeaponWheel);
        private readonly ComboBox thumbRest = Choices(Setting.ThumbRestWheel);
        private readonly ComboBox thumbRestPicks = Choices(Setting.ThumbRestPicks);
        private readonly CheckBox thumbRestFaceTouch = new CheckBox { AutoSize = true };
        private readonly CheckBox thumbRestSlowdown = new CheckBox { AutoSize = true };
        private readonly ComboBox steadiness = Choices(Setting.AimSteadiness);
        private readonly CheckBox aimDot = new CheckBox { AutoSize = true };
        private readonly CheckBox showArms = new CheckBox { AutoSize = true };
        private readonly ComboBox vibration = Choices(Setting.Vibration);
        private readonly ComboBox punchSpeed = Choices(Setting.PunchSpeed);
        private readonly ComboBox holdTime = Choices(Setting.HoldTime);
        private readonly CheckBox bhaptics = new CheckBox { AutoSize = true };
        /// <summary>Whether the controls folder holds maps of the player's own (<see cref="ShowControlsState"/>).</summary>
        private readonly Label controlsState = Caption(string.Empty);
        private readonly NumericUpDown renderScale = Number(LauncherSettings.MinRenderScale, LauncherSettings.MaxRenderScale, 0.05, 2);
        /// <summary>The size each eye renders at, from the headset's last answer (<see cref="ShowHeadset"/>).</summary>
        private readonly Label eachEye = new Label { AutoSize = true, MaximumSize = new Size(260, 0), Margin = new Padding(3, 4, 3, 4) };
        private readonly ComboBox antiAliasing = Choices(Setting.AntiAliasing);
        private readonly ComboBox sharpening = Choices(Setting.Sharpening);
        private readonly ComboBox reflections = Choices(Setting.ScreenReflections);
        private readonly ComboBox foveation = Choices(Setting.Foveation);
        /// <summary>Its matched choice names the last session's refresh rate (<see cref="ShowPacingChoice"/>).</summary>
        private readonly ComboBox pacing = Choices(Setting.FramePacing);
        private readonly CheckBox parallelEyes = new CheckBox { AutoSize = true };
        private static readonly bool ParallelEyesOffered = Environment.GetEnvironmentVariable("ETERNALVR_SHOW_PARALLEL_EYES") == "1";

        /// <summary>A hand-set smoothing that is none of the named steps: shown as a fifth, "Custom" choice.</summary>
        private double customSmoothing = -1;
        /// <summary>A hand-set vibration strength that is none of the named steps, shown the same way.</summary>
        private double customVibration = -1;
        /// <summary>A hand-set punch speed or hold time that is none of the named steps, shown the same way.</summary>
        private double customPunchSpeed = -1, customHoldTime = -1;

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
                Row(Setting.GloryKills, gloryKills,
                    s => gloryKills.SelectedIndex = (int)s.GloryKills,
                    s => s.GloryKills = (GloryKillView)gloryKills.SelectedIndex),
                Row(Setting.WalkInRoom, bodyFollow, s => bodyFollow.Checked = s.BodyFollow, s => s.BodyFollow = bodyFollow.Checked),
                Row(Setting.HeadFade, headFade, s => headFade.Checked = s.HeadFade, s => s.HeadFade = headFade.Checked),
                Row(Setting.RecenterHold, recenter, s => recenter.Checked = s.RecenterLongPress, s => s.RecenterLongPress = recenter.Checked),
                Row(Setting.SkipCutscenes, skipCinematics, s => skipCinematics.Checked = s.SkipCinematics, s => s.SkipCinematics = skipCinematics.Checked));
            var body = Group("Body",
                Row(Setting.PlayPosition, posture,
                    s => posture.SelectedIndex = (int)s.Posture,
                    s => s.Posture = (PostureMode)posture.SelectedIndex),
                Row(Setting.EyeHeight, height,
                    s => height.SelectedIndex = (int)s.Height,
                    s => s.Height = (HeightMode)height.SelectedIndex));
            var gestures = Group("Gestures",
                Row(Setting.ThrowGesture, throwGesture, s => throwGesture.Checked = s.ThrowGesture, s => s.ThrowGesture = throwGesture.Checked),
                Row(Setting.SwingGesture, swingGesture, s => swingGesture.Checked = s.SwingGesture, s => s.SwingGesture = swingGesture.Checked),
                Row(Setting.HandsJump, handsJump, s => handsJump.Checked = s.HandsJump, s => s.HandsJump = handsJump.Checked),
                Row(Setting.PunchSpeed, punchSpeed, LoadPunchSpeed, ReadPunchSpeed));
            var controls = Group("Controls",
                Row(Setting.AimWith, aim,
                    s => aim.SelectedIndex = (int)s.Aim,
                    s => s.Aim = (AimMode)aim.SelectedIndex),
                Row(Setting.RevenantAimWith, revenantAim,
                    s => revenantAim.SelectedIndex = (int)s.RevenantAim,
                    s => s.RevenantAim = (RevenantAimMode)revenantAim.SelectedIndex),
                Row(Setting.MeleeAimWith, meleeAim,
                    s => meleeAim.SelectedIndex = (int)s.MeleeAim,
                    s => s.MeleeAim = (ActionAimMode)meleeAim.SelectedIndex),
                Row(Setting.EquipmentAimWith, equipmentAim,
                    s => equipmentAim.SelectedIndex = (int)s.EquipmentAim,
                    s => s.EquipmentAim = (ActionAimMode)equipmentAim.SelectedIndex),
                Row(Setting.WeaponHand, hand,
                    s => hand.SelectedIndex = (int)s.Hand,
                    s => s.Hand = (Handedness)hand.SelectedIndex),
                Row(Setting.Arms, showArms, s => showArms.Checked = s.ShowArms, s => s.ShowArms = showArms.Checked),
                Row(Setting.MoveToward, locomotion,
                    s => locomotion.SelectedIndex = (int)s.Locomotion,
                    s => s.Locomotion = (LocomotionMode)locomotion.SelectedIndex),
                Row(Setting.XButton, xButton,
                    s => xButton.SelectedIndex = (int)s.Dossier,
                    s => s.Dossier = (DossierPress)xButton.SelectedIndex),
                Row(Setting.DossierMapSticks, mapSticks,
                    s => mapSticks.SelectedIndex = (int)s.MapSticks,
                    s => s.MapSticks = (MapPanStick)mapSticks.SelectedIndex),
                Row(Setting.WeaponWheel, wheel,
                    s => wheel.SelectedIndex = (int)s.Wheel,
                    s => s.Wheel = (WheelSelect)wheel.SelectedIndex),
                Row(Setting.ThumbRestWheel, thumbRest,
                    s => thumbRest.SelectedIndex = (int)s.ThumbRest,
                    s => s.ThumbRest = (ThumbRestMode)thumbRest.SelectedIndex),
                Row(Setting.ThumbRestPicks, thumbRestPicks,
                    s => thumbRestPicks.SelectedIndex = (int)s.ThumbRestPicks,
                    s => s.ThumbRestPicks = (ThumbRestPick)thumbRestPicks.SelectedIndex),
                Row(Setting.ThumbRestFaceTouch, thumbRestFaceTouch,
                    s => thumbRestFaceTouch.Checked = s.ThumbRestFaceTouch, s => s.ThumbRestFaceTouch = thumbRestFaceTouch.Checked),
                Row(Setting.ThumbRestSlowdown, thumbRestSlowdown,
                    s => thumbRestSlowdown.Checked = s.ThumbRestSlowdown, s => s.ThumbRestSlowdown = thumbRestSlowdown.Checked),
                Row(Setting.HoldTime, holdTime, LoadHoldTime, ReadHoldTime),
                Row(Setting.AimSteadiness, steadiness, LoadSteadiness, ReadSteadiness),
                Row(Setting.AimDot, aimDot, s => aimDot.Checked = s.AimDot, s => s.AimDot = aimDot.Checked),
                Row(Setting.Vibration, vibration, LoadVibration, ReadVibration),
                Row(Setting.Bhaptics, bhaptics, s => bhaptics.Checked = s.Bhaptics, s => s.Bhaptics = bhaptics.Checked),
                // Nothing to save: the buttons open the controls editor and the folder, the caption says what it holds.
                Row(Setting.ButtonLayout, ControlsRow(), s => ShowControlsState(), s => { }, fit: w => WrapIn(controlsState, w)));
            var pictureRows = new List<SettingRow>
            {
                // Resolution's base and number (MainForm.Headset.cs).
                ResolutionRow(),
                // Nothing to save: the line follows the Resolution above and the headset's last answer (ShowHeadset).
                Row(Setting.EachEye, eachEye, s => { }, s => { }, wide: true),
                Row(Setting.AntiAliasing, antiAliasing,
                    s => antiAliasing.SelectedIndex = (int)s.AntiAliasing,
                    s => s.AntiAliasing = (AntiAliasingMode)Math.Max(0, antiAliasing.SelectedIndex)),
                Row(Setting.Sharpening, sharpening,
                    s => sharpening.SelectedIndex = (int)s.Sharpening,
                    s => s.Sharpening = (SharpeningMode)Math.Max(0, sharpening.SelectedIndex)),
                Row(Setting.ScreenReflections, reflections,
                    s => reflections.SelectedIndex = (int)s.Reflections,
                    s => s.Reflections = (ReflectionsMode)Math.Max(0, reflections.SelectedIndex)),
                Row(Setting.Foveation, foveation,
                    s => foveation.SelectedIndex = (int)s.Foveation,
                    s => s.Foveation = (FoveationMode)foveation.SelectedIndex),
                Row(Setting.FramePacing, pacing,
                    s => pacing.SelectedIndex = (int)s.Pacing,
                    s => s.Pacing = (FramePacing)Math.Max(0, pacing.SelectedIndex)),
            };
            // Texture streaming and the CPU Saver: one checkbox per item of data\cpu-saver.txt (MainForm.CpuSaver.cs).
            if (StreamingRow() is SettingRow streaming) pictureRows.Add(streaming);
            // Both eyes' rendering work at the same time, for processor-limited systems (experimental, off by default).
            // Shown only with ETERNALVR_SHOW_PARALLEL_EYES=1 (test builds) until it works in a headset; hidden, it stays off.
            if (ParallelEyesOffered)
                pictureRows.Add(Row(Setting.ParallelEyes, parallelEyes, s => parallelEyes.Checked = s.ParallelEyes, s => s.ParallelEyes = parallelEyes.Checked));
            var picture = Group("Picture", pictureRows.ToArray());
            var left = new List<Control> { comfort, body, gestures };
            if (SaverGroup() is GroupBox saver) left.Add(saver);
            // The Headset box across the top (MainForm.Headset.cs).
            var page = Page("Play", HeadsetGroup(), left.ToArray(), new Control[] { controls, picture, DlssGroup() });
            // The player edits the folder in Explorer: looked at again on coming back to the window or the tab.
            tabs.Selected += (s, e) => { if (e.TabPage == page) ShowControlsState(); };
            Activated += (s, e) => ShowControlsState();
            return page;
        }

        private FlowLayoutPanel ControlsRow()
        {
            var edit = new Button { Text = "Edit controls...", Width = 100, Height = ButtonHeight };
            var open = new Button { Text = "Open folder", Width = 80, Height = ButtonHeight };
            edit.Click += (s, e) => EditControls();
            open.Click += (s, e) => OpenControlsFolder();
            // The buttons on one line, what the folder holds under them.
            var buttons = new FlowLayoutPanel { AutoSize = true, Margin = Padding.Empty, WrapContents = false };
            buttons.Controls.AddRange(new Control[] { edit, open });
            controlsState.Padding = Padding.Empty;
            controlsState.Margin = new Padding(3, 0, 3, 3);
            var row = new FlowLayoutPanel { AutoSize = true, Margin = Padding.Empty, WrapContents = false, FlowDirection = FlowDirection.TopDown };
            row.Controls.AddRange(new Control[] { buttons, controlsState });
            return row;
        }

        private void ShowControlsState()
        {
            controlsState.Text = ctx.Controls.HasPlayerMaps ? "Using your own controls" : "Built-in controls";
        }

        /// <summary>Refreshes the controls folder (its README and the copy of the built-in maps); false, with a message, if it fails.</summary>
        private bool PrepareControls(ControlsFolder controls)
        {
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
                return false;
            }
            ctx.Log.Info("controls folder ready: " + controls.Dir);
            return true;
        }

        /// <summary>
        /// The controls editor (<see cref="ControlsDialog"/>), on the freshly prepared controls of the VR settings profile in
        /// use. A profile still using the controls of (none) gets its own copy first, so the editor changes only its own.
        /// </summary>
        private void EditControls()
        {
            if (!AdoptControls(ctx.Settings.Profile)) return;
            var controls = ctx.Controls;
            if (!PrepareControls(controls)) return;
            var lastProfile = SessionLogs.LastControllerProfile(ctx.Paths.Logs);
            if (lastProfile != null) ctx.Log.Info("controls editor: the last game used " + lastProfile);
            using (var dialog = new ControlsDialog(controls, ctx.Settings.Hand, lastProfile, ctx.Log))
                dialog.ShowDialog(this);
            ShowControlsState();
        }

        /// <summary>Opens the controls folder of the VR settings profile in use in Explorer, to edit the files by hand.</summary>
        private void OpenControlsFolder()
        {
            if (!AdoptControls(ctx.Settings.Profile)) return;
            var controls = ctx.Controls;
            if (!PrepareControls(controls)) return;
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

        private void LoadPunchSpeed(LauncherSettings s)
        {
            var speed = LauncherSettings.Clamp(s.PunchSpeed, LauncherSettings.MinPunchSpeed, LauncherSettings.MaxPunchSpeed,
                LauncherSettings.DefaultPunchSpeed);
            customPunchSpeed = SelectStep(punchSpeed, PunchSpeed.Names.Length, PunchSpeed.IndexOf(speed), speed);
        }

        private void ReadPunchSpeed(LauncherSettings s)
        {
            int i = punchSpeed.SelectedIndex;
            if (i >= 0 && i < PunchSpeed.Values.Length) s.PunchSpeed = PunchSpeed.Values[i];
            else if (customPunchSpeed >= 0) s.PunchSpeed = customPunchSpeed;
        }

        private void LoadHoldTime(LauncherSettings s)
        {
            var seconds = LauncherSettings.Clamp(s.HoldTime, LauncherSettings.MinHoldTime, LauncherSettings.MaxHoldTime,
                LauncherSettings.DefaultHoldTime);
            customHoldTime = SelectStep(holdTime, HoldTime.Names.Length, HoldTime.IndexOf(seconds), seconds);
        }

        private void ReadHoldTime(LauncherSettings s)
        {
            int i = holdTime.SelectedIndex;
            if (i >= 0 && i < HoldTime.Values.Length) s.HoldTime = HoldTime.Values[i];
            else if (customHoldTime >= 0) s.HoldTime = customHoldTime;
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
