using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.RegularExpressions;

namespace EternalVR.Launcher.Core.Report
{
    /// <summary>
    /// Removes personal identifiers from report text (T-095): the user profile path, the computer name, the Windows
    /// user name, the name played under and Steam account IDs. Rules, applied in this order:
    /// <list type="number">
    /// <item>The user's own profile folder, with <c>\</c>, <c>/</c> or JSON's <c>\\</c> separators, in any case, becomes <c>%USERPROFILE%</c>.</item>
    /// <item>Any other <c>X:\Users\&lt;name&gt;</c> folder (not Public or Default) becomes <c>%USERPROFILE%</c> too.</item>
    /// <item>The computer name as a whole word, case-insensitive, becomes <c>&lt;computer&gt;</c>; names shorter than 3
    ///       characters are left alone. It goes before the user name, which may be part of it (<c>JASON-PC</c>).</item>
    /// <item>The user name as a whole word (not inside a longer word), case-insensitive, becomes <c>&lt;user&gt;</c>;
    ///       names shorter than 3 characters are left alone, they would hit ordinary words.</item>
    /// <item>The name in the game's sign-in lines (<c>User 'name' signed in - 1234567890</c>, the name played under)
    ///       becomes <c>&lt;player&gt;</c>, and the number at the end of such a line <c>&lt;playerid&gt;</c>.</item>
    /// <item>Every name played under known (passed in, or found by the previous rule in any file of the report) with 3 or
    ///       more characters becomes <c>&lt;player&gt;</c> wherever it stands as a word of its own, in any case: a VR settings
    ///       profile named after it, for example. A name that is a placeholder's word (<c>user</c>, <c>player</c>) is left alone.</item>
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
        public const string ComputerToken = "<computer>";
        public const string PlayerToken = "<player>";
        public const string PlayerIdToken = "<playerid>";
        public const string SteamIdToken = "<steamid>";
        public const string SteamId64Token = "<steamid64>";
        public const int MinUserNameLength = 3;
        public const int MinComputerNameLength = 3;
        public const int MinPlayerNameLength = 3;
        public const int MinAccountIdDigits = 5;

        private const string Sep = @"(?:\\\\|\\|/)";
        private const RegexOptions Options = RegexOptions.IgnoreCase | RegexOptions.CultureInvariant;

        private static readonly Regex AnyProfile = new Regex(
            @"(?<![A-Za-z])[A-Za-z]:" + Sep + "Users" + Sep + @"(?!(?:Public|Default|All Users|Default User)(?:\\|/|\b))"
            + @"(?:[^\\/\r\n""'<>|:*?]+(?=\\|/)|[^\\/\s""'<>|:*?,;()\[\]]+)", Options);
        private static readonly Regex SteamId64 = new Regex(@"(?<![0-9A-Za-z])7656119\d{10}(?![0-9A-Za-z])", Options);
        // Not a date: the game's console log names its build branch "release-steam-2026-08".
        private static readonly Regex SteamFolder = new Regex(@"(?<![0-9A-Za-z])(steam-)(\d+)(?!\d|-\d)", Options);
        private static readonly Regex UserdataFolder = new Regex(@"(userdata" + Sep + @")(\d+)(?!\d)", Options);
        // The game's qconsole.log: "idSignInManager::TriggerLocalUserSignInEvent - User 'name' signed in - 1234567890".
        private static readonly Regex PlayerName = new Regex(@"(?<![\p{L}\p{N}_])(User ')([^'\r\n]+)(')", Options);
        private static readonly Regex PlayerId = new Regex(@"(User '" + PlayerToken + @"'[^\r\n]*? - )\d+(?![0-9A-Za-z])", Options);
        // The words inside the placeholders: a known name that is one of them would break the placeholders already written.
        private static readonly HashSet<string> PlaceholderWords = new HashSet<string>(
            new[] { ProfileToken, UserToken, ComputerToken, PlayerToken, PlayerIdToken, SteamIdToken, SteamId64Token }.Select(w => w.Trim('<', '>', '%')),
            StringComparer.OrdinalIgnoreCase);

        private readonly Regex ownProfile;
        private readonly Regex computerName;
        private readonly Regex userName;
        private readonly Regex accountIds;
        private readonly Regex playerNames;

        /// <param name="userProfile">The profile folder, e.g. <c>C:\Users\name</c>; may be null.</param>
        /// <param name="userName">The Windows user name; may be null.</param>
        /// <param name="steamAccountIds">Known Steam account IDs (the 32-bit numbers); may be null.</param>
        /// <param name="computerName">The computer name (<c>Environment.MachineName</c>); may be null.</param>
        /// <param name="playerNames">Known names played under (<see cref="FindPlayerNames"/>); may be null.</param>
        public Redactor(string userProfile, string userName, IEnumerable<string> steamAccountIds, string computerName = null, IEnumerable<string> playerNames = null)
        {
            this.computerName = WholeWord(computerName, MinComputerNameLength);
            var profile = (userProfile ?? string.Empty).Trim().TrimEnd('\\', '/');
            if (profile.Length >= 4)
            {
                var parts = profile.Split(new[] { '\\', '/' }, StringSplitOptions.RemoveEmptyEntries).Select(Regex.Escape);
                // Not followed by more of a folder name (a full stop ends a sentence unless a name character follows).
                const string nameChar = @"[^\\/\s""'<>|:*?,;()\[\].]";
                ownProfile = new Regex(string.Join(Sep, parts) + "(?!" + nameChar + @"|\." + nameChar + ")", Options);
            }
            this.userName = WholeWord(userName, MinUserNameLength);
            var ids = (steamAccountIds ?? Enumerable.Empty<string>())
                .Select(i => (i ?? string.Empty).Trim())
                .Where(i => i.Length >= MinAccountIdDigits && i.All(char.IsDigit))
                .Distinct()
                .OrderByDescending(i => i.Length)
                .ToList();
            if (ids.Count > 0)
                accountIds = new Regex(@"(?<![0-9A-Za-z])(?:" + string.Join("|", ids) + @")(?![0-9A-Za-z])", Options);
            var names = (playerNames ?? Enumerable.Empty<string>())
                .Select(n => (n ?? string.Empty).Trim())
                .Where(n => n.Length >= MinPlayerNameLength && !PlaceholderWords.Contains(n))
                .Distinct(StringComparer.OrdinalIgnoreCase)
                .OrderByDescending(n => n.Length)
                .Select(Regex.Escape)
                .ToList();
            if (names.Count > 0)
                this.playerNames = new Regex(@"(?<![\p{L}\p{N}_])(?:" + string.Join("|", names) + @")(?![\p{L}\p{N}_])", Options);
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

        /// <summary>The names played under in the game's sign-in lines (<c>User 'name' signed in</c>) in the texts.</summary>
        public static IReadOnlyList<string> FindPlayerNames(IEnumerable<string> texts)
        {
            var found = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var t in texts ?? Enumerable.Empty<string>())
            {
                if (string.IsNullOrEmpty(t)) continue;
                foreach (Match m in PlayerName.Matches(t)) found.Add(m.Groups[2].Value.Trim());
            }
            return found.OrderBy(x => x, StringComparer.Ordinal).ToList();
        }

        public string Apply(string text)
        {
            if (string.IsNullOrEmpty(text)) return text;
            var t = text;
            if (ownProfile != null) t = ownProfile.Replace(t, ProfileToken);
            t = AnyProfile.Replace(t, ProfileToken);
            if (computerName != null) t = computerName.Replace(t, ComputerToken);
            if (userName != null) t = userName.Replace(t, UserToken);
            t = PlayerName.Replace(t, "$1" + PlayerToken + "$3");
            t = PlayerId.Replace(t, "$1" + PlayerIdToken);
            if (playerNames != null) t = playerNames.Replace(t, PlayerToken);
            t = SteamId64.Replace(t, SteamId64Token);
            t = SteamFolder.Replace(t, m => m.Groups[1].Value + SteamIdToken);
            t = UserdataFolder.Replace(t, m => m.Groups[1].Value + SteamIdToken);
            if (accountIds != null) t = accountIds.Replace(t, SteamIdToken);
            return t;
        }

        /// <summary>A name as a whole word (not inside a longer word), in any case; null when it is shorter than <paramref name="minLength"/>.</summary>
        private static Regex WholeWord(string name, int minLength)
        {
            var n = (name ?? string.Empty).Trim();
            return n.Length < minLength ? null : new Regex(@"(?<![\p{L}\p{N}_])" + Regex.Escape(n) + @"(?![\p{L}\p{N}_])", Options);
        }
    }
}
