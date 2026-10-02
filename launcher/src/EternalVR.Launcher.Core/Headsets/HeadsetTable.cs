using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using EternalVR.Launcher.Core.Data;
using EternalVR.Launcher.Core.Launch;

namespace EternalVR.Launcher.Core.Headsets
{
    /// <summary>How a headset is recognised (<c>data\headsets.txt</c>).</summary>
    public enum HeadsetIdKind
    {
        /// <summary><c>xr=</c>: the OpenXR system name, exactly (a vendor's runtime names the model there).</summary>
        SystemName,
        /// <summary><c>vr=</c>: SteamVR's headset model (LastKnown <c>HMDModel</c>), exactly.</summary>
        SteamVrModel,
        /// <summary><c>vr~</c>: a part of SteamVR's headset model, in any case.</summary>
        SteamVrModelPart,
        /// <summary><c>tk=</c>: SteamVR's tracking system in its system name (<c>SteamVR/OpenXR : playstation_vr2</c>), in any
        /// case; only for a tracking system that one headset alone uses.</summary>
        TrackingSystem,
    }

    public sealed class HeadsetId
    {
        public HeadsetId(HeadsetIdKind kind, string value)
        {
            Kind = kind;
            Value = value;
        }

        public HeadsetIdKind Kind { get; }
        public string Value { get; }

        public bool Matches(HeadsetIdKind kind, string name)
        {
            if (string.IsNullOrWhiteSpace(name)) return false;
            name = name.Trim();
            switch (Kind)
            {
                case HeadsetIdKind.SystemName: return kind == HeadsetIdKind.SystemName && string.Equals(name, Value, StringComparison.Ordinal);
                case HeadsetIdKind.TrackingSystem: return kind == HeadsetIdKind.TrackingSystem && string.Equals(name, Value, StringComparison.OrdinalIgnoreCase);
                case HeadsetIdKind.SteamVrModel: return IsSteamVrModel(kind) && string.Equals(name, Value, StringComparison.Ordinal);
                default: return IsSteamVrModel(kind) && name.IndexOf(Value, StringComparison.OrdinalIgnoreCase) >= 0;
            }
        }

        private static bool IsSteamVrModel(HeadsetIdKind kind) => kind == HeadsetIdKind.SteamVrModel || kind == HeadsetIdKind.SteamVrModelPart;
    }

    /// <summary>One headset of the table: its name, how it is recognised, its native panel per eye and its refresh rates.</summary>
    public sealed class HeadsetModel
    {
        public HeadsetModel(string name, IEnumerable<HeadsetId> ids, Extent panel, IEnumerable<int> refreshRates)
        {
            Name = name;
            Ids = ids.ToList();
            Panel = panel;
            RefreshRates = refreshRates.ToList();
        }

        /// <summary>The name the window shows, such as "Valve Index".</summary>
        public string Name { get; }
        public IReadOnlyList<HeadsetId> Ids { get; }
        /// <summary>The native panel per eye, as the maker gives it.</summary>
        public Extent Panel { get; }
        /// <summary>The refresh rates the maker lists, in Hz.</summary>
        public IReadOnlyList<int> RefreshRates { get; }
    }

    /// <summary>
    /// The headsets the launcher knows by name (<c>data\headsets.txt</c>): <c>name | identifiers | panel WxH | rates</c>, the
    /// identifiers <c>xr=</c>, <c>vr=</c>, <c>vr~</c> or <c>tk=</c> separated by ';'. OpenXR gives no runtime's native panel size, so the
    /// Play tab's Native panel line and Resolution's native panel come from here. The first headset in file order with a
    /// matching identifier wins; one that matches none is "not in our list", never a guess.
    /// </summary>
    public sealed class HeadsetTable
    {
        public static readonly HeadsetTable Empty = new HeadsetTable(new HeadsetModel[0]);

        public HeadsetTable(IEnumerable<HeadsetModel> models) { Models = models.ToList(); }

        public IReadOnlyList<HeadsetModel> Models { get; }

        /// <summary>The headset whose OpenXR system name (<c>xr=</c>) is <paramref name="systemName"/>; null when none.</summary>
        public HeadsetModel BySystemName(string systemName) => Find(HeadsetIdKind.SystemName, systemName);

        /// <summary>The headset of SteamVR's model <paramref name="model"/> (<c>vr=</c> exactly, <c>vr~</c> a part); null when none.</summary>
        public HeadsetModel BySteamVrModel(string model) => Find(HeadsetIdKind.SteamVrModel, model);

        /// <summary>The headset that alone uses SteamVR's tracking system <paramref name="trackingSystem"/> (<c>tk=</c>); null when none.</summary>
        public HeadsetModel ByTrackingSystem(string trackingSystem) => Find(HeadsetIdKind.TrackingSystem, trackingSystem);

        private HeadsetModel Find(HeadsetIdKind kind, string name)
        {
            if (string.IsNullOrWhiteSpace(name)) return null;
            return Models.FirstOrDefault(m => m.Ids.Any(id => id.Matches(kind, name)));
        }

        /// <summary>Parses the data file; a line that does not follow the format throws <see cref="FormatException"/>.</summary>
        public static HeadsetTable Parse(string text)
        {
            var models = new List<HeadsetModel>();
            foreach (var r in DataFile.ParseRecords(text))
            {
                var line = string.Join("|", r);
                if (r.Length != 4) throw new FormatException("headsets: name | identifiers | panel WxH | rates: " + line);
                var name = r[0];
                if (name.Length == 0) throw new FormatException("headsets: no name: " + line);
                if (models.Any(m => string.Equals(m.Name, name, StringComparison.OrdinalIgnoreCase)))
                    throw new FormatException("headsets: listed twice: " + name);
                var ids = new List<HeadsetId>();
                foreach (var part in r[1].Split(';').Select(p => p.Trim()).Where(p => p.Length > 0))
                {
                    var id = ParseId(part);
                    if (id == null) throw new FormatException("headsets: an identifier is xr=, vr=, vr~ or tk= and a name: " + part + " (" + name + ")");
                    ids.Add(id);
                }
                if (ids.Count == 0) throw new FormatException("headsets: no identifier for " + name);
                var panel = ParsePanel(r[2]);
                if (panel == null) throw new FormatException("headsets: the panel of " + name + " is WxH: " + r[2]);
                var rates = new List<int>();
                foreach (var part in r[3].Split(',').Select(p => p.Trim()))
                {
                    if (!int.TryParse(part, NumberStyles.None, CultureInfo.InvariantCulture, out var hz) || hz < 30 || hz > 500)
                        throw new FormatException("headsets: the rates of " + name + " are Hz separated by ',': " + r[3]);
                    rates.Add(hz);
                }
                models.Add(new HeadsetModel(name, ids, panel.Value, rates));
            }
            return new HeadsetTable(models);
        }

        private static HeadsetId ParseId(string part)
        {
            if (part.Length < 4) return null;
            var prefix = part.Substring(0, 3);
            var value = part.Substring(3).Trim();
            if (value.Length == 0) return null;
            if (prefix == "xr=") return new HeadsetId(HeadsetIdKind.SystemName, value);
            if (prefix == "vr=") return new HeadsetId(HeadsetIdKind.SteamVrModel, value);
            if (prefix == "vr~") return new HeadsetId(HeadsetIdKind.SteamVrModelPart, value);
            if (prefix == "tk=") return new HeadsetId(HeadsetIdKind.TrackingSystem, value);
            return null;
        }

        private static Extent? ParsePanel(string text)
        {
            var parts = text.ToLowerInvariant().Split('x');
            if (parts.Length != 2
                || !uint.TryParse(parts[0].Trim(), NumberStyles.None, CultureInfo.InvariantCulture, out var w)
                || !uint.TryParse(parts[1].Trim(), NumberStyles.None, CultureInfo.InvariantCulture, out var h)
                || w < 256 || h < 256 || w > 16384 || h > 16384)
                return null;
            return new Extent(w, h);
        }
    }
}
