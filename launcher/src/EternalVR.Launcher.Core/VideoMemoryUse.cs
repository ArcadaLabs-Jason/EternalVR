using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text.RegularExpressions;

namespace EternalVR.Launcher.Core
{
    /// <summary>
    /// How often a session's game used more video memory than the budget Windows gives it, or nearly all of it, from the
    /// layer's 10 s lines (<c>vram: the process uses U MB of local video memory, budget B MB (P%); last 10 s: peak K MB, N of M
    /// reading(s) over the budget</c>, src/vkcore/vram_watch.cpp), and what to change when it was over: the game's settings
    /// that cost the most of it, as the layer's last <c>game settings:</c> line has them (src/vkcore/game_settings_line.cpp).
    /// <see cref="SessionSummary"/> reads its lines once for both.
    /// </summary>
    public sealed class VideoMemoryUse
    {
        /// <summary>From this share of the session's vram windows over the budget, the summary says so (T-227: in 198 players'
        /// sessions a session was over in none of its windows or in 12% or more).</summary>
        public const double OverShareToSay = 0.10;
        /// <summary>A vram window using this share of the budget or more is near it.</summary>
        public const double NearBudget = 0.95;
        /// <summary>From this share of the session's vram windows near the budget (and the over-budget sentence not said), the
        /// summary says so: a runtime menu opening over the game can then reset the graphics driver (issue #19: SteamVR's
        /// dashboard at 97 to 99% of the budget, fixed by a lower texture pool).</summary>
        public const double NearShareToSay = 0.50;
        /// <summary>The texture pool of the game's lowest Texture Pool Size (Low; docs/rig-findings/perf-cpu-cvars.md section 2).</summary>
        public const int LowestTexturePoolMb = 1024;
        /// <summary>The shadow atlas height of Shadow Quality Ultra and above; High has half of it (perf-cpu-cvars.md section 2).</summary>
        public const int UltraShadowAtlasHeight = 8192;

        private const string Tag = "] vram: ";
        private const string SettingsTag = "] game settings: ";
        /// <summary>The layer's count of the cvars it found (src/vkcore/game_settings.cpp), once per game start: each settings line
        /// has exactly that many entries, so a line the game was killed in the middle of has fewer, or a cut last entry.</summary>
        private static readonly Regex LocatedLine = new Regex(@"\] game settings cvars: (?<n>[0-9]+) of [0-9]+ located", RegexOptions.CultureInvariant);
        /// <summary>One entry as game_settings_line.cpp writes it: the name, the value (or <c>?</c> when unreadable), an optional note.</summary>
        private static readonly Regex Entry = new Regex(@"^[A-Za-z_][A-Za-z0-9_]* (?:-?[0-9][0-9.e+-]*|\?)(?: \([^()]*\))?$", RegexOptions.CultureInvariant);
        private static readonly Regex VramLine = new Regex(
            @"\] vram: the process uses (?<use>[0-9]+) MB of local video memory, budget (?<budget>[0-9]+) MB.*; last 10 s: peak (?<peak>[0-9]+)(?:\.[0-9]+)? MB, (?<over>[0-9]+) of (?<all>[0-9]+) reading\(s\) over the budget",
            RegexOptions.CultureInvariant);
        private static readonly Regex TexturePool = Setting("is_poolSize");
        private static readonly Regex ShadowAtlas = Setting("r_shadowAtlasHeight");
        private static readonly Regex RayTracingOn = Setting("r_useRayTracing");

        private int? located;
        private int overWindows;
        private int nearWindows;
        private readonly List<long> overBy = new List<long>();
        private readonly List<long> overBudgets = new List<long>();

        /// <summary>The layer's 10 s vram windows with a reading; 0 when it logged none (no budget, or an older layer).</summary>
        public int Windows { get; private set; }
        /// <summary>The share (0 to 1) of <see cref="Windows"/> with most of their readings over the budget.</summary>
        public double OverShare => Windows > 0 ? (double)overWindows / Windows : 0.0;
        /// <summary>The share (0 to 1) of <see cref="Windows"/> whose last reading used <see cref="NearBudget"/> of the budget or
        /// more (over-budget windows included).</summary>
        public double NearShare => Windows > 0 ? (double)nearWindows / Windows : 0.0;
        /// <summary>True when the session was over the budget for long enough to say so: the status line then asks for a change.</summary>
        public bool Over => Windows >= SessionRates.MinWindows && OverShare >= OverShareToSay;
        /// <summary>How far the over-budget windows' peaks went past their budget, the median in MB; null without one past it.</summary>
        public long? OverByMb => overBy.Count > 0 ? Median(overBy) : (long?)null;
        /// <summary>The over-budget windows' budget, the median in MB; null without one.</summary>
        public long? OverBudgetMb => overBudgets.Count > 0 ? Median(overBudgets) : (long?)null;
        /// <summary>The game's texture pool in MB (<c>is_poolSize</c>, its Texture Pool Size); null when not logged.</summary>
        public int? TexturePoolMb { get; private set; }
        /// <summary>The game's shadow atlas height (<c>r_shadowAtlasHeight</c>, set by its Shadow Quality); null when not logged.</summary>
        public int? ShadowAtlasHeight { get; private set; }
        /// <summary>The game's Raytracing setting (<c>r_useRayTracing</c>); null when not logged (an older layer).</summary>
        public bool? RayTracing { get; private set; }

        /// <summary>Takes one layer log line; true when it was a vram or game settings line (no other reader needs it then).</summary>
        internal bool Read(string line)
        {
            if (line == null) return false;
            int settings = line.IndexOf(SettingsTag, StringComparison.Ordinal);
            if (settings >= 0)
            {
                // The last complete line counts: the settings the player would change from. A value it lacks (or could not
                // read) keeps the one an earlier line gave.
                var text = line.Substring(settings + SettingsTag.Length);
                if (!Complete(text)) return true;
                TexturePoolMb = Value(TexturePool, text) ?? TexturePoolMb;
                ShadowAtlasHeight = Value(ShadowAtlas, text) ?? ShadowAtlasHeight;
                var rt = Value(RayTracingOn, text);
                if (rt.HasValue) RayTracing = rt.Value != 0;
                return true;
            }
            var count = LocatedLine.Match(line);
            if (count.Success)
            {
                located = int.TryParse(count.Groups["n"].Value, NumberStyles.None, CultureInfo.InvariantCulture, out var n) ? n : (int?)null;
                return true;
            }
            if (line.IndexOf(Tag, StringComparison.Ordinal) < 0) return false;
            var v = VramLine.Match(line);
            if (!v.Success) return true;
            if (int.TryParse(v.Groups["over"].Value, NumberStyles.None, CultureInfo.InvariantCulture, out var over)
                && int.TryParse(v.Groups["all"].Value, NumberStyles.None, CultureInfo.InvariantCulture, out var readings) && readings > 0
                && long.TryParse(v.Groups["use"].Value, NumberStyles.None, CultureInfo.InvariantCulture, out var use)
                && long.TryParse(v.Groups["budget"].Value, NumberStyles.None, CultureInfo.InvariantCulture, out var budget)
                && long.TryParse(v.Groups["peak"].Value, NumberStyles.None, CultureInfo.InvariantCulture, out var peak))
            {
                Windows++;
                if (over * 2 > readings)
                {
                    overWindows++;
                    if (budget > 0) overBudgets.Add(budget);
                    if (budget > 0 && peak > budget) overBy.Add(peak - budget);
                }
                if (budget > 0 && use >= NearBudget * budget) nearWindows++;
            }
            return true;
        }

        /// <summary>The sentence for the status line after the session (and the report); empty when there is nothing to say.</summary>
        public string Sentence()
        {
            if (Over) return OverSentence();
            if (Windows >= SessionRates.MinWindows && NearShare >= NearShareToSay)
                return string.Format(CultureInfo.InvariantCulture,
                    "The game used nearly all the video memory Windows gives it for {0}% of the session, so a headset menu opening "
                    + "over it (like SteamVR's dashboard) can reset the graphics driver: a lower Texture Pool Size in the game's "
                    + "video settings makes room.",
                    (int)Math.Round(NearShare * 100, MidpointRounding.AwayFromZero));
            return string.Empty;
        }

        /// <summary>
        /// How far past the budget it went and the game's settings to change, only those the log says can still go down:
        /// Texture Pool Size (unless at its lowest), Shadow Quality to High (from Ultra and above, whose shadow atlas is twice
        /// as tall as High's), Raytracing off (when on); Resolution on the Play tab after them, or alone. With ray tracing on,
        /// turning it off comes first and the texture pool's step is counted from what the menu shows after it: the menu picks
        /// the pool by ray tracing too, and turning it off raised Ultra Nightmare's from 4096 to 4608 (T-227). One step at a
        /// time, since the launcher measures again after the next session.
        /// </summary>
        private string OverSentence()
        {
            int percent = Math.Max(1, (int)Math.Round(OverShare * 100, MidpointRounding.AwayFromZero));
            var over = OverByMb.HasValue && OverBudgetMb.HasValue
                ? string.Format(CultureInfo.InvariantCulture, "The game went {0} over the {1} of video memory Windows gives it for {2}% of the session",
                    Amount(OverByMb.Value), Gigabytes(OverBudgetMb.Value), percent)
                : string.Format(CultureInfo.InvariantCulture, "The game went over the video memory Windows gives it for {0}% of the session", percent);
            bool pool = !TexturePoolMb.HasValue || TexturePoolMb.Value > LowestTexturePoolMb;
            bool shadows = ShadowAtlasHeight >= UltraShadowAtlasHeight;
            string change;
            if (RayTracing == true)
                change = "turn Raytracing off"
                    + (pool ? ", then set Texture Pool Size one step lower than it shows after that" : string.Empty)
                    + (shadows ? (pool ? ", and Shadow Quality to High" : " and set Shadow Quality to High") : string.Empty);
            else if (pool || shadows)
                change = "set " + (pool && shadows ? "Texture Pool Size one step lower and Shadow Quality to High"
                    : pool ? "Texture Pool Size one step lower" : "Shadow Quality to High");
            else
                return over + ", which can make it stutter or crash: lower Resolution on the Play tab.";
            return over + ", which can make it stutter or crash: in the game's video settings " + change
                + "; if this shows again after your next session, lower Resolution on the Play tab.";
        }

        /// <summary>The counts for the launcher log's session line, with the settings read.</summary>
        public string LogText() => string.Format(CultureInfo.InvariantCulture,
            "video memory over the budget in {0} of {1} window(s), at 95% of it or more in {2}, over it by {3} MB (median) of a {4} MB budget; texture pool {5}, shadow atlas {6}, ray tracing {7}",
            overWindows, Windows, nearWindows, OverByMb?.ToString(CultureInfo.InvariantCulture) ?? "-", OverBudgetMb?.ToString(CultureInfo.InvariantCulture) ?? "-",
            TexturePoolMb?.ToString(CultureInfo.InvariantCulture) ?? "not logged", ShadowAtlasHeight?.ToString(CultureInfo.InvariantCulture) ?? "not logged",
            RayTracing.HasValue ? (RayTracing.Value ? "on" : "off") : "not logged");

        /// <summary>A cvar's integer value in the settings line (<c>is_poolSize 3072, ...</c>); a value the layer could not read is <c>?</c>.</summary>
        private static Regex Setting(string cvar) =>
            new Regex(@"(?:^|, )" + cvar + @" (?<v>[0-9]+)(?![0-9.])", RegexOptions.CultureInvariant);

        /// <summary>True when the line has as many entries as the layer found cvars, each whole; false without that count.</summary>
        private bool Complete(string text)
        {
            if (!located.HasValue) return false;
            var entries = text.TrimEnd().Split(new[] { ", " }, StringSplitOptions.None);
            if (entries.Length != located.Value) return false;
            foreach (var entry in entries)
                if (!Entry.IsMatch(entry)) return false;
            return true;
        }

        private static int? Value(Regex setting, string text)
        {
            var m = setting.Match(text);
            return m.Success && int.TryParse(m.Groups["v"].Value, NumberStyles.None, CultureInfo.InvariantCulture, out var value) ? value : (int?)null;
        }

        /// <summary>"about 170 MB", "about 1.7 GB" (1024 MB to a GB, as Windows counts video memory).</summary>
        private static string Amount(long mb) => mb < 1024
            ? "about " + Math.Max(10, (mb + 5) / 10 * 10).ToString(CultureInfo.InvariantCulture) + " MB"
            : "about " + Gigabytes(mb);

        private static string Gigabytes(long mb) => (mb / 1024.0).ToString("0.0", CultureInfo.InvariantCulture) + " GB";

        private static long Median(List<long> values)
        {
            var sorted = new List<long>(values);
            sorted.Sort();
            int n = sorted.Count;
            return n % 2 == 1 ? sorted[n / 2] : (sorted[n / 2 - 1] + sorted[n / 2]) / 2;
        }
    }
}
