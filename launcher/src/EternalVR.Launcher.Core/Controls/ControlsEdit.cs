using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using EternalVR.Launcher.Core.Settings;

namespace EternalVR.Launcher.Core.Controls
{
    /// <summary>A kind of controller with a built-in controller file (a copy in the controls folder's <c>defaults\</c>).</summary>
    public sealed class ControllerFamily
    {
        public ControllerFamily(string defaultsFile, string profilePath)
        {
            DefaultsFile = defaultsFile;
            FileName = Path.GetFileName(defaultsFile);
            Name = ControlNames.FamilyName(FileName);
            ProfilePath = profilePath;
        }

        public string DefaultsFile { get; }
        public string FileName { get; }
        public string Name { get; }
        /// <summary>The OpenXR interaction profile the file names; the layer matches a player's file to it by this.</summary>
        public string ProfilePath { get; }
        public override string ToString() => Name;
    }

    public enum ControlsSaveResult
    {
        /// <summary>The player's file was written.</summary>
        Saved,
        /// <summary>The controls are the built-in ones and there was no file of the player's: nothing was written.</summary>
        BuiltIn,
        /// <summary>The controls are the built-in ones again: the player's file was deleted.</summary>
        Removed,
    }

    /// <summary>
    /// Editing the controls of one kind of controller: the player's file for it in the controls folder when there is one,
    /// else a copy of the built-in file. Saving writes to the player's folder only, never to <c>defaults\</c> (which the
    /// launcher replaces), and refuses controls the layer would reject.
    /// </summary>
    public sealed class ControlsEdit
    {
        private readonly ControlsFolder folder;
        private string savedText;

        private ControlsEdit(ControlsFolder folder, ControllerFamily family, ControllerMapFile defaults, string playerFile, string text)
        {
            this.folder = folder;
            Family = family;
            Defaults = defaults;
            PlayerFile = playerFile;
            File = ControllerMapFile.Parse(text);
            savedText = text;
        }

        /// <summary>
        /// Opens the controls of <paramref name="family"/>: the player's file the layer would use for it (the last by name of
        /// those naming its profile), else the built-in file. Throws an <see cref="IOException"/> when a file cannot be read.
        /// </summary>
        public static ControlsEdit Open(ControlsFolder folder, ControllerFamily family)
        {
            var defaultsText = ReadText(family.DefaultsFile);
            var player = folder.PlayerFileFor(family.ProfilePath);
            return new ControlsEdit(folder, family, ControllerMapFile.Parse(defaultsText), player, player != null ? ReadText(player) : defaultsText);
        }

        /// <summary>A file's text as it is, a byte order mark included, so it is written back the same.</summary>
        internal static string ReadText(string path) => Encoding.UTF8.GetString(System.IO.File.ReadAllBytes(path));

        public ControllerFamily Family { get; }
        /// <summary>The controls being edited.</summary>
        public ControllerMapFile File { get; private set; }
        /// <summary>The built-in controls of this kind of controller.</summary>
        public ControllerMapFile Defaults { get; }
        /// <summary>The player's file being edited, or null while the controls are the built-in ones.</summary>
        public string PlayerFile { get; private set; }
        /// <summary>Where <see cref="Save"/> writes: the player's file, or a new one named like the built-in file.</summary>
        public string SavePath => PlayerFile ?? Path.Combine(folder.Dir, Family.FileName);
        /// <summary>True when there are changes not saved yet.</summary>
        public bool IsModified => File.ToText() != savedText;

        // ---- One map ----

        /// <summary>The value of <paramref name="key"/> in the map for <paramref name="hand"/>, or null.</summary>
        public string Get(Handedness hand, BindingKey key) => File.Get(ControlNames.MapSection(hand), key.ToString());

        /// <summary>Binds <paramref name="action"/> (an action name) to a button or a gesture; null or "none" leaves it free.</summary>
        public void SetAction(Handedness hand, BindingKey key, string action)
        {
            if (key.Kind != BindingKeyKind.Button && key.Kind != BindingKeyKind.Gesture) throw new ArgumentException("not a button or a gesture", nameof(key));
            var section = ControlNames.MapSection(hand);
            if (string.IsNullOrEmpty(action) || action == ControlNames.Unbound) File.Remove(section, key.ToString());
            else File.Set(section, key.ToString(), action);
        }

        /// <summary>A stick's role in the map for <paramref name="hand"/> (None when it has none, or one the layer does not know).</summary>
        public StickRole Role(Handedness hand, ControlHand stick) =>
            ControlNames.TryParseRole(Get(hand, BindingKey.Role(stick)), out var role) ? role : StickRole.None;

        /// <summary>The turn stick, as the layer picks it (the left one when both are set to turn), or null.</summary>
        public ControlHand? TurnStick(Handedness hand) =>
            Role(hand, ControlHand.Left) == StickRole.Turn ? ControlHand.Left
            : Role(hand, ControlHand.Right) == StickRole.Turn ? ControlHand.Right
            : (ControlHand?)null;

        /// <summary>
        /// Gives a stick a role. The other stick, if it had that role, takes this stick's old one, so the two are never both
        /// Move or both Turn; and when the turn stick changes sides its gestures (up, down tap, down hold) go with it.
        /// </summary>
        public void SetStickRole(Handedness hand, ControlHand stick, StickRole role)
        {
            var section = ControlNames.MapSection(hand);
            var other = stick == ControlHand.Left ? ControlHand.Right : ControlHand.Left;
            var old = Role(hand, stick);
            var oldTurn = TurnStick(hand);
            WriteRole(section, stick, role);
            if (role != StickRole.None && Role(hand, other) == role) WriteRole(section, other, old);
            var newTurn = TurnStick(hand);
            if (oldTurn.HasValue && newTurn.HasValue && oldTurn != newTurn && !Gestures(hand, newTurn.Value).Any())
            {
                foreach (var g in Gestures(hand, oldTurn.Value).ToList())
                {
                    File.Set(section, BindingKey.StickGestureOf(newTurn.Value, g.Gesture).ToString(), g.Action);
                    File.Remove(section, BindingKey.StickGestureOf(oldTurn.Value, g.Gesture).ToString());
                }
            }
        }

        private void WriteRole(string section, ControlHand stick, StickRole role)
        {
            var key = BindingKey.Role(stick).ToString();
            if (role == StickRole.None) File.Remove(section, key);
            else File.Set(section, key, ControlNames.RoleName(role));
        }

        private IEnumerable<(StickGesture Gesture, string Action)> Gestures(Handedness hand, ControlHand stick) =>
            Enum.GetValues(typeof(StickGesture)).Cast<StickGesture>()
                .Select(g => (g, Get(hand, BindingKey.StickGestureOf(stick, g))))
                .Where(g => g.Item2 != null);

        /// <summary>True when the controller has the button (the <c>[profile]</c> section binds it).</summary>
        public bool HasInput(ControlHand hand, ButtonInput input) => InputPath(hand, input) != null;

        /// <summary>
        /// The buttons to show for the map of <paramref name="hand"/>: the left hand's, then the right hand's, each one the
        /// controller has or the map binds.
        /// </summary>
        public IReadOnlyList<(ControlHand Hand, ButtonInput Input)> Inputs(Handedness hand)
        {
            var list = new List<(ControlHand, ButtonInput)>();
            foreach (var h in new[] { ControlHand.Left, ControlHand.Right })
                foreach (ButtonInput input in Enum.GetValues(typeof(ButtonInput)))
                {
                    bool bound = Enum.GetValues(typeof(PressKind)).Cast<PressKind>().Any(p => Get(hand, BindingKey.Button(h, input, p)) != null);
                    if (HasInput(h, input) || bound) list.Add((h, input));
                }
            return list;
        }

        /// <summary>"Right A", "Left trigger", "Right trackpad click": the button's name on this controller.</summary>
        public string InputName(ControlHand hand, ButtonInput input) =>
            ControlNames.Capitalized(hand) + " " + (ControlNames.PhysicalName(InputPath(hand, input)) ?? ControlNames.GenericInputName(input));

        /// <summary>"Left stick" or, on the Vive wands, "Left trackpad".</summary>
        public string StickName(ControlHand hand) =>
            ControlNames.Capitalized(hand) + " " + (ControlNames.PhysicalName(File.Get(ControlNames.ProfileSection, "gameplay." + ControlNames.HandName(hand) + "." + ControlNames.StickAction)) ?? "stick");

        private string InputPath(ControlHand hand, ButtonInput input) =>
            File.Get(ControlNames.ProfileSection, "gameplay." + ControlNames.HandName(hand) + "." + ControlNames.InputActions[(int)input]);

        // ---- The file ----

        /// <summary>Every problem the layer would find: the file's, then each map's (<see cref="ControlMapRules"/>).</summary>
        public IReadOnlyList<ControlIssue> Check()
        {
            var issues = new List<ControlIssue>(File.FileIssues);
            foreach (var section in ControlNames.MapSections.Where(File.HasSection))
                issues.AddRange(ControlMapRules.Check(File.Entries(section), section, InputName, StickName));
            return issues;
        }

        /// <summary>Puts the built-in controls back, all three maps, in the editor only (<see cref="Save"/> keeps them).</summary>
        public void ResetToDefaults() => File = Defaults.Clone();

        /// <summary>
        /// Saves the controls to the player's controls folder. Built-in controls need no file of the player's: an existing one
        /// is deleted (unless another file of the player's names the same controller, which would then be used instead).
        /// Throws an <see cref="InvalidOperationException"/> when <see cref="Check"/> finds problems, and an
        /// <see cref="IOException"/> when the file cannot be written.
        /// </summary>
        public ControlsSaveResult Save()
        {
            var issues = Check();
            if (issues.Count > 0) throw new InvalidOperationException("the controls have problems: " + issues[0].Message);
            var target = Path.GetFullPath(SavePath);
            if (!string.Equals(Path.GetDirectoryName(target), folder.Dir, StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException("controls are saved only in the controls folder, not in " + Path.GetDirectoryName(target));

            var text = File.ToText();
            var others = folder.PlayerFilesFor(Family.ProfilePath).Where(f => !string.Equals(f, PlayerFile, StringComparison.OrdinalIgnoreCase));
            if (File.SameContent(Defaults) && !others.Any())
            {
                var result = ControlsSaveResult.BuiltIn;
                if (PlayerFile != null && System.IO.File.Exists(PlayerFile))
                {
                    System.IO.File.SetAttributes(PlayerFile, FileAttributes.Normal);
                    System.IO.File.Delete(PlayerFile);
                    result = ControlsSaveResult.Removed;
                }
                PlayerFile = null;
                savedText = text;
                return result;
            }
            Directory.CreateDirectory(folder.Dir);
            FileUtil.WriteAllTextAtomic(target, text);
            PlayerFile = target;
            savedText = text;
            return ControlsSaveResult.Saved;
        }
    }
}
