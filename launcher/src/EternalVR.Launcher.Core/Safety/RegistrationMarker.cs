using System;
using System.Collections.Generic;
using System.Linq;

namespace EternalVR.Launcher.Core.Safety
{
    /// <summary>
    /// The lines of <c>REGISTRATION_PENDING</c>, written before the HKCU implicit-layer value of the <c>--register-hkcu</c>
    /// route (T-093). One line per registered value (the manifest path, which is the value's name), and one
    /// <c>created key: &lt;key&gt;</c> line per registry key the registration had to create, so that the clean-up can delete
    /// those keys again once they are empty and an uninstall leaves the registry as it found it. A launcher from before the
    /// key lines reads one as the name of a value that does not exist and skips it.
    /// </summary>
    public static class RegistrationMarker
    {
        public const string CreatedKeyPrefix = "created key: ";

        /// <summary>The marker lines for one registered value and the keys created for it (shallowest first).</summary>
        public static IEnumerable<string> Lines(string valueName, IEnumerable<string> createdKeys) =>
            new[] { valueName }.Concat(createdKeys.Select(k => CreatedKeyPrefix + k));

        /// <summary>
        /// The value names and the created keys a marker names, each once (case ignored). The keys come deepest first,
        /// the order in which they can be deleted. Only <paramref name="layersKey"/> and its ancestors below the root key
        /// (the only keys <see cref="MissingKeys"/> can name for it) are taken: any other <c>created key:</c> line (a
        /// damaged or foreign marker) goes to <paramref name="ignored"/> and is never deleted.
        /// </summary>
        public static void Parse(IEnumerable<string> lines, string layersKey, out IReadOnlyList<string> valueNames,
                                 out IReadOnlyList<string> createdKeys, out IReadOnlyList<string> ignored)
        {
            var trimmed = lines.Select(l => l.Trim()).Where(l => l.Length > 0).ToList();
            valueNames = trimmed.Where(l => !l.StartsWith(CreatedKeyPrefix, StringComparison.Ordinal))
                .Distinct(StringComparer.OrdinalIgnoreCase).ToList();
            var named = trimmed.Where(l => l.StartsWith(CreatedKeyPrefix, StringComparison.Ordinal))
                .Select(l => l.Substring(CreatedKeyPrefix.Length).Trim().Trim('\\'))
                .Distinct(StringComparer.OrdinalIgnoreCase)
                .ToList();
            var allowed = new HashSet<string>(MissingKeys(layersKey, _ => false), StringComparer.OrdinalIgnoreCase);
            createdKeys = named.Where(allowed.Contains).OrderByDescending(k => k.Split('\\').Length).ToList();
            ignored = named.Where(k => !allowed.Contains(k)).ToList();
        }

        /// <summary>
        /// The keys on the path to <paramref name="key"/> (itself included, its first segment not: that is a root key
        /// such as SOFTWARE) that do not exist yet, shallowest first: what creating <paramref name="key"/> would create.
        /// </summary>
        public static IReadOnlyList<string> MissingKeys(string key, Func<string, bool> exists)
        {
            var parts = key.Split(new[] { '\\' }, StringSplitOptions.RemoveEmptyEntries);
            var missing = new List<string>();
            for (int i = 2; i <= parts.Length; i++)
            {
                var path = string.Join("\\", parts.Take(i));
                if (missing.Count > 0 || !exists(path)) missing.Add(path);
            }
            return missing;
        }
    }
}
