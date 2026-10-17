using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Windows.Forms;
using EternalVR.Launcher.Core.Settings;

namespace EternalVR.Launcher
{
    /// <summary>
    /// The VR settings profile strip above the tabs (<see cref="ProfileStore"/>): pick which saved set of EternalVR
    /// settings to use, save the settings shown into the selected profile (or a new one), duplicate, rename or delete one,
    /// and see how many settings differ from the profile's preset (<see cref="ShowChanges"/>). Changes go into the profile
    /// only when saved; switching away from unsaved changes asks first. Each profile also keeps its own controls
    /// (<see cref="ControlSets"/>), which Edit controls saves straight away. The profiles hold EternalVR's settings only,
    /// never the game's saves.
    /// </summary>
    public sealed partial class MainForm
    {
        private const string NoProfile = "(none)";
        private readonly ComboBox profile = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 180 };
        private readonly Button saveProfile = new Button { Text = "Save", Width = 80, Height = ButtonHeight };
        private readonly Button duplicateProfile = new Button { Text = "Duplicate...", Width = 90, Height = ButtonHeight };
        private readonly Button renameProfile = new Button { Text = "Rename...", Width = 80, Height = ButtonHeight };
        private readonly Button deleteProfile = new Button { Text = "Delete", Width = 80, Height = ButtonHeight };
        private FlowLayoutPanel profileStrip;
        /// <summary>The list is being refilled: a selection change then is not the user's.</summary>
        private bool fillingProfiles;
        /// <summary>A setting changed since the selected profile was loaded or saved.</summary>
        private bool profileUnsaved;
        /// <summary>
        /// The selected profile as its file holds it, read with this launcher's preset (T-134); null for (none) or a profile
        /// whose file cannot be read. launcher.ini lists the settings that differ from it (<see cref="UnsavedNow"/>).
        /// </summary>
        private LauncherSettings profileSaved;
        /// <summary>A rename is not finished: the profile strip stays off until the next start (<see cref="HoldProfilesForRename"/>).</summary>
        private bool renameHeld;
        /// <summary>Says why the profile strip is locked (<see cref="HoldProfilesForRename"/>); hidden otherwise.</summary>
        private readonly Label renameNote = new Label
        {
            AutoSize = true, Visible = false, Margin = new Padding(12, 8, 3, 3),
            Text = "A rename is not finished: profiles are locked until the launcher starts again.",
        };

        private ProfileStore Profiles => new ProfileStore(ctx.Paths.Profiles);

        private FlowLayoutPanel ProfileStrip()
        {
            var label = new Label { Text = "VR settings profile", AutoSize = true, Margin = new Padding(3, 8, 6, 3) };
            // Wraps onto a second line in a narrow window.
            profileStrip = new FlowLayoutPanel { AutoSize = true, Dock = DockStyle.Fill, WrapContents = true, Margin = Padding.Empty };
            profileStrip.Controls.AddRange(new Control[] { label, profile, saveProfile, duplicateProfile, renameProfile, deleteProfile, presetState, renameNote });
            tips.SetToolTip(profile, "Which saved set of EternalVR settings to use, for example one per person. Each profile keeps its own Play and Advanced settings and its own controls (Edit controls on the Play tab); the game folder and the runtime are shared. DOOM Eternal's own saves and settings are not affected.");
            tips.SetToolTip(saveProfile, "Saves the settings shown into the selected profile. With (none) selected it asks for a name and makes a new profile, which starts with a copy of the controls in use.");
            tips.SetToolTip(duplicateProfile, "Makes a new profile with the settings shown and a copy of this profile's controls, and picks it. This profile stays as it was saved.");
            tips.SetToolTip(renameProfile, "Gives this profile another name. Its settings and controls go with it.");
            tips.SetToolTip(deleteProfile, "Deletes this EternalVR settings profile and its controls. The settings shown stay in use, with the controls of (none); game saves are not affected.");
            tips.SetToolTip(presetState, Wrap(PresetStateTip()));
            profile.SelectedIndexChanged += (s, e) => SwitchProfile();
            saveProfile.Click += (s, e) => SaveProfile();
            duplicateProfile.Click += (s, e) => DuplicateProfile();
            renameProfile.Click += (s, e) => RenameProfile();
            deleteProfile.Click += (s, e) => DeleteProfile();
            FillProfiles();
            if (renameHeld) HoldProfilesForRename();
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
            deleteProfile.Enabled = renameProfile.Enabled = duplicateProfile.Enabled = active.Length > 0 && !renameHeld;
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
                profileSaved = null;
            }
            else
            {
                AdoptControls(name);
                LauncherSettings loaded;
                string why = null;
                try { loaded = Profiles.Load(name, ctx.Settings); }
                // A profile saved by a newer launcher (its schema is refused), or a file that cannot be read.
                catch (Exception e) when (e is SettingsException || e is IOException || e is UnauthorizedAccessException)
                {
                    loaded = null;
                    why = e.Message;
                }
                if (loaded == null)
                {
                    ctx.Log.Warn("VR settings profile '" + name + "' could not be read" + (why == null ? string.Empty : ": " + why));
                    if (why != null)
                        MessageBox.Show(this, "The VR settings profile " + name + " could not be read: " + why, "VR settings profile",
                            MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    FillProfiles();
                    return;
                }
                profileSaved = loaded.Clone();
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
            try { Profiles.Save(name, ctx.Settings, ctx.Data.CpuSaver); }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException || e is ArgumentException)
            {
                ctx.Log.Error("saving VR settings profile '" + name + "' failed: " + e.Message);
                MessageBox.Show(this, "The profile could not be saved: " + e.Message, "VR settings profile",
                    MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return false;
            }
            profileUnsaved = false;
            profileSaved = ReadSaved(name);
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
            profileSaved = null;
            SaveSettings();
            FillProfiles();
            ShowControlsState();
            ctx.Log.Info("VR settings profile deleted: " + name);
        }

        /// <summary>
        /// At start, before the window reads the settings: the active profile as its file holds it, which the context read
        /// with this launcher's preset and laid launcher.ini's unsaved changes on (T-134, <see cref="LauncherContext.ActiveProfileSaved"/>).
        /// Save shows the settings that differ from it as unsaved. A rename still not finished holds the profile strip.
        /// </summary>
        private void TakeProfileAtStart()
        {
            renameHeld = ctx.RenameUnfinished;
            if (ctx.Settings.Profile.Length == 0 || ctx.ActiveProfileSaved == null) return;
            profileSaved = ctx.ActiveProfileSaved;
            profileUnsaved = UnsavedNow().Count > 0;
        }

        /// <summary>
        /// Holds the profile strip while a rename is not finished (its journal stays; <see cref="ProfileRename"/>): picking,
        /// saving, duplicating, renaming or deleting a profile now could hide the controls still on their way. The next start
        /// finishes the rename.
        /// </summary>
        private void HoldProfilesForRename()
        {
            renameHeld = true;
            // The strip itself stays on, so the note reads as normal text and keeps its tooltip.
            foreach (var c in new Control[] { profile, saveProfile, duplicateProfile, renameProfile, deleteProfile }) c.Enabled = false;
            renameNote.Visible = true;
            tips.SetToolTip(renameNote, "A rename of a VR settings profile could not finish. It finishes the next time the launcher starts; until then the profile list, Edit controls and Open folder of this profile are locked. The launcher's log says what stopped it.");
            ctx.Log.Warn("a VR settings profile rename is not finished: the profile list is locked until the launcher starts again");
        }

        /// <summary>The settings file keys whose value differs from the selected profile's file; null without one.</summary>
        private IReadOnlyList<string> UnsavedNow() =>
            ctx.Settings.Profile.Length == 0 || profileSaved == null ? null
            : ProfileEdits.Changed(ctx.Settings, profileSaved, ctx.Data.CpuSaver).OrderBy(k => k, StringComparer.Ordinal).ToList();

        /// <summary>Profile <paramref name="name"/> as its file holds it (<see cref="profileSaved"/>); null when it cannot be read.</summary>
        private LauncherSettings ReadSaved(string name)
        {
            try { return Profiles.Load(name, ctx.Settings); }
            catch (Exception e) when (e is SettingsException || e is IOException || e is UnauthorizedAccessException)
            {
                ctx.Log.Warn("VR settings profile '" + name + "' could not be read: " + e.Message);
                return null;
            }
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
            // While a rename is not finished, a folder made now could stand where the profile's own controls are going.
            if (renameHeld && name.Length > 0)
            {
                if (!quiet)
                    MessageBox.Show(this, "A rename of a VR settings profile is not finished. It finishes the next time the launcher starts; the controls can be changed then.",
                        "VR settings profile", MessageBoxButtons.OK, MessageBoxIcon.Information);
                return false;
            }
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

        /// <summary>
        /// Duplicate...: a new profile with the settings shown and a copy of the selected profile's controls, then picked. Unsaved
        /// changes can be saved into the selected profile first; if not, the selected profile stays as it was saved.
        /// </summary>
        private void DuplicateProfile()
        {
            var from = ctx.Settings.Profile;
            if (from.Length == 0) return;
            if (profileUnsaved)
            {
                var answer = MessageBox.Show(this, "Save your changes to the VR settings profile " + from + " first?\n\nThe new profile gets the settings shown either way.",
                    "Duplicate VR settings profile", MessageBoxButtons.YesNoCancel, MessageBoxIcon.Question);
                if (answer == DialogResult.Cancel || (answer == DialogResult.Yes && !WriteProfile(from))) return;
            }
            const string suffix = " copy";
            var copy = (from.Length + suffix.Length <= ProfileStore.MaxNameLength ? from : from.Substring(0, ProfileStore.MaxNameLength - suffix.Length).TrimEnd()) + suffix;
            var name = AskProfileName("Duplicate VR settings profile", "Duplicate", copy,
                n => Profiles.Exists(n) ? "A VR settings profile called " + n + " exists already." : null);
            if (name == null || !StartControls(name, from) || !WriteProfile(name)) return;
            ctx.Settings.Profile = name;
            SaveSettings(); // launcher.ini names the profile in use
            FillProfiles();
            ShowControlsState();
            ctx.Log.Info("VR settings profile '" + from + "' duplicated as '" + name + "'");
        }

        /// <summary>
        /// Rename...: the selected profile's file and controls folder take the new name, and launcher.ini names it
        /// (<see cref="ProfileRename"/>, which a launcher stopped half-way finishes at its next start).
        /// </summary>
        private void RenameProfile()
        {
            var from = ctx.Settings.Profile;
            if (from.Length == 0) return;
            var renamer = new ProfileRename(Profiles, ctx.ControlSets);
            var to = AskProfileName("Rename VR settings profile", "Rename", from, n => renamer.WhyNot(from, n));
            if (to == null || to == from) return;
            try { renamer.Start(from, to); }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException || e is ArgumentException)
            {
                ctx.Log.Error("renaming VR settings profile '" + from + "' to '" + to + "' failed: " + e.Message);
                bool unfinished = renamer.Unfinished;
                MessageBox.Show(this, (unfinished
                        ? "The profile could not be renamed, and not all of it could be put back. The rename finishes the next time the launcher starts; until then the profile list is locked."
                        : "The profile could not be renamed; it keeps its name.") + "\n\n" + e.Message, "Rename VR settings profile",
                    MessageBoxButtons.OK, MessageBoxIcon.Warning);
                FillProfiles();
                if (unfinished) HoldProfilesForRename();
                return;
            }
            ctx.Settings.Profile = to;
            SaveSettings(); // launcher.ini names the new name
            try
            {
                // Until launcher.ini names it, the journal stays and the next start finishes the rename.
                if (LauncherSettings.Load(ctx.Paths.SettingsFile).Profile == to) renamer.Done();
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException || e is SettingsException)
            {
                ctx.Log.Warn("the rename of VR settings profile '" + to + "' is finished again at the next start: " + e.Message);
            }
            FillProfiles();
            if (renamer.Unfinished) HoldProfilesForRename();
            ShowControlsState();
            ctx.Log.Info("VR settings profile '" + from + "' renamed to '" + to + "'; controls in " + ctx.Controls.Dir);
        }

        /// <summary>A small dialog for a new profile's name; null when cancelled.</summary>
        private string AskProfileName() =>
            AskProfileName("New VR settings profile", "Save", string.Empty, n => null);

        /// <summary>
        /// A small dialog for a profile's name, titled <paramref name="title"/>, its button <paramref name="accept"/>, the box
        /// holding <paramref name="initial"/>; a name <paramref name="whyNot"/> refuses (its reason shown) is asked again.
        /// Null when cancelled.
        /// </summary>
        private string AskProfileName(string title, string accept, string initial, Func<string, string> whyNot)
        {
            using (var dialog = new Form
            {
                Text = title, FormBorderStyle = FormBorderStyle.FixedDialog, StartPosition = FormStartPosition.CenterParent,
                MinimizeBox = false, MaximizeBox = false, ShowInTaskbar = false, AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink,
                Font = UiFont(),
            })
            {
                // Sizes are at 96 DPI: the scaling pass at ResumeLayout scales them to the display's (it runs only when the
                // scale is set while the layout is suspended).
                dialog.SuspendLayout();
                dialog.AutoScaleDimensions = new SizeF(96F, 96F);
                dialog.AutoScaleMode = AutoScaleMode.Dpi;
                var prompt = new Label { Text = "Name (up to " + ProfileStore.MaxNameLength + " characters):", AutoSize = true };
                var box = new TextBox { Width = 240, MaxLength = ProfileStore.MaxNameLength, Text = initial ?? string.Empty };
                var ok = new Button { Text = accept, DialogResult = DialogResult.OK, Width = 80, Height = ButtonHeight };
                var cancel = new Button { Text = "Cancel", DialogResult = DialogResult.Cancel, Width = 80, Height = ButtonHeight };
                var buttons = new FlowLayoutPanel { AutoSize = true, FlowDirection = FlowDirection.RightToLeft, Dock = DockStyle.Fill };
                buttons.Controls.AddRange(new Control[] { cancel, ok });
                var layout = new TableLayoutPanel { AutoSize = true, ColumnCount = 1, Padding = new Padding(10) };
                layout.Controls.AddRange(new Control[] { prompt, box, buttons });
                dialog.Controls.Add(layout);
                dialog.AcceptButton = ok;
                dialog.CancelButton = cancel;
                DisplayScale.Follow(dialog);
                dialog.ResumeLayout(false);
                dialog.PerformLayout();
                box.SelectAll();
                while (dialog.ShowDialog(this) == DialogResult.OK)
                {
                    var name = ProfileStore.NormaliseName(box.Text);
                    var why = name == null
                        ? "That name cannot be used: it must not contain \\ / : * ? \" < > | or start or end with a dot."
                        : whyNot(name);
                    if (why == null) return name;
                    MessageBox.Show(dialog, why, title, MessageBoxButtons.OK, MessageBoxIcon.Information);
                }
                return null;
            }
        }
    }
}
