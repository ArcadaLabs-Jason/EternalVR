using EternalVR.Launcher.Core.Report;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    public class RedactionTests
    {
        private static readonly Redactor R = new Redactor(@"C:\Users\Robin", "Robin", new[] { "12345678" });

        [Theory]
        [InlineData(@"save backup: C:\Users\Robin\AppData\Local\EternalVR\save-backups\1", @"save backup: %USERPROFILE%\AppData\Local\EternalVR\save-backups\1")]
        [InlineData(@"c:\users\ROBIN\Saved Games", @"%USERPROFILE%\Saved Games")]
        [InlineData("C:/Users/Robin/AppData", "%USERPROFILE%/AppData")]
        [InlineData(@"{""path"": ""C:\\Users\\Robin\\AppData\\x.json""}", @"{""path"": ""%USERPROFILE%\\AppData\\x.json""}")]
        [InlineData(@"data folder C:\Users\Robin", "data folder %USERPROFILE%")]
        [InlineData(@"in C:\Users\Robin.", "in %USERPROFILE%.")]
        public void OwnProfilePathBecomesThePlaceholder(string input, string expected) => Assert.Equal(expected, R.Apply(input));

        [Theory]
        [InlineData(@"C:\Users\Someone Else\AppData\Local", @"%USERPROFILE%\AppData\Local")]
        [InlineData(@"D:\Users\bob\x", @"%USERPROFILE%\x")]
        [InlineData(@"C:\Users\Robin.CORP\x", @"%USERPROFILE%\x")]
        [InlineData(@"profile C:\Users\alice", "profile %USERPROFILE%")]
        public void OtherProfilePathsAreRedactedToo(string input, string expected) => Assert.Equal(expected, R.Apply(input));

        [Theory]
        [InlineData(@"C:\Users\Public\Documents")]
        [InlineData(@"C:\Users\Default\NTUSER.DAT")]
        [InlineData(@"C:\WINDOWS\SYSTEM32\vulkan-1.dll")]
        [InlineData(@"E:\SteamLibrary\steamapps\common\DOOMEternal")]
        public void OtherPathsAreKept(string input) => Assert.Equal(input, R.Apply(input));

        [Fact]
        public void ALongerProfileNameIsNotCutShort()
        {
            // C:\Users\Robin2 is another profile: the generic rule takes it whole, never leaving "2" behind.
            Assert.Equal(@"%USERPROFILE%\x", R.Apply(@"C:\Users\Robin2\x"));
        }

        [Theory]
        [InlineData("logged in as Robin today", "logged in as <user> today")]
        [InlineData("user=robin;", "user=<user>;")]
        [InlineData(@"\\ROBIN\share", @"\\<user>\share")]
        public void UserNameAloneBecomesThePlaceholder(string input, string expected) => Assert.Equal(expected, R.Apply(input));

        [Theory]
        [InlineData("Robins and Robinville and MrRobin and Robin_x")]
        [InlineData("%USERPROFILE% stays as it is")]
        public void UserNameInsideLongerWordsIsKept(string input) => Assert.Equal(input, R.Apply(input));

        [Fact]
        public void ShortUserNamesAreNotReplacedAlone()
        {
            var r = new Redactor(@"C:\Users\al", "al", null);
            Assert.Equal("al is a word in: it is all", r.Apply("al is a word in: it is all"));
            Assert.Equal(@"%USERPROFILE%\x", r.Apply(@"C:\Users\al\x"));
        }

        private static readonly Redactor WithComputer = new Redactor(@"C:\Users\Jason", "Jason", null, "DESKTOP-7Q2XK9M");

        [Theory]
        [InlineData("\tHost Name: DESKTOP-7Q2XK9M", "\tHost Name: <computer>")]
        [InlineData("Crash.desktop-7q2xk9m.00014.html", "Crash.<computer>.00014.html")]
        [InlineData(@"\\DESKTOP-7Q2XK9M\share", @"\\<computer>\share")]
        [InlineData("(DESKTOP-7Q2XK9M)", "(<computer>)")]
        public void ComputerNameAloneBecomesThePlaceholder(string input, string expected) => Assert.Equal(expected, WithComputer.Apply(input));

        [Theory]
        [InlineData("XDESKTOP-7Q2XK9M and DESKTOP-7Q2XK9M2 and DESKTOP-7Q2XK9M_x and myDESKTOP-7Q2XK9M")]
        [InlineData("DESKTOP-7Q2 and XK9M")]
        public void ComputerNameInsideLongerWordsIsKept(string input) => Assert.Equal(input, WithComputer.Apply(input));

        [Fact]
        public void ShortComputerNamesAreNotReplaced()
        {
            var r = new Redactor(null, null, null, "PC");
            Assert.Equal("PC and pc stay", r.Apply("PC and pc stay"));
            Assert.Equal("<computer> here", new Redactor(null, null, null, " ABC ").Apply("abc here"));
        }

        [Fact]
        public void AComputerNameHoldingTheUserNameIsReplacedWhole()
        {
            var r = new Redactor(@"C:\Users\Jason", "Jason", null, "JASON-PC");
            Assert.Equal("on <computer> as <user>", r.Apply("on jason-pc as Jason"));
        }

        [Theory]
        [InlineData("idSignInManager::TriggerLocalUserSignInEvent - User 'SlayerFan42' signed in - 1234567890",
                    "idSignInManager::TriggerLocalUserSignInEvent - User '<player>' signed in - <playerid>")]
        [InlineData("idSignInManager::TriggerLocalUserSignInEvent - User 'Some Name' promoted - 42", "idSignInManager::TriggerLocalUserSignInEvent - User '<player>' promoted - <playerid>")]
        [InlineData("User 'x' left\nnext line - 123", "User '<player>' left\nnext line - 123")]
        public void TheNamePlayedUnderBecomesThePlaceholder(string input, string expected) => Assert.Equal(expected, R.Apply(input));

        [Fact]
        public void KnownNamesPlayedUnderAreReplacedAsWords()
        {
            var r = new Redactor(null, "alex", null, "TESTBOX", new[] { "TestPilot", "Night Shift", "ab", "player", " ", null });
            Assert.Equal(@"controls\profiles\<player>\valve_index.toml", r.Apply(@"controls\profiles\TestPilot\valve_index.toml"));
            Assert.Equal("profile = <player> and <player>", r.Apply("profile = testpilot and Night Shift"));
            Assert.Equal("TestPilots and xTestPilot and TestPilot_2", r.Apply("TestPilots and xTestPilot and TestPilot_2"));
            Assert.Equal("ab stays, so does the player", r.Apply("ab stays, so does the player"));
            Assert.Equal("User '<player>' signed in - <playerid> on <computer> as <user>", r.Apply("User 'TestPilot' signed in - 1234567890 on TESTBOX as alex"));
            // A name that is a placeholder's word never breaks the placeholders.
            Assert.Equal("<user> on <computer>", new Redactor(null, "alex", null, "TESTBOX", new[] { "user", "computer" }).Apply("alex on TESTBOX"));
        }

        [Fact]
        public void NamesPlayedUnderAreFound()
        {
            var names = Redactor.FindPlayerNames(new[]
            {
                "idSignInManager::TriggerLocalUserSignInEvent - User 'TestPilot' signed in - 1234567890", null,
                "User 'Night Shift' promoted - 42\nuser 'testpilot' signed in", "no sign-in here",
            });
            Assert.Equal(new[] { "Night Shift", "TestPilot" }, names);
        }

        [Theory]
        [InlineData("restore: unchanged steam-12345678/PROFILE/profile.bin", "restore: unchanged steam-<steamid>/PROFILE/profile.bin")]
        [InlineData(@"E:\Steam\userdata\12345678\782330\remote", @"E:\Steam\userdata\<steamid>\782330\remote")]
        [InlineData("account 12345678 logged in", "account <steamid> logged in")]
        [InlineData("id=12345678,", "id=<steamid>,")]
        [InlineData("owner 76561197972611406 here", "owner <steamid64> here")]
        [InlineData("[U:1:12345678]", "[U:1:<steamid>]")]
        public void SteamIdsBecomePlaceholders(string input, string expected) => Assert.Equal(expected, R.Apply(input));

        [Fact]
        public void UnknownSteamFolderIdsAreRedactedByTheirShape()
        {
            var r = new Redactor(null, null, null);
            Assert.Equal("steam-<steamid>/PROFILE", r.Apply("steam-1234/PROFILE"));
            Assert.Equal("userdata/<steamid>/7", r.Apply("userdata/123456/7"));
        }

        [Fact]
        public void AccountIdsFoundInOneFileAreFound()
        {
            var ids = Redactor.FindSteamAccountIds(new[] { "restore: steam-11112222/PROFILE", null, @"C:\Steam\userdata\33334444\config" });
            Assert.Equal(new[] { "11112222", "33334444" }, ids);
            var r = new Redactor(null, null, ids);
            Assert.Equal("user <steamid> and <steamid>", r.Apply("user 11112222 and 33334444"));
        }

        [Theory]
        [InlineData("vulkan-1.dll version 1.4.357.0")]
        [InlineData("launcher 0.1.0+0123456789abcdef0123456789abcdef01234567")]
        [InlineData("mp guard: Steam join callbacks at RVA 0x1BC3AB6, timestamp 0x6a7b9b8c")]
        [InlineData("log start 2026-09-26 16:30:52.250, session 20260926-163050, pid 51804")]
        [InlineData("Known game build 25216728 (retail), eye size 2496x2688")]
        [InlineData("device created: LUID valid 00000000:00014209")]
        [InlineData("sha256 a76561197972611406b0000000000000000000000000000000000000000000000")]
        [InlineData("x123456781 and 1123456780 and 1234567")]
        [InlineData("SteamLibrary\\steamapps\\common and steam running")]
        [InlineData("\t\tBranch: release-steam-2026-08")]
        public void OrdinaryNumbersAndTextAreKept(string input) => Assert.Equal(input, R.Apply(input));

        [Fact]
        public void ShortAccountIdsAreNotReplacedAlone()
        {
            var r = new Redactor(null, null, new[] { "1234", "12a45" });
            Assert.Equal("value 1234 and 12a45", r.Apply("value 1234 and 12a45"));
        }

        [Fact]
        public void NullAndEmptyPassThrough()
        {
            Assert.Null(R.Apply(null));
            Assert.Equal(string.Empty, R.Apply(string.Empty));
        }
    }
}
