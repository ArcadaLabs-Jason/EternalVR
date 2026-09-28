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
