using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.RegularExpressions;
using EternalVR.Launcher.Core.Data;

namespace EternalVR.Launcher.Core.Launch
{
    /// <summary>
    /// Single-player only (T-109, ARCHITECTURE section 4a): extra command-line text that asks for a
    /// multiplayer or BATTLEMODE context is refused before anything else happens. The patterns are data
    /// (<c>data\refused-args.txt</c>: <c>pattern | reason</c>), matched case-insensitively as substrings
    /// of the normalised argument text: the arguments as the game receives them (quotes removed), slashes
    /// unified and repeated ones collapsed, and <c>+ cmd</c>, <c>+set cmd</c> and <c>+seta cmd</c> read as
    /// <c>+cmd</c>, so quoting or spelling a command differently does not slip past a pattern.
    /// </summary>
    public sealed class ArgumentPolicy
    {
        private readonly List<KeyValuePair<string, string>> patterns;

        public ArgumentPolicy(IEnumerable<KeyValuePair<string, string>> patterns) { this.patterns = patterns.ToList(); }

        public static ArgumentPolicy Parse(string text)
        {
            var list = new List<KeyValuePair<string, string>>();
            foreach (var r in DataFile.ParseRecords(text))
            {
                var p = DataFile.Field(r, 0);
                if (p.Length == 0) throw new FormatException("refused-args: empty pattern");
                list.Add(new KeyValuePair<string, string>(Normalise(p), DataFile.Field(r, 1)));
            }
            return new ArgumentPolicy(list);
        }

        /// <summary>Null when the arguments are allowed, else the reason for the refusal.</summary>
        public string Check(string extraArguments)
        {
            if (string.IsNullOrWhiteSpace(extraArguments)) return null;
            var text = " " + Normalise(string.Join(" ", LaunchPlanBuilder.SplitArguments(extraArguments))) + " ";
            foreach (var p in patterns)
                if (text.IndexOf(p.Key, StringComparison.OrdinalIgnoreCase) >= 0)
                    return $"Refused: \"{p.Key.Trim()}\" requests {p.Value}. EternalVR is single-player only; launch the game from Steam without VR for multiplayer.";
            return null;
        }

        private static string Normalise(string s)
        {
            var t = s.Replace('\\', '/').Replace("\"", string.Empty);
            t = Regex.Replace(t, @"\s+", " ").Trim();
            t = Regex.Replace(t, "/{2,}", "/");
            t = Regex.Replace(t, @"\+\s*(?:seta?\s+)?", "+", RegexOptions.IgnoreCase);
            return t;
        }
    }
}
