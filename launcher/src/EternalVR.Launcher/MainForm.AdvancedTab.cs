using System;
using System.Collections.Generic;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Windows.Forms;
using EternalVR.Launcher.Core.Settings;

namespace EternalVR.Launcher
{
    /// <summary>The Advanced tab: the view, the HUD panel, the controllers, the runtime and the arguments, and Reset to defaults.</summary>
    public sealed partial class MainForm
    {
        private readonly ComboBox mode = Choices(Setting.VrMode);
        private readonly ComboBox alternateEyes = Choices(Setting.AlternateEyes);
        private readonly NumericUpDown worldScale = Number(LauncherSettings.MinWorldScale, LauncherSettings.MaxWorldScale, 0.05, 2);
        private readonly CheckBox ipdOverride = new CheckBox { Text = "Set my own:", AutoSize = true };
        private readonly NumericUpDown ipd = Number(LauncherSettings.MinIpdMm, LauncherSettings.MaxIpdMm, 0.5, 1);
        private readonly ComboBox mirror = Choices(Setting.DesktopWindow);
        private readonly ComboBox mirrorMonitor = Choices(Setting.DesktopMonitor);
        private readonly ComboBox mirrorSize = Choices(Setting.DesktopSize);
        private readonly CheckBox mirrorCrop = new CheckBox { AutoSize = true };
        private readonly ComboBox cutscenes = Choices(Setting.CutsceneView);
        private readonly ComboBox cinemaShape = Choices(Setting.CutsceneShape);
        /// <summary>The ini value of each entry of <see cref="mirrorMonitor"/>: auto, primary, then each display's top-left x,y.</summary>
        private readonly List<string> monitorValues = new List<string>();
        /// <summary>A size that is none of the named ones (set in the file by hand): shown as a last, "Custom" choice.</summary>
        private string customMirrorSize;
        private readonly NumericUpDown hudDistance = Number(LauncherSettings.MinHudDistance, LauncherSettings.MaxHudDistance, 0.1, 2);
        private readonly NumericUpDown hudWidth = Number(LauncherSettings.MinHudWidth, LauncherSettings.MaxHudWidth, 0.1, 2);
        private readonly NumericUpDown hudHeight = Number(LauncherSettings.MinHudHeight, LauncherSettings.MaxHudHeight, 0.05, 2);
        private readonly ComboBox hudPlace = Choices(Setting.HudPlace);
        private readonly CheckBox controllers = new CheckBox { AutoSize = true };
        private readonly ComboBox shots = Choices(Setting.ShotsFrom);
        private readonly NumericUpDown dotSize = Number(LauncherSettings.MinAimDotSize, LauncherSettings.MaxAimDotSize, 0.1, 1);
        private readonly CheckBox menuBeam = new CheckBox { AutoSize = true };
        private readonly ComboBox runtime = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 420 };
        private readonly TextBox extraArgs = new TextBox { Width = 420 };
        /// <summary>One line, cut with an ellipsis; the whole path and where it came from are in its tooltip.</summary>
        private readonly Label gameFolder = new Label { AutoSize = false, AutoEllipsis = true, Width = 250, Height = 18, Anchor = AnchorStyles.Top | AnchorStyles.Left };

        /// <summary>The eye distance shown while the headset's is used (the value an override starts from).</summary>
        private const decimal TypicalIpdMm = 63.0m;

        private TabPage AdvancedTab()
        {
            // Filling a monitor with the whole (tall) eye image squashes it: choosing Fill turns the crop on.
            mirrorSize.SelectedIndexChanged += (o, e) =>
            {
                if (!loading && mirrorSize.SelectedIndex == Array.IndexOf(MirrorSettings.Sizes, MirrorSettings.SizeFill))
                    mirrorCrop.Checked = true;
            };
            var eyeDistance = new FlowLayoutPanel { AutoSize = true, Margin = Padding.Empty, WrapContents = false };
            eyeDistance.Controls.AddRange(new Control[] { ipdOverride, ipd, Caption("mm") });
            var view = Group("View",
                Row(Setting.VrMode, mode,
                    s => mode.SelectedIndex = (int)s.Mode,
                    s => s.Mode = (VrMode)mode.SelectedIndex, preflight: true),
                Row(Setting.AlternateEyes, alternateEyes,
                    s => alternateEyes.SelectedIndex = (int)s.AlternateEyes,
                    s => s.AlternateEyes = (AlternateEyesMode)alternateEyes.SelectedIndex),
                Row(Setting.WorldSize, worldScale,
                    s => worldScale.Value = (decimal)LauncherSettings.ClampWorldScale(s.WorldScale),
                    s => s.WorldScale = (double)worldScale.Value),
                Row(Setting.EyeDistance, eyeDistance, LoadIpd, s => s.IpdMm = ipdOverride.Checked ? (double)ipd.Value : 0.0),
                Row(Setting.DesktopWindow, mirror,
                    s => mirror.SelectedIndex = (int)s.Mirror,
                    s => s.Mirror = (MirrorMode)mirror.SelectedIndex),
                Row(Setting.DesktopMonitor, mirrorMonitor, LoadMirrorMonitor,
                    s => s.MirrorDisplay = mirrorMonitor.SelectedIndex >= 0 ? monitorValues[mirrorMonitor.SelectedIndex] : MirrorSettings.DisplayAuto),
                Row(Setting.DesktopSize, mirrorSize, LoadMirrorSize, ReadMirrorSize),
                Row(Setting.DesktopCrop, mirrorCrop, s => mirrorCrop.Checked = s.MirrorCrop, s => s.MirrorCrop = mirrorCrop.Checked),
                Row(Setting.CutsceneView, cutscenes,
                    s => cutscenes.SelectedIndex = (int)s.Cutscenes,
                    s => s.Cutscenes = (CutsceneView)cutscenes.SelectedIndex),
                Row(Setting.CutsceneShape, cinemaShape,
                    s => cinemaShape.SelectedIndex = (int)s.Cinema,
                    s => s.Cinema = (CinemaShape)cinemaShape.SelectedIndex));
            var hud = Group("HUD panel",
                Row(Setting.HudDistance, WithUnit(hudDistance, "m"),
                    s => hudDistance.Value = Clamped(s.HudDistance, hudDistance, LauncherSettings.DefaultHudDistance),
                    s => s.HudDistance = (double)hudDistance.Value),
                Row(Setting.HudSize, WithUnit(hudWidth, "m wide"),
                    s => hudWidth.Value = Clamped(s.HudWidth, hudWidth, LauncherSettings.DefaultHudWidth),
                    s => s.HudWidth = (double)hudWidth.Value),
                Row(Setting.HudHeight, WithUnit(hudHeight, "m"),
                    s => hudHeight.Value = Clamped(s.HudHeight, hudHeight, 0.0),
                    s => s.HudHeight = (double)hudHeight.Value),
                Row(Setting.HudPlace, hudPlace,
                    s => hudPlace.SelectedIndex = (int)s.Hud,
                    s => s.Hud = (HudMode)hudPlace.SelectedIndex));
            var hands = Group("Controllers",
                Row(Setting.MotionControllers, controllers,
                    s => controllers.Checked = s.Controllers,
                    s => s.Controllers = controllers.Checked, preflight: true),
                Row(Setting.ShotsFrom, shots,
                    s => shots.SelectedIndex = (int)s.Shots,
                    s => s.Shots = (ShotOrigin)shots.SelectedIndex),
                Row(Setting.AimDotSize, WithUnit(dotSize, "°"),
                    s => dotSize.Value = Clamped(s.AimDotSize, dotSize, LauncherSettings.DefaultAimDotSize),
                    s => s.AimDotSize = (double)dotSize.Value),
                Row(Setting.MenuLaser, menuBeam, s => menuBeam.Checked = s.MenuBeam, s => s.MenuBeam = menuBeam.Checked));

            var runtimeRow = new FlowLayoutPanel { AutoSize = true, Margin = Padding.Empty, WrapContents = false };
            var browse = new Button { Text = "Browse...", Width = 90, Height = ButtonHeight };
            browse.Click += (s, e) => BrowseRuntime();
            runtimeRow.Controls.AddRange(new Control[] { runtime, browse });
            var folderRow = new FlowLayoutPanel { AutoSize = true, Margin = Padding.Empty, WrapContents = false };
            var choose = new Button { Text = "Choose...", Width = 90, Height = ButtonHeight };
            var find = new Button { Text = "Find automatically", Width = 130, Height = ButtonHeight };
            choose.Click += (s, e) => ChooseGameFolder();
            find.Click += (s, e) => { ctx.Settings.GameDir = string.Empty; SaveSettings(); RunPreflight(); };
            folderRow.Controls.AddRange(new Control[] { gameFolder, choose, find });
            var system = Group("System",
                // The folder is chosen by its buttons and shown after each check (RunPreflight).
                Row(Setting.GameFolder, folderRow, s => { }, s => { }, fit: w => FitGameFolder(w, choose, find)),
                // The runtime list is filled from the context (FillRuntimes); only the choice is read back here.
                Row(Setting.Runtime, runtimeRow, s => { }, s => s.Runtime = (runtime.SelectedItem as RuntimeItem)?.Value ?? LauncherSettings.SystemRuntime,
                    preflight: true, fit: w => SetWidth(runtime, w - browse.Width - browse.Margin.Horizontal - runtime.Margin.Horizontal)),
                Row(Setting.ExtraArguments, extraArgs, s => extraArgs.Text = s.ExtraArguments, s => s.ExtraArguments = extraArgs.Text.Trim(), preflight: true));

            var reset = new Button { Text = "Reset to defaults...", Width = 150, Height = ButtonHeight, Margin = new Padding(9, 6, 3, 6) };
            // The table would squeeze a fixed-size button to its text's first word.
            reset.MinimumSize = reset.Size;
            reset.Click += (s, e) => ResetToDefaults();
            tips.SetToolTip(reset, "Puts every setting on the Play and Advanced tabs back to its default.\nThe game folder, the layer folder and the runtime are kept.");
            return Page("Advanced", null, new Control[] { view, reset }, new Control[] { hands, hud }, system);
        }

        /// <summary>The game folder's line takes the room the buttons leave, its text level with theirs.</summary>
        private void FitGameFolder(int width, Button choose, Button find)
        {
            SetWidth(gameFolder, width - choose.Width - choose.Margin.Horizontal - find.Width - find.Margin.Horizontal - gameFolder.Margin.Horizontal);
            int height = gameFolder.Font.Height + 2;
            if (gameFolder.Height != height) gameFolder.Height = height;
            int top = choose.Margin.Top + Math.Max(0, (choose.Height - height) / 2);
            if (gameFolder.Margin.Top != top) gameFolder.Margin = new Padding(gameFolder.Margin.Left, top, gameFolder.Margin.Right, 0);
        }

        private void LoadIpd(LauncherSettings s)
        {
            var mm = LauncherSettings.ClampIpd(s.IpdMm);
            ipdOverride.Checked = mm > 0.0;
            ipd.Value = mm > 0.0 ? (decimal)mm : TypicalIpdMm;
        }

        /// <summary>Automatic, the primary monitor, then each display (Windows' list), and a saved point no display holds now.</summary>
        private void LoadMirrorMonitor(LauncherSettings s)
        {
            mirrorMonitor.Items.Clear();
            monitorValues.Clear();
            var names = SettingTexts.For(Setting.DesktopMonitor).Choices;
            mirrorMonitor.Items.Add(names[0]);
            monitorValues.Add(MirrorSettings.DisplayAuto);
            mirrorMonitor.Items.Add(names[1]);
            monitorValues.Add(MirrorSettings.DisplayPrimary);
            var displays = Platform.WindowsSystem.Displays();
            for (int i = 0; i < displays.Count; i++)
            {
                var d = displays[i];
                mirrorMonitor.Items.Add(string.Format(CultureInfo.InvariantCulture, "Display {0}: {1}x{2} at {3},{4}", i + 1, d.Width, d.Height, d.X, d.Y));
                monitorValues.Add(MirrorSettings.Point(d.X, d.Y));
            }
            var saved = MirrorSettings.NormaliseDisplay(s.MirrorDisplay) ?? MirrorSettings.DisplayAuto;
            if (!monitorValues.Contains(saved))
            {
                mirrorMonitor.Items.Add("Saved position " + saved);
                monitorValues.Add(saved);
            }
            mirrorMonitor.SelectedIndex = monitorValues.IndexOf(saved);
        }

        private void LoadMirrorSize(LauncherSettings s)
        {
            var size = MirrorSettings.NormaliseSize(s.MirrorSize) ?? MirrorSettings.DefaultSize;
            int index = Array.IndexOf(MirrorSettings.Sizes, size);
            while (mirrorSize.Items.Count > MirrorSettings.Sizes.Length) mirrorSize.Items.RemoveAt(mirrorSize.Items.Count - 1);
            customMirrorSize = index < 0 ? size : null;
            if (index < 0)
            {
                mirrorSize.Items.Add("Custom (" + size + ")");
                index = mirrorSize.Items.Count - 1;
            }
            mirrorSize.SelectedIndex = index;
        }

        private void ReadMirrorSize(LauncherSettings s)
        {
            int i = mirrorSize.SelectedIndex;
            if (i >= 0 && i < MirrorSettings.Sizes.Length) s.MirrorSize = MirrorSettings.Sizes[i];
            else if (customMirrorSize != null) s.MirrorSize = customMirrorSize;
        }

        /// <summary>Parts of a row that follow another control of the same row.</summary>
        private void UpdateRowParts()
        {
            ipd.Enabled = ipdOverride.Checked;
            ShowDlss();
            ShowHeadset();
        }

        private static decimal Clamped(double v, NumericUpDown box, double fallback) =>
            (decimal)LauncherSettings.Clamp(v, (double)box.Minimum, (double)box.Maximum, fallback);
    }
}
