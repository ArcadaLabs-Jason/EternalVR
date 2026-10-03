using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Data;
using EternalVR.Launcher.Core.Launch;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>
    /// Single-player only: the extra game arguments the launcher refuses. The layer builds the same data file in
    /// (src/platform/mp_policy) and its tests read the same cases (tests/platform/mp_policy/argument-cases.txt).
    /// </summary>
    public class ArgumentPolicyTests
    {
        private static ArgumentPolicy Policy() => ArgumentPolicy.Parse(TestData.Read("refused-args.txt"));

        /// <summary>The cases both test suites read: <c>refuse | arguments | pattern</c> or <c>allow | arguments</c>.</summary>
        public static IEnumerable<object[]> SharedCases() =>
            DataFile.ParseRecords(TestData.Read("argument-cases.txt"))
                .Select(r => new object[] { DataFile.Field(r, 0), DataFile.Field(r, 1), DataFile.Field(r, 2) });

        [Theory]
        [MemberData(nameof(SharedCases))]
        public void TheSharedCasesAreJudgedAsTheLayerJudgesThem(string verdict, string args, string pattern)
        {
            var reason = Policy().Check(args);
            if (verdict == "allow")
            {
                Assert.Null(reason);
                return;
            }
            Assert.Equal("refuse", verdict);
            Assert.NotNull(reason);
            Assert.StartsWith("Refused: \"" + pattern + "\" requests ", reason);
            Assert.Contains("single-player", reason);
        }

        [Fact]
        public void TheSharedCasesAreThere()
        {
            Assert.True(SharedCases().Count() > 40);
            Assert.Contains(SharedCases(), c => (string)c[1] == "+com_gamemode 2");
        }

        [Fact]
        public void EveryPatternIsWrittenAsMatchedSoTheLayersBuildTakesIt()
        {
            // The layer's CMake stops on a pattern that is not printable ASCII in lower case without spaces, quotes,
            // backslashes, brackets, semicolons or "//" (src/platform/mp_policy/CMakeLists.txt).
            var records = DataFile.ParseRecords(TestData.Read("refused-args.txt"));
            Assert.True(records.Count >= 23);
            foreach (var r in records)
            {
                Assert.Equal(2, r.Length);
                var p = r[0];
                Assert.Equal(p.ToLowerInvariant(), p);
                Assert.True(p.All(ch => ch > ' ' && ch <= '~'), p);
                Assert.True(p.IndexOfAny(new[] { '"', '\\', '[', ']', ';' }) < 0, p);
                Assert.DoesNotContain("//", p);
                Assert.False(string.IsNullOrWhiteSpace(r[1]), p);
                Assert.True(r[1].IndexOfAny(new[] { '"', '\\', '[', ']', ';' }) < 0, r[1]);
            }
        }

        [Fact]
        public void EveryPatternIsRefusedAlsoInItsSetForm()
        {
            var policy = Policy();
            foreach (var r in DataFile.ParseRecords(TestData.Read("refused-args.txt")))
            {
                Assert.NotNull(policy.Check(r[0] + " 1"));
                if (r[0].StartsWith("+")) Assert.NotNull(policy.Check("+set " + r[0].Substring(1) + " 1"));
            }
        }

        [Theory]
        [InlineData("+map game/pvp/pvp_inferno")]
        [InlineData("+map GAME\\PVP\\pvp_darkworld")]
        [InlineData("+connect 1.2.3.4")]
        [InlineData("+com_skipIntroVideo 1 +matchmaking_start")]
        [InlineData("-battlemode")]
        [InlineData("+net_serverDedicated 1")]
        [InlineData("+\"connect\" 1.2.3.4")]
        [InlineData("+map \"game/pvp/pvp_inferno\"")]
        [InlineData("+map game/\"pvp\"/pvp_inferno")]
        [InlineData("+map game//pvp//pvp_inferno")]
        [InlineData("+map game\\\\pvp\\pvp_inferno")]
        [InlineData("+ connect 1.2.3.4")]
        [InlineData("+set net_serverDedicated 1")]
        [InlineData("+SETA si_map x")]
        [InlineData("battle\"mode\"")]
        public void MultiplayerRequestsAreRefused(string args)
        {
            var reason = Policy().Check(args);
            Assert.NotNull(reason);
            Assert.Contains("single-player", reason);
        }

        [Theory]
        [InlineData(null)]
        [InlineData("")]
        [InlineData("+map game/sp/e1m1_intro/e1m1_intro")]
        [InlineData("+com_skipIntroVideo 1 +r_fullscreen 0")]
        [InlineData("+set com_skipIntroVideo 1 +map \"game/sp/e1m1_intro/e1m1_intro\"")]
        public void SinglePlayerArgumentsAreAllowed(string args)
        {
            Assert.Null(Policy().Check(args));
        }
    }
}
