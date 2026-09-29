using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using EternalVR.Launcher.Core.Data;
using EternalVR.Launcher.Core.Settings;

namespace EternalVR.Launcher.Core.Launch
{
    /// <summary>
    /// One item of the processor saver: a checkbox on the Play tab (Picture) with its game cvars. Its choice is kept in
    /// launcher.ini as <c>cpu_saver_&lt;id&gt; = on|off</c> (<see cref="LauncherSettings.CpuSaverChoice"/>).
    /// </summary>
    public sealed class CpuSaverItem
    {
        public CpuSaverItem(string id, bool defaultOn, Setting row, string label, string note, string tooltip, IEnumerable<ForcedCvar> cvars)
        {
            Id = id;
            DefaultOn = defaultOn;
            Row = row;
            Label = label;
            Note = note ?? string.Empty;
            Tooltip = tooltip;
            Cvars = cvars.ToList();
        }

        /// <summary>A stable name: lower-case letters, digits and '_'.</summary>
        public string Id { get; }
        /// <summary>On for a player whose launcher.ini has no choice for it (and no older <c>cpu_saver = on</c>).</summary>
        public bool DefaultOn { get; }
        /// <summary>The window row the checkbox is in (<see cref="CpuSaver.Rows"/>).</summary>
        public Setting Row { get; }
        /// <summary>The checkbox's text.</summary>
        public string Label { get; }
        /// <summary>A short line under a checkbox of the Texture streaming row; empty for none.</summary>
        public string Note { get; }
        /// <summary>What it changes, the measured gain and what it costs in the picture.</summary>
        public string Tooltip { get; }
        public IReadOnlyList<ForcedCvar> Cvars { get; }

        /// <summary>The item's key in launcher.ini.</summary>
        public string Key => LauncherSettings.CpuSaverKeyPrefix + Id;

        /// <summary>The cvars in words, for the tooltip: <c>r_a 1, r_b at most 0.5</c>.</summary>
        public string CvarText => string.Join(", ", Cvars.Select(c => c.Name + " "
            + (c.Value.StartsWith(CpuSaver.CapPrefix, StringComparison.Ordinal) ? "at most " + c.Value.Substring(CpuSaver.CapPrefix.Length) : c.Value)));
    }

    /// <summary>
    /// The processor saver (Play tab, Picture): game cvars that cut the processor's work per render, in items the player
    /// turns on one by one, kept as data in <c>data\cpu-saver.txt</c> (docs/rig-findings/perf-cpu-cvars.md). A stereo
    /// launch hands the cvars of the items that are on to the layer (<c>ETERNALVR_CPU_SAVER</c>), which holds them at
    /// run time. The game may save them into its config, so the settings restore puts the keys of every item back after
    /// every session, whether the item was on or not (<see cref="LauncherData.RestoredKeys"/>).
    /// </summary>
    public sealed class CpuSaver
    {
        /// <summary>The layer's variable.</summary>
        public const string EnvironmentName = "ETERNALVR_CPU_SAVER";

        /// <summary>A cap: <c>&lt;=N</c> lowers the cvar to N only while it is above N (a value the game's menu sets per quality level).</summary>
        public const string CapPrefix = "<=";

        /// <summary>The data file's row names: the window rows an item's checkbox can go in, in window order.</summary>
        public static readonly IReadOnlyList<KeyValuePair<string, Setting>> Rows = new[]
        {
            new KeyValuePair<string, Setting>("streaming", Setting.TextureStreaming),
            new KeyValuePair<string, Setting>("saver", Setting.ProcessorSaver),
        };

        public static readonly CpuSaver Empty = new CpuSaver(new CpuSaverItem[0]);

        public CpuSaver(IEnumerable<CpuSaverItem> items) { Items = items.ToList(); }

        public IReadOnlyList<CpuSaverItem> Items { get; }

        /// <summary>Every item's cvars, in file order.</summary>
        public IReadOnlyList<ForcedCvar> All => Items.SelectMany(i => i.Cvars).ToList();

        /// <summary>Every item's cvar names: the keys the settings restore puts back.</summary>
        public IEnumerable<string> Names => All.Select(c => c.Name);

        public IEnumerable<CpuSaverItem> InRow(Setting row) => Items.Where(i => i.Row == row);

        /// <summary>Whether <paramref name="item"/> is on: the player's choice, else on for every item after an older
        /// launcher's <c>cpu_saver = on</c>, else the item's default.</summary>
        public static bool IsOn(CpuSaverItem item, LauncherSettings s) =>
            s.CpuSaverChoice(item.Id) ?? (s.CpuSaverAllOn || item.DefaultOn);

        public IEnumerable<CpuSaverItem> Selected(LauncherSettings s) => Items.Where(i => IsOn(i, s));

        /// <summary>The layer's value for the items that are on: <c>name=value;name=value</c>, in file order; empty when none is.</summary>
        public string EnvironmentValue(LauncherSettings s) => Join(Selected(s).SelectMany(i => i.Cvars));

        /// <summary>The layer's value with every item on.</summary>
        public string EnvironmentValueAll => Join(All);

        private static string Join(IEnumerable<ForcedCvar> cvars) => string.Join(";", cvars.Select(c => c.Name + "=" + c.Value));

        /// <summary>
        /// A value the layer takes: a plain value without the list's separators (';' between items, '=' after the name;
        /// "?" would only log), or a cap, <c>&lt;=</c> and a number.
        /// </summary>
        public static bool IsValue(string value)
        {
            if (string.IsNullOrEmpty(value)) return false;
            if (value.StartsWith(CapPrefix, StringComparison.Ordinal))
            {
                var number = value.Substring(CapPrefix.Length);
                return number.Length > 0 && !char.IsWhiteSpace(number[0])
                    && double.TryParse(number, NumberStyles.Float, CultureInfo.InvariantCulture, out var v)
                    && !double.IsNaN(v) && !double.IsInfinity(v);
            }
            return value != "?" && value.IndexOfAny(new[] { ';', '=', '"', ' ', '<' }) < 0;
        }

        /// <summary>An item id: 1 to 40 of a-z, 0-9 and '_', starting with a letter (it becomes part of a launcher.ini key).</summary>
        public static bool IsId(string id) =>
            !string.IsNullOrEmpty(id) && id.Length <= 40 && id[0] >= 'a' && id[0] <= 'z'
            && id.All(ch => (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_');

        /// <summary>
        /// Parses the data file: <c>item | id | on|off | row | label [| note]</c> starts an item, <c>tip | text</c> adds to its
        /// tooltip, and <c>name | value</c> lines are its cvars.
        /// </summary>
        public static CpuSaver Parse(string text)
        {
            var items = new List<CpuSaverItem>();
            string id = null, label = null, note = null, tip = null;
            bool defaultOn = false;
            Setting row = Setting.ProcessorSaver;
            var cvars = new List<ForcedCvar>();
            var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

            void Finish()
            {
                if (id == null) return;
                if (cvars.Count == 0) throw new FormatException("cpu-saver: item " + id + " has no cvars");
                if (string.IsNullOrWhiteSpace(tip)) throw new FormatException("cpu-saver: item " + id + " has no tip");
                items.Add(new CpuSaverItem(id, defaultOn, row, label, note, tip, cvars));
                cvars = new List<ForcedCvar>();
            }

            foreach (var r in DataFile.ParseRecords(text))
            {
                var first = DataFile.Field(r, 0);
                if (first == "item")
                {
                    Finish();
                    if (r.Length != 5 && r.Length != 6)
                        throw new FormatException("cpu-saver: item | id | on or off | row | label [| note]: " + string.Join("|", r));
                    id = r[1];
                    if (!IsId(id)) throw new FormatException("cpu-saver: bad item id: " + id);
                    if (items.Any(i => i.Id == id)) throw new FormatException("cpu-saver: item listed twice: " + id);
                    if (r[2] != "on" && r[2] != "off") throw new FormatException("cpu-saver: the default of " + id + " is on or off, not " + r[2]);
                    defaultOn = r[2] == "on";
                    var place = Rows.Where(x => x.Key == r[3]).ToList();
                    if (place.Count == 0) throw new FormatException("cpu-saver: unknown row for " + id + ": " + r[3]);
                    row = place[0].Value;
                    label = r[4];
                    if (label.Length == 0) throw new FormatException("cpu-saver: no label for " + id);
                    note = DataFile.Field(r, 5);
                    if (r.Length == 6 && note.Length == 0) throw new FormatException("cpu-saver: an empty note for " + id);
                    tip = null;
                    continue;
                }
                if (first == "tip")
                {
                    if (id == null) throw new FormatException("cpu-saver: a tip before the first item");
                    if (r.Length != 2 || r[1].Length == 0) throw new FormatException("cpu-saver: tip | text (no '|' in the text): " + string.Join("|", r));
                    tip = tip == null ? r[1] : tip + " " + r[1];
                    continue;
                }
                var name = first;
                var value = DataFile.Field(r, 1);
                if (id == null) throw new FormatException("cpu-saver: a cvar before the first item: " + name);
                if (r.Length != 2) throw new FormatException("cpu-saver: one name and one value per line: " + string.Join("|", r));
                if (name.Length == 0 || name.IndexOfAny(new[] { '+', ' ', '=', ';', '"' }) >= 0)
                    throw new FormatException("cpu-saver: bad cvar name: " + name);
                if (value.Length == 0) throw new FormatException("cpu-saver: no value for " + name);
                if (!IsValue(value)) throw new FormatException("cpu-saver: bad value for " + name + ": " + value);
                if (!names.Add(name)) throw new FormatException("cpu-saver: listed twice: " + name);
                cvars.Add(new ForcedCvar(name, value, true));
            }
            Finish();
            return new CpuSaver(items);
        }
    }
}
