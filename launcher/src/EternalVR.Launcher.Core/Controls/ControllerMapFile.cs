using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace EternalVR.Launcher.Core.Controls
{
    /// <summary>
    /// A controller file (data/input/controllers/*.toml), read the way the layer reads it (features/input/controller_bindings.cpp
    /// and binding_text.cpp): a <c>[profile]</c> section and <c>[map.right]</c>, <c>[map.left_button_swap]</c> and
    /// <c>[map.left_full_mirror]</c>, each a list of <c>"key" = "value"</c> lines. Only that subset of TOML is read: quoted or
    /// bare keys, basic or literal string values without escapes, comments and blank lines.
    /// <para>
    /// Edits change only the lines they touch, so comments, blank lines, the line endings and a byte order mark come back as
    /// they were: an unedited file is written back byte for byte.
    /// </para>
    /// </summary>
    public sealed class ControllerMapFile
    {
        private const string Bom = "﻿";
        private readonly List<string> lines;
        private readonly string newline;
        private readonly bool bom;
        private List<Entry> entries = new List<Entry>();
        private List<Header> headers = new List<Header>();
        private List<ControlIssue> issues = new List<ControlIssue>();

        private sealed class Entry
        {
            public int Line; // 0-based index into lines
            public string Section; // null: before any header
            public string Key;
            public string Value;
            /// <summary>The line up to the value, and the text after it (spacing and a trailing comment).</summary>
            public string Before, After;
            /// <summary>False for a later copy of a key already in its section, which the layer ignores.</summary>
            public bool Used;
        }

        private sealed class Header
        {
            public int Line;
            public string Name;
        }

        private ControllerMapFile(string text)
        {
            text = text ?? string.Empty;
            bom = text.StartsWith(Bom, StringComparison.Ordinal);
            if (bom) text = text.Substring(Bom.Length);
            newline = text.Contains("\r\n") ? "\r\n" : "\n";
            lines = text.Split('\n').Select(l => newline == "\r\n" && l.EndsWith("\r", StringComparison.Ordinal) ? l.Substring(0, l.Length - 1) : l).ToList();
            Reindex();
        }

        public static ControllerMapFile Parse(string text) => new ControllerMapFile(text);

        /// <summary>The file's text: unchanged lines as they were read.</summary>
        public string ToText() => (bom ? Bom : string.Empty) + string.Join(newline, lines);

        public ControllerMapFile Clone() => Parse(ToText());

        /// <summary>The <c>"path"</c> of the <c>[profile]</c> section (the OpenXR interaction profile), or null.</summary>
        public string ProfilePath => Get(ControlNames.ProfileSection, "path");

        /// <summary>The section names, in the order they first appear.</summary>
        public IReadOnlyList<string> Sections => headers.Select(h => h.Name).Distinct().ToList();

        public bool HasSection(string section) => headers.Any(h => h.Name == section);

        /// <summary>The entries of a section as the layer uses them: key to value, the first of a repeated key.</summary>
        public IReadOnlyDictionary<string, string> Entries(string section)
        {
            var map = new Dictionary<string, string>(StringComparer.Ordinal);
            foreach (var e in entries.Where(e => e.Used && e.Section == section)) map[e.Key] = e.Value;
            return map;
        }

        public string Get(string section, string key) =>
            entries.FirstOrDefault(e => e.Used && e.Section == section && e.Key == key)?.Value;

        /// <summary>
        /// Sets <paramref name="key"/> in <paramref name="section"/>: the value of its line is replaced (keeping the rest of
        /// the line), or a line is added among the section's entries in the layer's order, or the section is added at the end
        /// of the file. Later copies of the key, which the layer ignores, are removed.
        /// </summary>
        public void Set(string section, string key, string value)
        {
            if (section == null) throw new ArgumentNullException(nameof(section));
            if (!IsWritable(key) || !IsWritable(value)) throw new ArgumentException("'" + key + "' = '" + value + "' cannot be written in a controller file");
            var existing = entries.Where(e => e.Section == section && e.Key == key).ToList();
            if (existing.Count > 0)
            {
                var first = existing[0];
                lines[first.Line] = first.Before + Quoted(value) + first.After;
                RemoveLines(existing.Skip(1).Select(e => e.Line));
                return;
            }
            lines.Insert(InsertionLine(section, key), Quoted(key) + " = " + Quoted(value));
            Reindex();
        }

        /// <summary>Removes every line of <paramref name="key"/> in <paramref name="section"/> (a button without a line is free).</summary>
        public void Remove(string section, string key) =>
            RemoveLines(entries.Where(e => e.Section == section && e.Key == key).Select(e => e.Line));

        /// <summary>
        /// Replaces the entries of <paramref name="section"/> with those of the same section in <paramref name="source"/>, as
        /// written there. Comments between the entries are kept.
        /// </summary>
        public void ReplaceSection(string section, ControllerMapFile source)
        {
            var mine = entries.Where(e => e.Section == section).Select(e => e.Line).ToList();
            var theirs = source.entries.Where(e => e.Section == section).Select(e => source.lines[e.Line]).ToList();
            if (mine.Count == 0)
            {
                foreach (var e in source.entries.Where(e => e.Section == section && e.Used)) Set(section, e.Key, e.Value);
                return;
            }
            int at = mine[0];
            RemoveLines(mine);
            lines.InsertRange(at, theirs);
            Reindex();
        }

        /// <summary>
        /// The problems the layer would find reading the file (controller_bindings.cpp), before it compiles the maps: lines
        /// that are not <c>"key" = "value"</c>, repeated keys and sections, unknown sections, and a missing <c>[profile]</c> or
        /// profile path. The maps' own rules are <see cref="ControlMapRules"/>.
        /// </summary>
        public IReadOnlyList<ControlIssue> FileIssues => issues;

        /// <summary>True when both files hold the same sections and the same entries (comments and layout aside).</summary>
        public bool SameContent(ControllerMapFile other)
        {
            var a = Sections.OrderBy(s => s, StringComparer.Ordinal).ToList();
            var b = other.Sections.OrderBy(s => s, StringComparer.Ordinal).ToList();
            if (!a.SequenceEqual(b)) return false;
            foreach (var s in a)
            {
                var x = Entries(s);
                var y = other.Entries(s);
                if (x.Count != y.Count || x.Any(kv => !y.TryGetValue(kv.Key, out var v) || v != kv.Value)) return false;
            }
            return true;
        }

        // ---- Reading ----

        private void Reindex()
        {
            entries = new List<Entry>();
            headers = new List<Header>();
            issues = new List<ControlIssue>();
            string current = null;
            bool inSection = false;
            for (int i = 0; i < lines.Count; i++)
            {
                var line = Trim(lines[i]);
                if (line.Length == 0 || line[0] == '#') continue;
                if (line[0] == '[')
                {
                    int hash = line.IndexOf('#');
                    var header = Trim(hash < 0 ? line : line.Substring(0, hash));
                    if (header.Length < 3 || header[header.Length - 1] != ']')
                    {
                        issues.Add(FileIssue(ControlIssueKind.Syntax, null, i, "malformed section header"));
                        inSection = false;
                        continue;
                    }
                    var name = Trim(header.Substring(1, header.Length - 2));
                    if (headers.Any(h => h.Name == name))
                        issues.Add(FileIssue(ControlIssueKind.DuplicateKey, name, i, "[" + name + "] appears more than once"));
                    headers.Add(new Header { Line = i, Name = name });
                    current = name;
                    inSection = true;
                    continue;
                }
                if (!inSection)
                {
                    // Before the first header, or under a malformed one: the layer uses the line for nothing.
                    issues.Add(FileIssue(ControlIssueKind.Syntax, null, i, "entries must be under a [profile], [map.<handedness>] or [labels] header"));
                    continue;
                }
                var entry = ParseEntry(lines[i], out var error);
                if (entry == null)
                {
                    issues.Add(FileIssue(ControlIssueKind.Syntax, null, i, error, current));
                    continue;
                }
                entry.Line = i;
                entry.Section = current;
                entry.Used = !entries.Any(e => e.Used && e.Section == current && e.Key == entry.Key);
                if (!entry.Used)
                    issues.Add(FileIssue(ControlIssueKind.DuplicateKey, entry.Key, i, "'" + entry.Key + "' is set more than once in [" + current + "]; the first value is used", current));
                entries.Add(entry);
            }

            bool sawProfile = false;
            foreach (var name in Sections)
            {
                if (name == ControlNames.ProfileSection)
                {
                    sawProfile = true;
                    CheckProfile();
                    continue;
                }
                if (name == ControlNames.LabelsSection)
                {
                    CheckLabels();
                    continue;
                }
                if (!ControlNames.MapSections.Contains(name))
                    issues.Add(new ControlIssue
                    {
                        Kind = ControlIssueKind.UnknownKey, Key = name, Line = headers.First(h => h.Name == name).Line + 1,
                        Message = "[" + name + "] is not a section of a controller file; use [profile], [labels] or [map.right], [map.left_button_swap], [map.left_full_mirror]",
                    });
            }
            if (!sawProfile)
                issues.Add(new ControlIssue { Kind = ControlIssueKind.Syntax, Message = "the file has no [profile] section" });
        }

        private static readonly string[] GameplayActions = { "trigger", "grip", "thumbstick", "thumbstick_click", "primary", "secondary", "face3", "face4", "shoulder", "menu", "aim_pose", "grip_pose", "haptic", "thumbrest", "primary_touch", "secondary_touch" };

        /// <summary>
        /// The shape of the <c>[profile]</c> entries. The layer also checks each path against the controller's inputs; the
        /// launcher has no list of those, and the editor never changes this section.
        /// </summary>
        private void CheckProfile()
        {
            const string section = ControlNames.ProfileSection;
            var profile = entries.Where(e => e.Used && e.Section == section).ToList();
            if (!profile.Any(e => e.Key == "path"))
                issues.Add(new ControlIssue { Kind = ControlIssueKind.Syntax, Section = section, Key = "path", Message = "[profile] has no \"path\" naming its interaction profile" });
            foreach (var e in profile.Where(e => e.Key != "path"))
            {
                // Earlier versions also bound a "menu" action set that was never synced; files copied from them keep its keys,
                // which the layer skips.
                if (e.Key.StartsWith("menu.", StringComparison.Ordinal)) continue;
                var parts = e.Key.Split('.');
                bool known = parts.Length == 3 && ControlNames.TryParseHand(parts[1], out _) && parts[0] == "gameplay" && GameplayActions.Contains(parts[2]);
                if (!known)
                    issues.Add(new ControlIssue
                    {
                        Kind = ControlIssueKind.UnknownKey, Section = section, Key = e.Key, Value = e.Value, Line = e.Line + 1,
                        Message = "line " + (e.Line + 1) + ": '" + e.Key + "' is not a suggested-binding key; use <set>.<hand>.<action>",
                    });
                else if (!e.Value.StartsWith("/input/", StringComparison.Ordinal) && e.Value != "/output/haptic")
                    issues.Add(new ControlIssue
                    {
                        Kind = ControlIssueKind.UnknownValue, Section = section, Key = e.Key, Value = e.Value, Line = e.Line + 1,
                        Message = "line " + (e.Line + 1) + ": '" + e.Value + "' is not an input path; paths start with /input/ or are /output/haptic",
                    });
            }
        }

        /// <summary>The <c>[labels]</c> entries: <c>"&lt;hand&gt;.&lt;input&gt;" = "&lt;name&gt;"</c>, as the layer reads them.</summary>
        private void CheckLabels()
        {
            const string section = ControlNames.LabelsSection;
            foreach (var e in entries.Where(e => e.Used && e.Section == section))
            {
                var parts = e.Key.Split('.');
                bool known = parts.Length == 2 && ControlNames.TryParseHand(parts[0], out _) && ControlNames.LabelInputNames.Contains(parts[1]);
                if (!known)
                    issues.Add(new ControlIssue
                    {
                        Kind = ControlIssueKind.UnknownKey, Section = section, Key = e.Key, Value = e.Value, Line = e.Line + 1,
                        Message = "line " + (e.Line + 1) + ": '" + e.Key + "' is not a label key; use <hand>.<input>, e.g. 'left.primary'",
                    });
                else if (e.Value.Trim().Length == 0)
                    issues.Add(new ControlIssue
                    {
                        Kind = ControlIssueKind.UnknownValue, Section = section, Key = e.Key, Value = e.Value, Line = e.Line + 1,
                        Message = "line " + (e.Line + 1) + ": '" + e.Key + "' has an empty name",
                    });
            }
        }

        private static ControlIssue FileIssue(ControlIssueKind kind, string key, int index, string message, string section = null) =>
            new ControlIssue { Kind = kind, Key = key, Section = section, Line = index + 1, Message = "line " + (index + 1) + ": " + message };

        private static string Trim(string s) => s.Trim(' ', '\t', '\r');

        private static bool IsQuote(char c) => c == '"' || c == '\'';

        private static bool IsBareKeyChar(char c) =>
            c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z' || c >= '0' && c <= '9' || c == '_' || c == '-' || c == '.';

        /// <summary>A <c>key = "value"</c> line (binding_text.cpp's parseLine), or null with the reason.</summary>
        private static Entry ParseEntry(string raw, out string error)
        {
            error = null;
            int i = 0;
            while (i < raw.Length && (raw[i] == ' ' || raw[i] == '\t')) i++;
            string key;
            if (i < raw.Length && IsQuote(raw[i]))
            {
                if (!TakeQuoted(raw, ref i, out key, out error)) return null;
            }
            else
            {
                int start = i;
                while (i < raw.Length && IsBareKeyChar(raw[i])) i++;
                if (i == start)
                {
                    error = "expected a key";
                    return null;
                }
                key = raw.Substring(start, i - start);
            }
            while (i < raw.Length && (raw[i] == ' ' || raw[i] == '\t')) i++;
            if (i >= raw.Length || raw[i] != '=')
            {
                error = "expected '=' after the key";
                return null;
            }
            i++;
            while (i < raw.Length && (raw[i] == ' ' || raw[i] == '\t')) i++;
            int valueStart = i;
            if (!TakeQuoted(raw, ref i, out var value, out error)) return null;
            var rest = Trim(raw.Substring(i));
            if (rest.Length > 0 && rest[0] != '#')
            {
                error = "unexpected text after the value";
                return null;
            }
            return new Entry { Key = key, Value = value, Before = raw.Substring(0, valueStart), After = raw.Substring(i) };
        }

        private static bool TakeQuoted(string raw, ref int i, out string text, out string error)
        {
            text = null;
            error = null;
            if (i >= raw.Length || !IsQuote(raw[i]))
            {
                error = "expected a quoted string";
                return false;
            }
            char quote = raw[i];
            int close = raw.IndexOf(quote, i + 1);
            if (close < 0)
            {
                error = "unterminated string";
                return false;
            }
            text = raw.Substring(i + 1, close - i - 1);
            if (quote == '"' && text.IndexOf('\\') >= 0)
            {
                error = "escape sequences are not supported";
                text = null;
                return false;
            }
            i = close + 1;
            return true;
        }

        // ---- Writing ----

        /// <summary>A key or value the layer reads back unchanged: one line, and not holding both kinds of quote.</summary>
        private static bool IsWritable(string text) =>
            text != null && text.IndexOfAny(new[] { '\r', '\n' }) < 0 && !(text.IndexOf('\'') >= 0 && text.IndexOfAny(new[] { '"', '\\' }) >= 0);

        /// <summary>A basic string, or a literal one for text a basic string would have to escape (binding_text.cpp).</summary>
        private static string Quoted(string text)
        {
            char quote = text.IndexOfAny(new[] { '"', '\\' }) >= 0 ? '\'' : '"';
            return quote + text + quote;
        }

        private void RemoveLines(IEnumerable<int> indexes)
        {
            foreach (var i in indexes.Distinct().OrderByDescending(i => i)) lines.RemoveAt(i);
            Reindex();
        }

        /// <summary>Where a new line for <paramref name="key"/> goes, adding the section at the end of the file if it is missing.</summary>
        private int InsertionLine(string section, string key)
        {
            var header = headers.FirstOrDefault(h => h.Name == section);
            if (header == null)
            {
                // A blank line before the new section, unless the file ends in one already.
                int end = lines.Count;
                bool endsWithNewline = end > 0 && lines[end - 1].Length == 0;
                if (endsWithNewline) end--;
                var added = new List<string>();
                if (end > 0 && Trim(lines[end - 1]).Length > 0) added.Add(string.Empty);
                added.Add("[" + section + "]");
                lines.InsertRange(end, added);
                if (!endsWithNewline) lines.Add(string.Empty);
                Reindex();
                return end + added.Count;
            }
            var mine = entries.Where(e => e.Section == section).ToList();
            if (mine.Count == 0) return header.Line + 1;
            int rank = RankOf(key);
            // After the last entry that comes before it in the layer's order, else before the first entry.
            var before = mine.Where(e => RankOf(e.Key) <= rank).LastOrDefault();
            return before != null ? before.Line + 1 : mine[0].Line;
        }

        private static int RankOf(string key) => BindingKey.TryParse(key, out var k) ? k.Rank : int.MaxValue;
    }
}
