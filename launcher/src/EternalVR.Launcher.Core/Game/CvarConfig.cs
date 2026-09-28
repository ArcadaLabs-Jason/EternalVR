using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace EternalVR.Launcher.Core.Game
{
    /// <summary>
    /// An id Tech text config (<c>DOOMEternalConfig.cfg</c>, <c>DOOMEternalConfig.local</c>) edited line by
    /// line: cvar lines are <c>name "value"</c> (optionally after <c>seta</c>/<c>set</c>); every other line
    /// (binds, comments, <c>configVersion</c>) is kept byte for byte. Line endings are preserved.
    /// </summary>
    public sealed class CvarConfig
    {
        private readonly List<string> lines;
        private readonly string newline;
        private readonly bool trailingNewline;

        private CvarConfig(List<string> lines, string newline, bool trailingNewline)
        {
            this.lines = lines;
            this.newline = newline;
            this.trailingNewline = trailingNewline;
        }

        /// <summary>Commands whose lines are never cvar assignments.</summary>
        private static readonly HashSet<string> Commands = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
        {
            "bind", "bindSecondary", "unbind", "unbindall", "bindset", "exec",
        };

        public static CvarConfig Parse(string text)
        {
            text = text ?? string.Empty;
            string nl = text.Contains("\r\n") ? "\r\n" : "\n";
            bool trailing = text.EndsWith("\n", StringComparison.Ordinal);
            var body = trailing ? text.Substring(0, text.Length - (text.EndsWith("\r\n", StringComparison.Ordinal) ? 2 : 1)) : text;
            var list = body.Length == 0 ? new List<string>() : body.Split(new[] { "\r\n", "\n" }, StringSplitOptions.None).ToList();
            return new CvarConfig(list, nl, trailing || list.Count == 0);
        }

        /// <summary>
        /// Reads a config file byte for byte: every byte maps to one character (ISO-8859-1), so bytes
        /// that are not valid UTF-8 survive a rewrite unchanged; a UTF-8 byte order mark is kept aside.
        /// Cvar names and the forced values are ASCII, the same in every encoding.
        /// </summary>
        public static CvarConfig ParseBytes(byte[] bytes, out bool utf8Bom)
        {
            utf8Bom = bytes.Length >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF;
            int skip = utf8Bom ? 3 : 0;
            return Parse(ByteTransparent.GetString(bytes, skip, bytes.Length - skip));
        }

        /// <summary>The file's bytes as <see cref="ParseBytes"/> read them, with the byte order mark put back.</summary>
        public byte[] ToBytes(bool utf8Bom)
        {
            var body = ByteTransparent.GetBytes(ToString());
            if (!utf8Bom) return body;
            var all = new byte[body.Length + 3];
            all[0] = 0xEF; all[1] = 0xBB; all[2] = 0xBF;
            Buffer.BlockCopy(body, 0, all, 3, body.Length);
            return all;
        }

        private static readonly Encoding ByteTransparent = Encoding.GetEncoding(28591);

        /// <summary>Every cvar and its value, last assignment wins (as when the game executes the file).</summary>
        public IReadOnlyDictionary<string, string> Values
        {
            get
            {
                var map = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
                foreach (var line in lines)
                    if (TryParseLine(line, out var name, out var value)) map[name] = value;
                return map;
            }
        }

        public bool TryGet(string name, out string value) => Values.TryGetValue(name, out value);

        /// <summary>Sets a cvar: rewrites its last assignment (and drops earlier ones), or appends a line.</summary>
        public void Set(string name, string value)
        {
            int last = -1;
            for (int i = 0; i < lines.Count; i++)
                if (TryParseLine(lines[i], out var n, out _) && string.Equals(n, name, StringComparison.OrdinalIgnoreCase)) last = i;

            var formatted = name + " \"" + Escape(value) + "\"";
            if (last < 0)
            {
                lines.Add(formatted);
                return;
            }
            // Keep the original spelling of the name and any seta/set prefix.
            var prefix = Prefix(lines[last]);
            TryParseLine(lines[last], out var original, out _);
            lines[last] = prefix + original + " \"" + Escape(value) + "\"";
            for (int i = last - 1; i >= 0; i--)
                if (TryParseLine(lines[i], out var n, out _) && string.Equals(n, name, StringComparison.OrdinalIgnoreCase)) lines.RemoveAt(i);
        }

        /// <summary>Removes every assignment of a cvar. Returns true if one was removed.</summary>
        public bool Remove(string name)
        {
            int removed = lines.RemoveAll(l => TryParseLine(l, out var n, out _) && string.Equals(n, name, StringComparison.OrdinalIgnoreCase));
            return removed > 0;
        }

        public override string ToString()
        {
            var sb = new StringBuilder();
            for (int i = 0; i < lines.Count; i++)
            {
                sb.Append(lines[i]);
                if (i < lines.Count - 1 || trailingNewline) sb.Append(newline);
            }
            return sb.ToString();
        }

        /// <summary>
        /// Parses a cvar assignment line. Accepts <c>name "value"</c>, <c>name value</c> and a leading
        /// <c>seta</c> or <c>set</c>. Returns false for comments, commands and blank lines.
        /// </summary>
        public static bool TryParseLine(string line, out string name, out string value)
        {
            name = null;
            value = null;
            var tokens = Tokenize(line);
            if (tokens.Count == 0) return false;
            int start = 0;
            if (tokens[0].Equals("seta", StringComparison.OrdinalIgnoreCase) || tokens[0].Equals("set", StringComparison.OrdinalIgnoreCase))
                start = 1;
            if (tokens.Count - start != 2) return false;
            if (Commands.Contains(tokens[start]) || tokens[start].Equals("configVersion", StringComparison.OrdinalIgnoreCase)) return false;
            if (!IsCvarName(tokens[start])) return false;
            name = tokens[start];
            value = tokens[start + 1];
            return true;
        }

        private static bool IsCvarName(string s) =>
            s.Length > 0 && (char.IsLetter(s[0]) || s[0] == '_') && s.All(c => char.IsLetterOrDigit(c) || c == '_' || c == '.');

        private static string Prefix(string line)
        {
            var trimmed = line.TrimStart();
            var lead = line.Substring(0, line.Length - trimmed.Length);
            foreach (var p in new[] { "seta ", "set " })
                if (trimmed.StartsWith(p, StringComparison.OrdinalIgnoreCase)) return lead + trimmed.Substring(0, p.Length);
            return lead;
        }

        private static string Escape(string value) => (value ?? string.Empty).Replace("\"", "'");

        private static List<string> Tokenize(string line)
        {
            var tokens = new List<string>();
            int i = 0;
            while (i < line.Length)
            {
                if (char.IsWhiteSpace(line[i])) { i++; continue; }
                if (line[i] == '/' && i + 1 < line.Length && line[i + 1] == '/') break;
                if (line[i] == '"')
                {
                    int end = line.IndexOf('"', i + 1);
                    if (end < 0) { tokens.Add(line.Substring(i + 1)); break; }
                    tokens.Add(line.Substring(i + 1, end - i - 1));
                    i = end + 1;
                    continue;
                }
                int s = i;
                while (i < line.Length && !char.IsWhiteSpace(line[i])) i++;
                tokens.Add(line.Substring(s, i - s));
            }
            return tokens;
        }
    }

    /// <summary>One forced key put back by the restore.</summary>
    public sealed class KeyRestore
    {
        public KeyRestore(string key, string before, string after)
        {
            Key = key;
            RestoredValue = before;
            SessionValue = after;
        }

        public string Key { get; }
        /// <summary>The value from the snapshot, or null when the key was absent (and is removed again).</summary>
        public string RestoredValue { get; }
        public string SessionValue { get; }

        public override string ToString() =>
            RestoredValue == null ? $"{Key}: removed (was absent before; session left \"{SessionValue}\")"
                                  : $"{Key}: \"{SessionValue}\" -> \"{RestoredValue}\"";
    }

    public static class ForcedKeyRestore
    {
        /// <summary>
        /// Puts the forced keys of <paramref name="current"/> back to their snapshot values (T-036): a key
        /// the snapshot had is set to its old value, a key it lacked is removed. Every other key, including
        /// changes the player made during the session, is left alone.
        /// </summary>
        public static IReadOnlyList<KeyRestore> Apply(CvarConfig snapshot, CvarConfig current, IEnumerable<string> forcedKeys)
        {
            var before = snapshot.Values;
            var after = current.Values;
            var changes = new List<KeyRestore>();
            foreach (var key in forcedKeys.Distinct(StringComparer.OrdinalIgnoreCase))
            {
                bool had = before.TryGetValue(key, out var oldValue);
                bool has = after.TryGetValue(key, out var newValue);
                if (had && (!has || newValue != oldValue))
                {
                    current.Set(key, oldValue);
                    changes.Add(new KeyRestore(key, oldValue, has ? newValue : null));
                }
                else if (!had && has)
                {
                    current.Remove(key);
                    changes.Add(new KeyRestore(key, null, newValue));
                }
            }
            return changes;
        }
    }
}
