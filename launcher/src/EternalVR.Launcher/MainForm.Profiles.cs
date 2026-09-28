using System;
using System.IO;
using System.Windows.Forms;
using EternalVR.Launcher.Core.Settings;

namespace EternalVR.Launcher
{
    /// <summary>
    /// The player profile strip above the tabs (<see cref="ProfileStore"/>): pick whose settings to use, save the
    /// current ones as a new player, or delete one. Every saved change goes to the active profile too.
    /// </summary>
    public sealed partial class MainForm
    {
        private const string NoProfile = "(none)";
        private readonly ComboBox profile = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 180 };
        private readonly Button newProfile = new Button { Text = "New...", Width = 80, Height = 26 };
        private readonly Button deleteProfile = new Button { Text = "Delete", Width = 80, Height = 26 };
        private FlowLayoutPanel profileStrip;
        /// <summary>The list is being refilled: a selection change then is not the player's.</summary>
        private bool fillingProfiles;

        private ProfileStore Profiles => new ProfileStore(ctx.Paths.Profiles);

        private FlowLayoutPanel ProfileStrip()
        {
            var label = new Label { Text = "Player", AutoSize = true, Margin = new Padding(3, 8, 6, 3) };
            profileStrip = new FlowLayoutPanel { AutoSize = true, Dock = DockStyle.Fill, WrapContents = false, Margin = Padding.Empty };
            profileStrip.Controls.AddRange(new Control[] { label, profile, newProfile, deleteProfile });
            tips.SetToolTip(profile, "Whose settings to use. Each player keeps their own Play and Advanced settings; the game folder and the runtime are shared.");
            tips.SetToolTip(newProfile, "Saves the settings shown now as a new player.");
            tips.SetToolTip(deleteProfile, "Deletes this player's saved settings. The settings shown stay in use.");
            profile.SelectedIndexChanged += (s, e) => SwitchProfile();
            newProfile.Click += (s, e) => NewProfile();
            deleteProfile.Click += (s, e) => DeleteProfile();
            FillProfiles();
            return profileStrip;
        }

        private void FillProfiles()
        {
            fillingProfiles = true;
            profile.Items.Clear();
            profile.Items.Add(NoProfile);
            foreach (var name in Profiles.Names()) profile.Items.Add(name);
            var active = ctx.Settings.Profile;
            int index = active.Length == 0 ? 0 : profile.Items.IndexOf(active);
            if (index < 0)
            {
                // The active profile's file is gone: its settings stay in use and are saved again under its name.
                profile.Items.Add(active);
                index = profile.Items.Count - 1;
            }
            profile.SelectedIndex = index;
            deleteProfile.Enabled = active.Length > 0;
            fillingProfiles = false;
        }

        private void SwitchProfile()
        {
            if (fillingProfiles) return;
            var name = profile.SelectedItem as string ?? NoProfile;
            if (name == NoProfile) name = string.Empty;
            if (name == ctx.Settings.Profile) return;
            SaveSettings();
            if (name.Length == 0)
            {
                ctx.Settings.Profile = string.Empty;
            }
            else
            {
                var loaded = Profiles.Load(name, ctx.Settings);
                if (loaded == null)
                {
                    ctx.Log.Warn("player profile '" + name + "' could not be read");
                    FillProfiles();
                    return;
                }
                ctx.Settings = loaded;
                LoadSettingsIntoControls();
            }
            SaveSettings();
            FillProfiles();
            ctx.Log.Info("player profile: " + (name.Length == 0 ? "none" : name));
            RunPreflight();
        }

        private void NewProfile()
        {
            var name = AskProfileName();
            if (name == null) return;
            if (Profiles.Exists(name) && MessageBox.Show(this, "A player called " + name + " exists. Replace their settings with the ones shown now?",
                    "New player", MessageBoxButtons.OKCancel, MessageBoxIcon.Question, MessageBoxDefaultButton.Button2) != DialogResult.OK)
                return;
            ReadControlsIntoSettings();
            ctx.Settings.Profile = name;
            SaveSettings();
            FillProfiles();
            ctx.Log.Info("player profile saved: " + name);
        }

        private void DeleteProfile()
        {
            var name = ctx.Settings.Profile;
            if (name.Length == 0) return;
            if (MessageBox.Show(this, "Delete the saved settings of " + name + "?\n\nThe settings shown stay in use.", "Delete player",
                    MessageBoxButtons.OKCancel, MessageBoxIcon.Question, MessageBoxDefaultButton.Button2) != DialogResult.OK)
                return;
            try { Profiles.Delete(name); }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException)
            {
                ctx.Log.Error("deleting player profile '" + name + "' failed: " + e.Message);
                return;
            }
            ctx.Settings.Profile = string.Empty;
            SaveSettings();
            FillProfiles();
            ctx.Log.Info("player profile deleted: " + name);
        }

        /// <summary>A small dialog for the new player's name; null when cancelled.</summary>
        private string AskProfileName()
        {
            using (var dialog = new Form
            {
                Text = "New player", FormBorderStyle = FormBorderStyle.FixedDialog, StartPosition = FormStartPosition.CenterParent,
                MinimizeBox = false, MaximizeBox = false, ShowInTaskbar = false, AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink,
                Font = Font,
            })
            {
                var prompt = new Label { Text = "Name (up to " + ProfileStore.MaxNameLength + " characters):", AutoSize = true };
                var box = new TextBox { Width = 240, MaxLength = ProfileStore.MaxNameLength };
                var ok = new Button { Text = "Save", DialogResult = DialogResult.OK, Width = 80 };
                var cancel = new Button { Text = "Cancel", DialogResult = DialogResult.Cancel, Width = 80 };
                var buttons = new FlowLayoutPanel { AutoSize = true, FlowDirection = FlowDirection.RightToLeft, Dock = DockStyle.Fill };
                buttons.Controls.AddRange(new Control[] { cancel, ok });
                var layout = new TableLayoutPanel { AutoSize = true, ColumnCount = 1, Padding = new Padding(10) };
                layout.Controls.AddRange(new Control[] { prompt, box, buttons });
                dialog.Controls.Add(layout);
                dialog.AcceptButton = ok;
                dialog.CancelButton = cancel;
                while (dialog.ShowDialog(this) == DialogResult.OK)
                {
                    if (ProfileStore.NormaliseName(box.Text) is string name) return name;
                    MessageBox.Show(dialog, "That name cannot be used: it must not contain \\ / : * ? \" < > | or start or end with a dot.",
                        "New player", MessageBoxButtons.OK, MessageBoxIcon.Information);
                }
                return null;
            }
        }
    }
}
