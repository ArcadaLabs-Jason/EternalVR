using System;
using System.Collections;
using System.Collections.Generic;
using System.Collections.Specialized;
using System.Diagnostics;
using System.Linq;

namespace EternalVR.Launcher.Core.Launch
{
    /// <summary>
    /// The environment of the processes the launcher starts (the game, the Steam resync launch, the headset probe): a
    /// merged copy of the launcher's own with the launch's variables, set on the start info. The launcher's own
    /// environment is never changed.
    /// <para>
    /// Windows names are not case-sensitive, but a block built by Git Bash or MSYS can hold two names that differ only in
    /// case (<c>TMP</c> and <c>tmp</c>). .NET Framework then throws "Item has already been added" at the first use of
    /// <see cref="ProcessStartInfo.EnvironmentVariables"/>, and no process could be started. <see cref="Apply"/> takes the
    /// start info's dictionary even then (<see cref="Open"/>) and fills it from <see cref="ForChild"/>, which keeps one of
    /// each such name with the value Windows itself returns for it, the one every child would have read anyway.
    /// </para>
    /// </summary>
    public static class ChildEnvironment
    {
        /// <summary>The prefixes of inherited variables a launch logs: the OpenXR loader's, the Vulkan loader's and the layer's.</summary>
        public static readonly string[] LoggedPrefixes = { "XR_", "VK_", "ETERNALVR_" };

        /// <summary>The names that differ only in case, a group per name in the order given (the first stays).</summary>
        public static IReadOnlyList<IReadOnlyList<string>> CaseDuplicates(IEnumerable<string> names) =>
            (names ?? Enumerable.Empty<string>())
                .GroupBy(n => n, StringComparer.OrdinalIgnoreCase)
                .Where(g => g.Count() > 1)
                .Select(g => (IReadOnlyList<string>)g.ToList())
                .ToList();

        /// <summary>
        /// The environment a child gets: <paramref name="inherited"/> with names compared without case, then
        /// <paramref name="changes"/> in order (a null value removes the name). Of names that differ only in case, the one
        /// whose value <paramref name="resolve"/> returns for the name stays (Windows' own lookup); without it, or when it
        /// matches none, the first.
        /// </summary>
        public static IReadOnlyDictionary<string, string> Merge(IEnumerable<KeyValuePair<string, string>> inherited,
                                                               IEnumerable<KeyValuePair<string, string>> changes,
                                                               Func<string, string> resolve = null)
        {
            var env = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            foreach (var group in (inherited ?? Enumerable.Empty<KeyValuePair<string, string>>()).GroupBy(kv => kv.Key, StringComparer.OrdinalIgnoreCase))
            {
                var first = group.First();
                if (resolve != null && group.Skip(1).Any())
                {
                    var value = resolve(first.Key);
                    first = group.Where(kv => kv.Value == value).DefaultIfEmpty(first).First();
                }
                env[first.Key] = first.Value;
            }
            foreach (var kv in changes ?? Enumerable.Empty<KeyValuePair<string, string>>())
            {
                // The launch's spelling of the name replaces an inherited one that differs only in case.
                env.Remove(kv.Key);
                if (kv.Value != null) env[kv.Key] = kv.Value;
            }
            return env;
        }

        /// <summary>
        /// The inherited variables the launch passes on unchanged that the OpenXR loader, the Vulkan loader or the layer read
        /// (<see cref="LoggedPrefixes"/>), sorted by name; for the launch plan's log, so a report shows them.
        /// </summary>
        public static IReadOnlyList<KeyValuePair<string, string>> Inherited(IEnumerable<KeyValuePair<string, string>> inherited,
                                                                           IEnumerable<KeyValuePair<string, string>> changes)
        {
            var set = new HashSet<string>((changes ?? Enumerable.Empty<KeyValuePair<string, string>>()).Select(kv => kv.Key), StringComparer.OrdinalIgnoreCase);
            return Merge(inherited, null)
                .Where(kv => !set.Contains(kv.Key) && LoggedPrefixes.Any(p => kv.Key.StartsWith(p, StringComparison.OrdinalIgnoreCase)))
                .OrderBy(kv => kv.Key, StringComparer.OrdinalIgnoreCase)
                .ToList();
        }

        /// <summary>The launcher's own environment as name and value pairs (both spellings of a name that differs only in case).</summary>
        public static IReadOnlyList<KeyValuePair<string, string>> Current()
        {
            var list = new List<KeyValuePair<string, string>>();
            foreach (DictionaryEntry e in Environment.GetEnvironmentVariables())
                list.Add(new KeyValuePair<string, string>((string)e.Key, (string)e.Value));
            return list;
        }

        /// <summary>The environment of a process started now with <paramref name="changes"/> (<see cref="Merge"/> of <see cref="Current"/>).</summary>
        public static IReadOnlyDictionary<string, string> ForChild(IEnumerable<KeyValuePair<string, string>> changes) =>
            Merge(Current(), changes, Environment.GetEnvironmentVariable);

        /// <summary>Sets the start info's environment to <see cref="ForChild"/> (a null value in <paramref name="changes"/> removes the variable).</summary>
        public static void Apply(ProcessStartInfo psi, IEnumerable<KeyValuePair<string, string>> changes) =>
            Fill(Open(() => psi.EnvironmentVariables), ForChild(changes));

        /// <summary>
        /// The start info's environment dictionary. Its first use copies the launcher's environment, and .NET Framework
        /// throws <see cref="ArgumentException"/> on a name that differs from another only in case; the dictionary is kept
        /// all the same (partly filled), and the second use returns it. <see cref="Fill"/> then replaces its contents.
        /// </summary>
        public static StringDictionary Open(Func<StringDictionary> get)
        {
            try { return get(); }
            catch (ArgumentException) { return get(); }
        }

        /// <summary>Replaces everything in <paramref name="target"/> with <paramref name="env"/>.</summary>
        public static void Fill(StringDictionary target, IReadOnlyDictionary<string, string> env)
        {
            target.Clear();
            foreach (var kv in env) target[kv.Key] = kv.Value;
        }
    }
}
