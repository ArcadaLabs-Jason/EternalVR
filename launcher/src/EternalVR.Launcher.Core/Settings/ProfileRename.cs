using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace EternalVR.Launcher.Core.Settings
{
    /// <summary>A rename of a VR settings profile under way: from <see cref="From"/> to <see cref="To"/>.</summary>
    public sealed class RenamePending
    {
        public RenamePending(string from, string to)
        {
            From = from;
            To = to;
        }

        public string From { get; }
        public string To { get; }
    }

    /// <summary>
    /// Renames a VR settings profile: its file in <c>profiles</c>, its controls folder in <c>controls\profiles</c> (with the
    /// README in it), and, by the caller, the <c>profile</c> line of launcher.ini that names it. A journal written before
    /// the first move (<see cref="JournalFile"/>) names the rename until the caller has saved launcher.ini (<see cref="Done"/>):
    /// a launcher stopped half-way finishes it at its next start (<see cref="Recover"/>), so the profile never loses its
    /// controls to a copy of another set. A move that fails puts back what moved and leaves the profile as it was; when even
    /// that cannot finish, the journal stays, no other rename starts (<see cref="WhyNot"/>), no controls are made at a name an
    /// entry is still moving to (<see cref="ControlSets.Adopt"/>), and the next start finishes the rename, setting aside (never
    /// removing) whatever was made at the new name meanwhile. A journal that reads but names no rename (a hand edit, a name
    /// rule changed between versions) is set aside as <see cref="BadJournalFile"/> and locks nothing. A rename that changes
    /// only the case goes through a temporary name, which Windows needs for a folder.
    /// </summary>
    public sealed class ProfileRename
    {
        /// <summary>An entry between a move's two steps. A leading dot: no profile name can have one (<see cref="ProfileStore.NormaliseName"/>).</summary>
        internal const string TempPrefix = ".evr-rename-";
        /// <summary>An entry the next start found at a rename's new name, set aside (<see cref="KeptAside"/>).</summary>
        internal const string KeptPrefix = ".evr-kept-";
        private readonly ProfileStore store;
        private readonly ControlSets controls;
        private readonly List<string> keptAside = new List<string>();

        /// <summary>Who moves what: a rename starting, a start finishing one, or a failed one put back.</summary>
        private enum Mode { Start, Recover, PutBack }

        public ProfileRename(ProfileStore store, ControlSets controls)
        {
            this.store = store ?? throw new ArgumentNullException(nameof(store));
            this.controls = controls ?? throw new ArgumentNullException(nameof(controls));
        }

        /// <summary>Names the rename under way: two lines, the old name and the new one.</summary>
        public string JournalFile => Path.Combine(store.Dir, ".rename-pending");

        /// <summary>Where <see cref="Recover"/> moves a journal that reads but names no rename.</summary>
        public string BadJournalFile => JournalFile + ".bad";

        /// <summary>True when <see cref="Recover"/> set the journal aside as <see cref="BadJournalFile"/>.</summary>
        public bool BadJournalSetAside { get; private set; }

        /// <summary>What <see cref="Recover"/> found at a new name and moved aside (full paths), to log.</summary>
        public IReadOnlyList<string> KeptAside => keptAside;

        /// <summary>
        /// Null when profile <paramref name="from"/> can be renamed to <paramref name="to"/> (a change of case only included),
        /// else why not, in one sentence for the window. Never while another rename is not finished.
        /// </summary>
        public string WhyNot(string from, string to)
        {
            if (Unfinished)
                return "An earlier rename of a VR settings profile is not finished; it finishes the next time the launcher starts.";
            if (ProfileStore.NormaliseName(from) == null) return "There is no VR settings profile selected to rename.";
            var n = ProfileStore.NormaliseName(to);
            if (n == null) return "That name cannot be used: it must not contain \\ / : * ? \" < > | or start or end with a dot.";
            if (string.Equals(n, from.Trim(), StringComparison.OrdinalIgnoreCase)) return null;
            return store.Exists(n) ? "A VR settings profile called " + n + " exists already." : null;
        }

        /// <summary>
        /// Renames profile <paramref name="from"/> to <paramref name="to"/>: writes the journal, then moves the file and the
        /// controls folder. On a failed move what moved is put back (<see cref="PutBack"/>) and the error thrown; the journal is
        /// removed only when nothing is left at a temporary name, else it stays (<see cref="ReadPending"/> then names it) and the
        /// next start finishes the rename. The caller saves launcher.ini with the new name, then calls <see cref="Done"/>.
        /// <paramref name="afterStep"/> runs after each step of each move ("file half", "file", "controls half", "controls"):
        /// tests stop or fail there.
        /// </summary>
        public RenamePending Start(string from, string to, Action<string> afterStep = null)
        {
            if (WhyNot(from, to) is string why) throw new ArgumentException(why);
            var p = new RenamePending(ProfileStore.NormaliseName(from), ProfileStore.NormaliseName(to));
            Directory.CreateDirectory(store.Dir);
            FileUtil.WriteAllTextAtomic(JournalFile, p.From + "\n" + p.To + "\n");
            try
            {
                Move(p, afterStep, Mode.Start);
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException)
            {
                try
                {
                    PutBack(p);
                    if (!HalfMoved(p)) Done();
                }
                catch (Exception again) when (again is IOException || again is UnauthorizedAccessException)
                {
                    // The journal stays: the next start finishes the rename.
                }
                throw;
            }
            return p;
        }

        /// <summary>
        /// Puts a failed rename back: an entry left at the temporary name between a move's two steps goes back to the old
        /// name, then whatever reached the new name. Nothing is removed.
        /// </summary>
        private void PutBack(RenamePending p)
        {
            foreach (var (dir, extension, isFolder) in Places())
            {
                if (!Directory.Exists(dir)) continue;
                var half = Path.Combine(dir, TempPrefix + p.To + extension);
                if (Exists(half, isFolder) && Find(dir, p.From + extension, isFolder) == null)
                    MoveTo(half, Path.Combine(dir, p.From + extension), isFolder);
            }
            Move(new RenamePending(p.To, p.From), null, Mode.PutBack);
        }

        /// <summary>True while an entry of <paramref name="p"/> is at a temporary name, in either direction.</summary>
        public bool HalfMoved(RenamePending p) =>
            Places().Any(place => Directory.Exists(place.Dir) && new[] { p.To, p.From }.Any(n =>
                Exists(Path.Combine(place.Dir, TempPrefix + n + place.Extension), place.IsFolder)));

        /// <summary>Where a profile has entries: its file, and its controls folder.</summary>
        private (string Dir, string Extension, bool IsFolder)[] Places() =>
            new[] { (store.Dir, ".ini", false), (controls.ProfilesDir, string.Empty, true) };

        /// <summary>The rename the journal names; null when there is none, it names none, or it cannot be read.</summary>
        public RenamePending ReadPending() => ReadJournal(out _);

        /// <summary>
        /// True while a rename is not finished: the journal names one, or it is there but cannot be read (it may name one).
        /// A journal that reads but names no rename does not count.
        /// </summary>
        public bool Unfinished => ReadJournal(out bool unreadable) != null || unreadable;

        private RenamePending ReadJournal(out bool unreadable)
        {
            unreadable = false;
            string[] lines;
            try
            {
                if (!File.Exists(JournalFile)) return null;
                lines = File.ReadAllText(JournalFile).Replace("\r\n", "\n").Split('\n');
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException)
            {
                unreadable = true;
                return null;
            }
            if (lines.Length < 2 || !(ProfileStore.NormaliseName(lines[0]) is string from) || !(ProfileStore.NormaliseName(lines[1]) is string to))
                return null;
            return new RenamePending(from, to);
        }

        /// <summary>
        /// Finishes a rename a launcher left half-way: moves what is still at the old name or at a temporary one (either
        /// direction) to the new name, setting aside whatever is at the new name (<see cref="KeptAside"/>), and, when
        /// <paramref name="settings"/> (launcher.ini's) names the old profile, names the new one. The pending rename, or null
        /// when there was none; the caller saves the settings, then calls <see cref="Done"/> when <see cref="HalfMoved"/> is
        /// false. A journal that reads but names no rename is moved to <see cref="BadJournalFile"/> (replacing an older one).
        /// </summary>
        public RenamePending Recover(LauncherSettings settings)
        {
            if (!(ReadJournal(out bool unreadable) is RenamePending p))
            {
                if (!unreadable && File.Exists(JournalFile))
                {
                    // In one step: an older one is replaced only by a move that succeeds.
                    if (File.Exists(BadJournalFile)) File.Replace(JournalFile, BadJournalFile, null);
                    else File.Move(JournalFile, BadJournalFile);
                    BadJournalSetAside = true;
                }
                return null;
            }
            Move(p, null, Mode.Recover);
            if (string.Equals(settings.Profile, p.From, StringComparison.OrdinalIgnoreCase)) ProfileEdits.Rename(settings, p.To);
            return p;
        }

        /// <summary>
        /// At start, before anything reads the profiles: finishes a rename the last launcher left (<see cref="Recover"/>), saves
        /// launcher.ini through <paramref name="save"/>, and removes the journal once nothing is left half-way. What happened
        /// goes to <paramref name="info"/> and <paramref name="warn"/>; a failure is logged and tried again at the next start.
        /// True while a rename is still not finished (<see cref="Unfinished"/>).
        /// </summary>
        public bool FinishAtStart(LauncherSettings settings, Action save, Action<string> info, Action<string> warn)
        {
            try
            {
                var p = Recover(settings);
                if (BadJournalSetAside)
                    warn("a VR settings profile rename journal named no rename; it is kept as " + BadJournalFile + " and nothing is locked");
                if (p == null) return Unfinished;
                foreach (var kept in KeptAside)
                    warn("VR settings profile rename: what was at the new name '" + p.To + "' is kept aside as " + kept);
                save();
                if (HalfMoved(p))
                {
                    warn("VR settings profile rename '" + p.From + "' to '" + p.To + "' is still half-way; it is tried again at the next start");
                    return Unfinished;
                }
                Done();
                info("VR settings profile rename finished: '" + p.From + "' is now '" + p.To + "' (the last launcher stopped before it was done)");
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException || e is SettingsException)
            {
                warn("finishing the rename of a VR settings profile failed, it is tried again at the next start: " + e.Message);
            }
            return Unfinished;
        }

        /// <summary>Removes the journal: launcher.ini names the new profile.</summary>
        public void Done()
        {
            if (File.Exists(JournalFile)) File.Delete(JournalFile);
        }

        /// <summary>
        /// Moves the profile's file and then its controls folder. The controls README is written for the new name when it can
        /// be; a README that cannot be written never stops a rename (the launcher writes it again at Edit controls and Open
        /// folder).
        /// </summary>
        private void Move(RenamePending p, Action<string> afterStep, Mode mode)
        {
            MoveEntry(store.Dir, p, ".ini", false, mode, () => afterStep?.Invoke("file half"));
            afterStep?.Invoke("file");
            var folder = MoveEntry(controls.ProfilesDir, p, string.Empty, true, mode, () => afterStep?.Invoke("controls half"));
            if (folder != null && File.Exists(Path.Combine(folder, ControlsFolder.ReadmeName)))
            {
                try { FileUtil.WriteAllTextAtomic(Path.Combine(folder, ControlsFolder.ReadmeName), ControlsFolder.ReadmeFor(p.To)); }
                catch (Exception e) when (e is IOException || e is UnauthorizedAccessException) { }
            }
            afterStep?.Invoke("controls");
        }

        /// <summary>
        /// Moves <paramref name="p"/>'s old entry (file or folder, its name's case ignored) in <paramref name="dir"/> to the new
        /// name, through <see cref="TempPrefix"/> (<paramref name="half"/> runs between the two steps), and returns where it is
        /// now; null when there is none. A move stopped between its steps is finished first. An entry already at the new name:
        /// <see cref="Mode.Start"/> replaces a folder a deleted profile left there (as a new profile does,
        /// <see cref="ControlSets.StartFrom"/>), once the old entry has been moved aside, and stops at a file;
        /// <see cref="Mode.Recover"/> sets it aside; <see cref="Mode.PutBack"/> stops. <see cref="Mode.Recover"/> also takes an
        /// entry a failed put-back left at the old name's temporary name as the old entry.
        /// </summary>
        private string MoveEntry(string dir, RenamePending p, string extension, bool isFolder, Mode mode, Action half)
        {
            if (!Directory.Exists(dir)) return null;
            var target = Path.Combine(dir, p.To + extension);
            var temp = Path.Combine(dir, TempPrefix + p.To + extension);
            if (Exists(temp, isFolder))
            {
                if (mode == Mode.Recover && Find(dir, p.To + extension, isFolder) is string made) SetAside(dir, made, p.To, extension, isFolder);
                if (Find(dir, p.To + extension, isFolder) == null)
                {
                    MoveTo(temp, target, isFolder);
                    return target;
                }
            }
            var old = Find(dir, p.From + extension, isFolder);
            if (old == null && mode == Mode.Recover && Exists(Path.Combine(dir, TempPrefix + p.From + extension), isFolder))
                old = TempPrefix + p.From + extension;
            if (old != null && old != p.To + extension)
            {
                var other = Find(dir, p.To + extension, isFolder);
                // A different entry already at the new name (not the old one in another case).
                bool leftover = other != null && !string.Equals(other, p.From + extension, StringComparison.OrdinalIgnoreCase);
                if (leftover && mode == Mode.Recover)
                {
                    SetAside(dir, other, p.To, extension, isFolder);
                    leftover = false;
                }
                if (leftover && (!isFolder || mode != Mode.Start)) return null;
                MoveTo(Path.Combine(dir, old), temp, isFolder);
                half();
                // Removed only once the move it makes room for has begun: a move that fails leaves it.
                if (leftover) FileUtil.DeleteDirectory(Path.Combine(dir, other));
            }
            if (Exists(temp, isFolder)) MoveTo(temp, target, isFolder);
            return Find(dir, p.To + extension, isFolder) is string now ? Path.Combine(dir, now) : null;
        }

        /// <summary>Moves <paramref name="name"/> in <paramref name="dir"/> to the first free <c>.evr-kept-&lt;profile&gt;-&lt;n&gt;</c>.</summary>
        private void SetAside(string dir, string name, string profile, string extension, bool isFolder)
        {
            for (int n = 1; ; n++)
            {
                var aside = Path.Combine(dir, KeptPrefix + profile + "-" + n + extension);
                if (File.Exists(aside) || Directory.Exists(aside)) continue;
                MoveTo(Path.Combine(dir, name), aside, isFolder);
                keptAside.Add(aside);
                return;
            }
        }

        /// <summary>The entry's name as it is on disk, its case ignored; null when there is none.</summary>
        private static string Find(string dir, string name, bool isFolder) =>
            (isFolder ? Directory.GetDirectories(dir) : Directory.GetFiles(dir)).Select(Path.GetFileName)
            .FirstOrDefault(n => string.Equals(n, name, StringComparison.OrdinalIgnoreCase));

        private static bool Exists(string path, bool isFolder) => isFolder ? Directory.Exists(path) : File.Exists(path);

        private static void MoveTo(string from, string to, bool isFolder)
        {
            if (isFolder) Directory.Move(from, to);
            else File.Move(from, to);
        }
    }
}
