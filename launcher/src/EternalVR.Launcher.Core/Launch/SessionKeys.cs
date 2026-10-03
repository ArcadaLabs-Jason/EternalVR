using System;
using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Data;

namespace EternalVR.Launcher.Core.Launch
{
    /// <summary>
    /// Keys a VR session makes the game write that are not forced on the command line: the window and
    /// display keys (the layer places the stereo window, and the game saves its position and mode on
    /// exit). Kept as data in <c>data\session-keys.txt</c>, one name per line. The settings restore
    /// puts these back together with the forced cvars (<see cref="RestoredKeys"/>).
    /// </summary>
    public sealed class SessionKeys
    {
        public SessionKeys(IEnumerable<string> names) { Names = names.ToList(); }

        public IReadOnlyList<string> Names { get; }

        public static SessionKeys Parse(string text)
        {
            var list = new List<string>();
            foreach (var r in DataFile.ParseRecords(text))
            {
                var name = DataFile.Field(r, 0);
                if (r.Length > 1) throw new FormatException("session-keys: one name per line: " + string.Join("|", r));
                if (name.Length == 0 || name.StartsWith("+", StringComparison.Ordinal) || name.Contains(" "))
                    throw new FormatException("session-keys: bad cvar name: " + name);
                if (list.Contains(name, StringComparer.OrdinalIgnoreCase))
                    throw new FormatException("session-keys: listed twice: " + name);
                list.Add(name);
            }
            return new SessionKeys(list);
        }

        /// <summary>
        /// The cvar names Extra game arguments set (<c>+name value</c>, also as <c>+ name</c>, <c>+set name</c> and
        /// <c>+seta name</c>), without repeats: the game saves them to its config on exit like the forced ones, so the
        /// session's restore puts them back too (<see cref="Safety.SettingsSnapshot.Take"/> records them). A command such
        /// as <c>+map</c> is listed as well; with no such key in a config, its restore changes nothing.
        /// </summary>
        public static IReadOnlyList<string> FromArguments(string extraArguments)
        {
            var names = new List<string>();
            var args = LaunchPlanBuilder.SplitArguments(extraArguments);
            for (int i = 0; i < args.Count; i++)
            {
                if (!args[i].StartsWith("+", StringComparison.Ordinal)) continue;
                var name = args[i].Substring(1);
                if (name.Length == 0 && i + 1 < args.Count) name = args[++i];
                if ((string.Equals(name, "set", StringComparison.OrdinalIgnoreCase) || string.Equals(name, "seta", StringComparison.OrdinalIgnoreCase))
                    && i + 1 < args.Count)
                    name = args[++i];
                if (IsCvarName(name) && !names.Contains(name, StringComparer.OrdinalIgnoreCase)) names.Add(name);
            }
            return names;
        }

        private static bool IsCvarName(string name) =>
            name.Length > 0 && (char.IsLetter(name[0]) || name[0] == '_') && name.All(c => (c < 128 && char.IsLetterOrDigit(c)) || c == '_');

        /// <summary>Every key the settings restore puts back: the forced cvars, then these, then <paramref name="more"/>, without repeats.</summary>
        public static IReadOnlyList<string> RestoredKeys(ForcedCvars forced, SessionKeys session, IEnumerable<string> more = null) =>
            forced.Names.Concat(session == null ? Enumerable.Empty<string>() : session.Names)
                .Concat(more ?? Enumerable.Empty<string>())
                .Distinct(StringComparer.OrdinalIgnoreCase).ToList();
    }
}
