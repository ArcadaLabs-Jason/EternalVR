using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Windows.Forms;
using EternalVR.Launcher.Core.Settings;

namespace EternalVR.Launcher.UiShots
{
    /// <summary>
    /// The VR settings profile strip used as a player would, on the fake tree: with profile Riley picked (five settings changed
    /// from Recommended), one setting is put back from its row, the profile saved, duplicated and the copy renamed, each
    /// name dialog answered by typing a name and pressing its button. Each check is a line of <c>profile-flow.txt</c>,
    /// <c>ok</c> or <c>FAIL</c>.
    /// </summary>
    internal sealed class ProfileFlow
    {
        private readonly MainForm main;
        private readonly LauncherContext ctx;
        private readonly Action<int> pump;
        private readonly List<string> lines = new List<string>();

        public ProfileFlow(MainForm main, LauncherContext ctx, Action<int> pump)
        {
            this.main = main;
            this.ctx = ctx;
            this.pump = pump;
        }

        public bool Failed => lines.Any(l => l.StartsWith("FAIL", StringComparison.Ordinal));

        public IReadOnlyList<string> Run()
        {
            var store = new ProfileStore(ctx.Paths.Profiles);
            var state = Reflect.Get<Label>(main, "presetState");
            Check("Riley is picked", ctx.Settings.Profile == "Riley");
            Check("the strip counts five changes: " + state.Text, state.Text.Contains("5 changes"));
            var turning = Row(Setting.Turning);
            Check("Turning is marked: " + Label(turning).Text, Label(turning).Text.EndsWith("•", StringComparison.Ordinal));
            Check("Eye height is not marked: " + Label(Row(Setting.EyeHeight)).Text, !Label(Row(Setting.EyeHeight)).Text.Contains("•"));

            Reflect.Call(main, "ResetRow", turning);
            pump(300);
            Check("Turning put back to Smooth", ctx.Settings.Turn == TurnMode.Smooth && !Label(turning).Text.Contains("•"));
            Check("the strip counts four: " + state.Text, state.Text.Contains("4 changes"));
            Check("Vignette kept: " + ctx.Settings.Vignette, ctx.Settings.Vignette == VignetteMode.Light);
            var save = Reflect.Get<Button>(main, "saveProfile");
            Check("Save shows unsaved changes: " + save.Text, save.Text == "Save *");
            var unsaved = LauncherSettings.Load(ctx.Paths.SettingsFile).ProfileUnsaved;
            Check("launcher.ini lists the unsaved change: " + (unsaved == null ? "no line" : string.Join(",", unsaved)),
                unsaved != null && unsaved.SequenceEqual(new[] { "turn" }));

            Reflect.Call(main, "SaveProfile");
            unsaved = LauncherSettings.Load(ctx.Paths.SettingsFile).ProfileUnsaved;
            Check("after Save launcher.ini lists none", unsaved != null && unsaved.Count == 0);
            var text = File.ReadAllText(store.ExistingFile("Riley"));
            Check("the saved file has only the changes", !text.Contains("turn =") && text.Contains("vignette = light") && text.Contains("preset = recommended")
                && !text.Contains("turn_rate"));

            Answer("Riley dup");
            Reflect.Call(main, "DuplicateProfile");
            pump(300);
            Check("Duplicate made and picked Riley dup", ctx.Settings.Profile == "Riley dup" && store.Exists("Riley dup") && store.Exists("Riley"));
            Check("the copy has Riley's controls folder", ctx.ControlSets.HasOwn("Riley dup"));
            Check("the copy is based on Recommended with the same changes",
                ProfileEdits.Changed(store.Load("Riley dup", ctx.Settings), SettingsPresets.Recommended.Values(), ctx.Data.CpuSaver)
                    .SetEquals(ProfileEdits.Changed(store.Load("Riley", ctx.Settings), SettingsPresets.Recommended.Values(), ctx.Data.CpuSaver)));

            Answer("Morgan");
            Reflect.Call(main, "RenameProfile");
            pump(300);
            Check("Rename: Morgan picked, Riley dup gone", ctx.Settings.Profile == "Morgan" && store.Exists("Morgan") && !store.Exists("Riley dup"));
            Check("Rename: the controls folder moved", ctx.ControlSets.HasOwn("Morgan") && !Directory.Exists(Path.Combine(ctx.ControlSets.ProfilesDir, "Riley dup")));
            Check("Rename: launcher.ini names Morgan", LauncherSettings.Load(ctx.Paths.SettingsFile).Profile == "Morgan");
            Check("Rename: no journal left", !File.Exists(Path.Combine(store.Dir, ".rename-pending")));
            var list = Reflect.Get<ComboBox>(main, "profile");
            Check("the list: " + string.Join(", ", list.Items.Cast<object>()), list.Items.Contains("Morgan") && !list.Items.Contains("Riley dup"));
            Check("launcher.ini's list holds after the rename", ProfileEdits.ListHolds(LauncherSettings.Load(ctx.Paths.SettingsFile)));

            // A hand edit to launcher.ini while the window is open: its next save merges it, then lists it as not saved into
            // the profile, with a check value that holds (T-134).
            var path = ctx.Paths.SettingsFile;
            var written = File.ReadAllText(path);
            File.WriteAllText(path, written.Replace("bhaptics_intensity = 1.00", "bhaptics_intensity = 0.50"));
            Reflect.Call(main, "SaveSettings");
            var after = LauncherSettings.Load(path);
            Check("a hand edit is merged and listed: " + (after.ProfileUnsaved == null ? "no list" : string.Join(",", after.ProfileUnsaved)),
                written.Contains("bhaptics_intensity = 1.00") && after.BhapticsIntensity == 0.5 && ctx.Settings.BhapticsIntensity == 0.5
                && after.ProfileUnsaved != null && after.ProfileUnsaved.Contains("bhaptics_intensity") && ProfileEdits.ListHolds(after));

            list.SelectedIndex = 0;
            pump(300);
            Check("back to (none)", ctx.Settings.Profile.Length == 0);
            return lines;
        }

        private void Check(string what, bool ok) => lines.Add((ok ? "ok   " : "FAIL ") + what);

        /// <summary>The window's row of <paramref name="id"/> (its private SettingRow).</summary>
        private object Row(Setting id)
        {
            var rows = (IEnumerable)typeof(MainForm).GetField("rows", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic).GetValue(main);
            return rows.Cast<object>().First(r => (Setting)r.GetType().GetField("Id").GetValue(r) == id);
        }

        private static Label Label(object row) => (Label)row.GetType().GetField("Label").GetValue(row);

        /// <summary>The next name dialog gets <paramref name="name"/> typed in and its button pressed.</summary>
        private void Answer(string name)
        {
            var timer = new Timer { Interval = 200 };
            timer.Tick += (s, e) =>
            {
                var dialog = Application.OpenForms.Cast<Form>().LastOrDefault(f => f != main && f.Visible && f.Modal);
                if (dialog == null) return;
                timer.Stop();
                timer.Dispose();
                var box = All(dialog).OfType<TextBox>().First();
                box.Text = name;
                ((Button)dialog.AcceptButton).PerformClick();
            };
            timer.Start();
        }

        private static IEnumerable<Control> All(Control c) => c.Controls.Cast<Control>().SelectMany(child => new[] { child }.Concat(All(child)));
    }
}
