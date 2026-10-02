using System;
using System.IO;
using System.Windows.Forms;
using EternalVR.Launcher.Core.Settings;

namespace EternalVR.Launcher
{
    /// <summary>
    /// The VR settings profile strip above the tabs (<see cref="ProfileStore"/>): pick which saved set of EternalVR
    /// settings to use, save the settings shown into the selected profile (or a new one), or delete one. Changes
    /// go into the profile only when saved; switching away from unsaved changes asks first. Each profile also keeps
    /// its own controls (<see cref="ControlSets"/>), which Edit controls saves straight away. The profiles hold
    /// EternalVR's settings only, never the game's saves.
    /// </summary>
    public sealed partial class MainForm
    {
        private const string NoProfile = "(none)";
        private readonly ComboBox profile = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 180 };
        private readonly Button saveProfile = new Button { Text = "Save", Width = 80, Height = ButtonHeight };
        private readonly Button deleteProfile = new Button { Text = "Delete", Width = 80, Height = ButtonHeight };
        private FlowLayoutPanel profileStrip;
        /// <summary>The list is being refilled: a selection change then is not the user's.</summary>
        private bool fillingProfiles;
        /// <summary>A setting changed since the selected profile was loaded or saved.</summary>
        private bool profileUnsaved;

        private ProfileStore Profiles => new ProfileStore(ctx.Paths.Profiles);

        private FlowLayoutPanel ProfileStrip()
        {
            var label = new Label { Text = "VR settings profile", AutoSize = true, Margin = new Padding(3, 8, 6, 3) };
            profileStrip = new FlowLayoutPanel { AutoSize = true, Dock = DockStyle.Fill, WrapContents = false, Margin = Padding.Empty };
            profileStrip.Controls.AddRange(new Control[] { label, profile, saveProfile, deleteProfile });
            tips.SetToolTip(profile, "Which saved set of EternalVR settings to use, for example one per person. Each profile keeps its own Play and Advanced settings and its own controls (Edit controls on the Play tab); the game folder and the runtime are shared. DOOM Eternal's own saves and settings are not affected.");
            tips.SetToolTip(saveProfile, "Saves the settings shown into the selected profile. With (none) selected it asks for a name and makes a new profile, which starts with a copy of the controls in use.");
            tips.SetToolTip(deleteProfile, "Deletes this EternalVR settings profile and its controls. The settings shown stay in use, with the controls of (none); game saves are not affected.");
            profile.SelectedIndexChanged += (s, e) => SwitchProfile();
            saveProfile.Click += (s, e) => SaveProfile();
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
                // The active profile's file is gone: its settings stay in use, and Save writes it again.
                profile.Items.Add(active);
                index = profile.Items.Count - 1;
            }
            profile.SelectedIndex = index;
            deleteProfile.Enabled = active.Length > 0;
            fillingProfiles = false;
            ShowProfileState();
        }

        /// <summary>A setting changed: the selected profile (if any) now differs from what is shown.</summary>
        private void MarkProfileChanged()
        {
            if (ctx.Settings.Profile.Length == 0 || profileUnsaved) return;
            profileUnsaved = true;
            ShowProfileState();
        }

        private void ShowProfileState()
        {
            saveProfile.Text = profileUnsaved ? "Save *" : "Save";
        }

        private void SwitchProfile()
        {
            if (fillingProfiles) return;
            var name = profile.SelectedItem as string ?? NoProfile;
            if (name == NoProfile) name = string.Empty;
            if (name == ctx.Settings.Profile) return;
            if (profileUnsaved && ctx.Settings.Profile.Length > 0)
            {
                var answer = MessageBox.Show(this, "Save your changes to the VR settings profile " + ctx.Settings.Profile + " first?",
                    "VR settings profile", MessageBoxButtons.YesNoCancel, MessageBoxIcon.Question);
                if (answer == DialogResult.Cancel)
                {
                    FillProfiles(); // back to the profile still in use
                    return;
                }
                if (answer == DialogResult.Yes && !WriteProfile(ctx.Settings.Profile))
                {
                    FillProfiles();
                    return;
                }
            }
            SaveSettings();
            if (name.Length == 0)
            {
                // (none): the settings shown stay in use, no longer tied to a profile.
                ctx.Settings.Profile = string.Empty;
            }
            else
            {
                AdoptControls(name);
                var loaded = Profiles.Load(name, ctx.Settings);
                if (loaded == null)
                {
                    ctx.Log.Warn("VR settings profile '" + name + "' could not be read");
                    FillProfiles();
                    return;
                }
                ctx.Settings = loaded;
                LoadSettingsIntoControls();
            }
            profileUnsaved = false;
            SaveSettings();
            FillProfiles();
            ShowControlsState();
            ctx.Log.Info("VR settings profile: " + (name.Length == 0 ? "none" : name) + "; controls in " + ctx.Controls.Dir);
            RunPreflight();
        }

        private void SaveProfile()
        {
            var name = ctx.Settings.Profile;
            bool isNew = false;
            if (name.Length == 0)
            {
                name = AskProfileName();
                if (name == null) return;
                isNew = !Profiles.Exists(name);
                if (!isNew && MessageBox.Show(this, "A profile called " + name + " exists. Replace its settings with the ones shown now?\n\nIts controls stay as they are.",
                        "New VR settings profile", MessageBoxButtons.OKCancel, MessageBoxIcon.Question, MessageBoxDefaultButton.Button2) != DialogResult.OK)
                    return;
            }
            // A new profile starts with a copy of the controls in use; an existing one keeps its own.
            if (isNew ? !StartControls(name, ctx.Settings.Profile) : !AdoptControls(name)) return;
            if (!WriteProfile(name)) return;
            ctx.Settings.Profile = name;
            SaveSettings(); // launcher.ini names the profile in use
            FillProfiles();
            ShowControlsState();
        }

        /// <summary>Writes the settings shown into profile <paramref name="name"/>; false (logged) when that failed.</summary>
        private bool WriteProfile(string name)
        {
            ReadControlsIntoSettings();
            try { Profiles.Save(name, ctx.Settings); }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException || e is ArgumentException)
            {
                ctx.Log.Error("saving VR settings profile '" + name + "' failed: " + e.Message);
                MessageBox.Show(this, "The profile could not be saved: " + e.Message, "VR settings profile",
                    MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return false;
            }
            profileUnsaved = false;
            ShowProfileState();
            ctx.Log.Info("VR settings profile saved: " + name);
            return true;
        }

        private void DeleteProfile()
        {
            var name = ctx.Settings.Profile;
            if (name.Length == 0) return;
            if (MessageBox.Show(this, "Delete the VR settings profile " + name + " and its controls?\n\nThe settings shown stay in use, with the controls of (none). Game saves are not affected.", "Delete VR settings profile",
                    MessageBoxButtons.OKCancel, MessageBoxIcon.Question, MessageBoxDefaultButton.Button2) != DialogResult.OK)
                return;
            try { Profiles.Delete(name); }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException)
            {
                ctx.Log.Error("deleting VR settings profile '" + name + "' failed: " + e.Message);
                return;
            }
            try { ctx.ControlSets.Delete(name); }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException)
            {
                // The profile is gone; its folder stays behind, and a new profile of that name replaces it.
                ctx.Log.Warn("deleting the controls of VR settings profile '" + name + "' failed: " + e.Message);
            }
            ctx.Settings.Profile = string.Empty;
            profileUnsaved = false;
            SaveSettings();
            FillProfiles();
            ShowControlsState();
            ctx.Log.Info("VR settings profile deleted: " + name);
        }

        /// <summary>
        /// Gives every profile without controls of its own a copy of the controls of (none), which they all used before
        /// profiles kept their own controls. Once per start; a profile that fails keeps using the controls of (none).
        /// </summary>
        private void GiveProfilesTheirControls()
        {
            foreach (var name in Profiles.Names()) AdoptControls(name, quiet: true);
        }

        /// <summary>
        /// Gives profile <paramref name="name"/> its own controls, a copy of the controls of (none), when it has none
        /// (<see cref="ControlSets.Adopt"/>). False, logged (and shown unless <paramref name="quiet"/>), when that failed.
        /// </summary>
        private bool AdoptControls(string name, bool quiet = false)
        {
            try
            {
                if (ctx.ControlSets.Adopt(name)) ctx.Log.Info("VR settings profile '" + name + "' now keeps its own controls, a copy of the controls of (none)");
                return true;
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException)
            {
                ctx.Log.Warn("copying the controls of (none) for VR settings profile '" + name + "' failed: " + e.Message);
                if (!quiet)
                    MessageBox.Show(this, "The controls of the profile " + name + " could not be made:\n\n" + e.Message, "VR settings profile",
                        MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return false;
            }
        }

        /// <summary>A new profile's controls: a copy of <paramref name="from"/>'s (<see cref="ControlSets.StartFrom"/>). False, shown, when that failed.</summary>
        private bool StartControls(string name, string from)
        {
            try
            {
                ctx.ControlSets.StartFrom(name, from);
                ctx.Log.Info("VR settings profile '" + name + "' starts with a copy of the controls of " + (from.Length == 0 ? "(none)" : from));
                return true;
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException)
            {
                ctx.Log.Error("copying the controls for VR settings profile '" + name + "' failed: " + e.Message);
                MessageBox.Show(this, "The profile's controls could not be made, so the profile was not saved:\n\n" + e.Message, "VR settings profile",
                    MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return false;
            }
        }

        /// <summary>A small dialog for a new profile's name; null when cancelled.</summary>
        private string AskProfileName()
        {
            using (var dialog = new Form
            {
                Text = "New VR settings profile", FormBorderStyle = FormBorderStyle.FixedDialog, StartPosition = FormStartPosition.CenterParent,
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
                        "New VR settings profile", MessageBoxButtons.OK, MessageBoxIcon.Information);
                }
                return null;
            }
        }
    }
}
