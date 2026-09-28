using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.RegularExpressions;

namespace EternalVR.Launcher.Core.Report
{
    /// <summary>
    /// Removes personal identifiers from report text (T-095): the user profile path, the Windows user name and
    /// Steam account IDs. Rules, applied in this order:
    /// <list type="number">
    /// <item>The user's own profile folder, with <c>\</c>, <c>/</c> or JSON's <c>\\</c> separators, in any case, becomes <c>%USERPROFILE%</c>.</item>
    /// <item>Any other <c>X:\Users\&lt;name&gt;</c> folder (not Public or Default) becomes <c>%USERPROFILE%</c> too.</item>
    /// <item>The user name as a whole word (not inside a longer word), case-insensitive, becomes <c>&lt;user&gt;</c>;
    ///       names shorter than 3 characters are left alone, they would hit ordinary words.</item>
    /// <item>A 17-digit SteamID64 (<c>7656119</c> and 10 digits) becomes <c>&lt;steamid64&gt;</c>.</item>
    /// <item><c>steam-&lt;digits&gt;</c> (the game's save folders) and <c>userdata\&lt;digits&gt;</c> (Steam's) keep their prefix, with <c>&lt;steamid&gt;</c> for the number.</item>
    /// <item>Every account ID known (passed in, or found by the previous rule in any file of the report) with 5 or more
    ///       digits becomes <c>&lt;steamid&gt;</c> wherever it stands alone (not inside a longer run of letters or digits).</item>
    /// </list>
    /// Numbers that are not a known ID (versions, addresses, timestamps, process IDs, hashes) are left alone.
    /// </summary>
    public sealed class Redactor
    {
        public const string ProfileToken = "%USERPROFILE%";
        public const string UserToken = "<user>";
        public const string SteamIdToken = "<steamid>";
        public const string SteamId64Token = "<steamid64>";
        public const int MinUserNameLength = 3;
        public const int MinAccountIdDigits = 5;

        private const string Sep = @"(?:\\\\|\\|/)";
        private const RegexOptions Options = RegexOptions.IgnoreCase | RegexOptions.CultureInvariant;

        private static readonly Regex AnyProfile = new Regex(
            @"(?<![A-Za-z])[A-Za-z]:" + Sep + "Users" + Sep + @"(?!(?:Public|Default|All Users|Default User)(?:\\|/|\b))"
            + @"(?:[^\\/\r\n""'<>|:*?]+(?=\\|/)|[^\\/\s""'<>|:*?,;()\[\]]+)", Options);
        private static readonly Regex SteamId64 = new Regex(@"(?<![0-9A-Za-z])7656119\d{10}(?![0-9A-Za-z])", Options);
        private static readonly Regex SteamFolder = new Regex(@"(?<![0-9A-Za-z])(steam-)(\d+)(?!\d)", Options);
        private static readonly Regex UserdataFolder = new Regex(@"(userdata" + Sep + @")(\d+)(?!\d)", Options);

        private readonly Regex ownProfile;
        private readonly Regex userName;
        private readonly Regex accountIds;

        /// <param name="userProfile">The profile folder, e.g. <c>C:\Users\name</c>; may be null.</param>
        /// <param name="userName">The Windows user name; may be null.</param>
        /// <param name="steamAccountIds">Known Steam account IDs (the 32-bit numbers); may be null.</param>
        public Redactor(string userProfile, string userName, IEnumerable<string> steamAccountIds)
        {
            var profile = (userProfile ?? string.Empty).Trim().TrimEnd('\\', '/');
            if (profile.Length >= 4)
            {
                var parts = profile.Split(new[] { '\\', '/' }, StringSplitOptions.RemoveEmptyEntries).Select(Regex.Escape);
                // Not followed by more of a folder name (a full stop ends a sentence unless a name character follows).
                const string nameChar = @"[^\\/\s""'<>|:*?,;()\[\].]";
                ownProfile = new Regex(string.Join(Sep, parts) + "(?!" + nameChar + @"|\." + nameChar + ")", Options);
            }
            var name = (userName ?? string.Empty).Trim();
            if (name.Length >= MinUserNameLength)
                this.userName = new Regex(@"(?<![\p{L}\p{N}_])" + Regex.Escape(name) + @"(?![\p{L}\p{N}_])", Options);
            var ids = (steamAccountIds ?? Enumerable.Empty<string>())
                .Select(i => (i ?? string.Empty).Trim())
                .Where(i => i.Length >= MinAccountIdDigits && i.All(char.IsDigit))
                .Distinct()
                .OrderByDescending(i => i.Length)
                .ToList();
            if (ids.Count > 0)
                accountIds = new Regex(@"(?<![0-9A-Za-z])(?:" + string.Join("|", ids) + @")(?![0-9A-Za-z])", Options);
        }

        /// <summary>The Steam account IDs named by <c>steam-&lt;digits&gt;</c> or <c>userdata\&lt;digits&gt;</c> in the texts.</summary>
        public static IReadOnlyList<string> FindSteamAccountIds(IEnumerable<string> texts)
        {
            var found = new HashSet<string>(StringComparer.Ordinal);
            foreach (var t in texts ?? Enumerable.Empty<string>())
            {
                if (string.IsNullOrEmpty(t)) continue;
                foreach (Match m in SteamFolder.Matches(t)) found.Add(m.Groups[2].Value);
                foreach (Match m in UserdataFolder.Matches(t)) found.Add(m.Groups[2].Value);
            }
            return found.OrderBy(x => x, StringComparer.Ordinal).ToList();
        }

        public string Apply(string text)
        {
            if (string.IsNullOrEmpty(text)) return text;
            var t = text;
            if (ownProfile != null) t = ownProfile.Replace(t, ProfileToken);
            t = AnyProfile.Replace(t, ProfileToken);
            if (userName != null) t = userName.Replace(t, UserToken);
            t = SteamId64.Replace(t, SteamId64Token);
            t = SteamFolder.Replace(t, m => m.Groups[1].Value + SteamIdToken);
            t = UserdataFolder.Replace(t, m => m.Groups[1].Value + SteamIdToken);
            if (accountIds != null) t = accountIds.Replace(t, SteamIdToken);
            return t;
        }
    }
}
