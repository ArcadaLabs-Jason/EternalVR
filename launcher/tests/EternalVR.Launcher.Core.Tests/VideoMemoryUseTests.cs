using System;
using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Headsets;
using EternalVR.Launcher.Core.Launch;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>Video memory over the budget Windows gives the game, and what to change (<see cref="VideoMemoryUse"/>, T-227).</summary>
    public class VideoMemoryUseTests
    {
        /// <summary>Twelve windows of issue #35's 27-minute session (RTX 5060 8 GB, Quest 3 through Virtual Desktop, 0.1.83), as the
        /// layer logged them.</summary>
        private static readonly string[] Issue35Windows =
        {
            "[  396.484] [14436] vram: the process uses 7019 MB of local video memory, budget 6796 MB (103%); last 10 s: peak 7019 MB, 10 of 10 reading(s) over the budget",
            "[  406.484] [14436] vram: the process uses 6911 MB of local video memory, budget 6805 MB (102%); last 10 s: peak 7019 MB, 10 of 10 reading(s) over the budget",
            "[  416.484] [14436] vram: the process uses 6965 MB of local video memory, budget 6796 MB (102%); last 10 s: peak 7019 MB, 10 of 10 reading(s) over the budget",
            "[  426.484] [14436] vram: the process uses 6965 MB of local video memory, budget 6796 MB (102%); last 10 s: peak 7019 MB, 10 of 10 reading(s) over the budget",
            "[  436.484] [14436] vram: the process uses 6965 MB of local video memory, budget 6796 MB (102%); last 10 s: peak 7019 MB, 10 of 10 reading(s) over the budget",
            "[  446.484] [14436] vram: the process uses 6965 MB of local video memory, budget 6813 MB (102%); last 10 s: peak 7019 MB, 10 of 10 reading(s) over the budget",
            "[  456.484] [14436] vram: the process uses 6965 MB of local video memory, budget 6813 MB (102%); last 10 s: peak 7019 MB, 10 of 10 reading(s) over the budget",
            "[  466.484] [14436] vram: the process uses 6965 MB of local video memory, budget 6813 MB (102%); last 10 s: peak 7019 MB, 10 of 10 reading(s) over the budget",
            "[  476.500] [14436] vram: the process uses 6911 MB of local video memory, budget 6796 MB (102%); last 10 s: peak 7019 MB, 9 of 10 reading(s) over the budget",
            "[  486.500] [14436] vram: the process uses 6965 MB of local video memory, budget 6796 MB (102%); last 10 s: peak 7019 MB, 10 of 10 reading(s) over the budget",
            "[  496.500] [14436] vram: the process uses 6911 MB of local video memory, budget 6796 MB (102%); last 10 s: peak 7019 MB, 10 of 10 reading(s) over the budget",
            "[  506.500] [14436] vram: the process uses 7019 MB of local video memory, budget 6796 MB (103%); last 10 s: peak 7019 MB, 10 of 10 reading(s) over the budget",
        };

        /// <summary>The layer's count of the cvars it found, as it logs it at the game's start; a settings line counts only
        /// with as many entries.</summary>
        private static string Located(int n) => FormattableString.Invariant(
            $"[    5.516] [ 2428] game settings cvars: {n} of 36 located (0 ms)");

        /// <summary>The layer's settings line of that session, shortened to 10 entries, with the three values a test sets.</summary>
        private static string Settings(int rayTracing, int pool, int shadowAtlas) => FormattableString.Invariant(
            $"[ 1517.594] [16288] game settings: r_useRayTracing {rayTracing}, r_enableRayTracing 1 (support), r_raytracedReflections 1 (not in the menu), r_antialiasing 0, r_dlssQuality 2, is_poolSize {pool}, r_shadowAtlasHeight {shadowAtlas}, r_shadowsDistanceFadeMultiplier 2, r_lodScale 2.875, r_fullscreen 0");

        private static VideoMemoryUse Read(IEnumerable<string> lines)
        {
            var use = new VideoMemoryUse();
            foreach (var line in lines) use.Read(line);
            return use;
        }

        /// <summary>#35's windows with the start's cvar count and the given settings lines after them.</summary>
        private static VideoMemoryUse Issue35(params string[] settings) =>
            Read(new[] { Located(10) }.Concat(Issue35Windows).Concat(settings));

        private const string Opening = "The game went about 220 MB over the 6.6 GB of video memory Windows gives it for 100% of the session, "
            + "which can make it stutter or crash: in the game's video settings ";
        private const string Tail = "; if this shows again after your next session, lower Resolution on the Play tab.";

        [Fact]
        public void Issue35sSessionNamesTheTexturePoolAndTheShadowsWithHowFarOverItWent()
        {
            var use = Issue35(Settings(0, 3072, 8192));
            Assert.True(use.Over);
            Assert.Equal(12, use.Windows);
            Assert.Equal(1.0, use.OverShare, 6);
            Assert.Equal(223, use.OverByMb);
            Assert.Equal(6796, use.OverBudgetMb);
            Assert.Equal(3072, use.TexturePoolMb);
            Assert.Equal(8192, use.ShadowAtlasHeight);
            Assert.False(use.RayTracing);
            Assert.Equal(Opening + "set Texture Pool Size one step lower and Shadow Quality to High" + Tail, use.Sentence());
            Assert.Equal("video memory over the budget in 12 of 12 window(s), at 95% of it or more in 12, over it by 223 MB (median) of a "
                + "6796 MB budget; texture pool 3072, shadow atlas 8192, ray tracing off", use.LogText());
        }

        [Fact]
        public void Issue35sSummaryLeadsWithTheChange()
        {
            // The sentences: the change first, then the headset's.
            var lines = new List<string>
            {
                "[    7.657] [10176] xr: runtime 'VirtualDesktopXR' 1.0.10, api 1.0",
                "[    7.800] [24384] xr: 1 frame(s), 0 new image(s), 1 repeat(s); 0 head-tracked, 1 on the screen; pose age average 0.0 ms, max 0.0 ms; display period 8.33 ms, pose lead 0.0 ms",
                Located(10),
            };
            lines.AddRange(Issue35Windows);
            lines.Add(Settings(0, 3072, 8192));
            var summary = SessionSummary.FromLines(lines);
            Assert.Equal(Opening + "set Texture Pool Size one step lower and Shadow Quality to High" + Tail, summary.Lead);
            Assert.Equal(string.Empty, summary.Rest());
            Assert.Equal(summary.Lead, summary.Describe());
            // With other sentences the lead comes first and the rest keeps its order (four windows at 120 Hz, 60 new frames).
            for (int i = 0; i < 4; i++)
            {
                double t = 20 + 10 * i;
                lines.Add(FormattableString.Invariant($"[{t,9:0.000}] [24384] xr: {1 + 1200 * (i + 1)} frame(s), 0 new image(s), 0 repeat(s); 0 head-tracked, 0 on the screen; pose age average 0.0 ms, max 0.0 ms; display period 8.33 ms, pose lead 0.0 ms"));
                lines.Add(FormattableString.Invariant($"[{t,9:0.000}] [24384] rates: game 120.0 present(s)/s, 60.0 tick(s)/s, 60.0 stereo pair(s)/s shown; XR 120.0 frame(s)/s, 60.0 new image(s)/s"));
            }
            var played = SessionSummary.FromLines(lines);
            Assert.Equal("The game drew about 60 new frames a second at 120 Hz.", played.Rest());
            Assert.Equal(played.Lead + " " + played.Rest(), played.Describe());
        }

        [Fact]
        public void OnlyTheSettingsThatCanStillGoDownAreNamed()
        {
            // Ray tracing on: turning it off comes first, and the texture pool's step is counted from what the menu shows after
            // it, since the menu picks the pool by ray tracing too.
            Assert.Equal(Opening + "turn Raytracing off, then set Texture Pool Size one step lower than it shows after that, and "
                + "Shadow Quality to High" + Tail, Issue35(Settings(1, 4096, 8192)).Sentence());
            Assert.Equal(Opening + "turn Raytracing off, then set Texture Pool Size one step lower than it shows after that" + Tail,
                Issue35(Settings(1, 3072, 4096)).Sentence());
            Assert.Equal(Opening + "turn Raytracing off and set Shadow Quality to High" + Tail, Issue35(Settings(1, 1024, 8192)).Sentence());
            Assert.Equal(Opening + "turn Raytracing off" + Tail, Issue35(Settings(1, 1024, 2048)).Sentence());
            // Shadow Quality High or lower (an atlas 4096 high): the shadows are not named.
            Assert.Equal(Opening + "set Texture Pool Size one step lower" + Tail, Issue35(Settings(0, 2048, 4096)).Sentence());
            // The lowest texture pool (a player's 4 GB card at Low): the shadows alone, or Resolution alone.
            Assert.Equal(Opening + "set Shadow Quality to High" + Tail, Issue35(Settings(0, 1024, 8192)).Sentence());
            Assert.EndsWith("for 100% of the session, which can make it stutter or crash: lower Resolution on the Play tab.",
                Issue35(Settings(0, 1024, 2048)).Sentence());
            // The last settings line counts: the player lowered the shadows during the session.
            Assert.Equal(Opening + "set Texture Pool Size one step lower" + Tail, Issue35(Settings(0, 3072, 8192), Settings(0, 3072, 4096)).Sentence());
        }

        [Fact]
        public void AHalfWrittenSettingsLineIsNotRead()
        {
            // The game killed while the layer wrote its last settings line: "is_poolSize 30" of 3072, the rest of the line lost.
            var full = Settings(0, 3072, 8192);
            var cut = full.Substring(0, full.IndexOf("is_poolSize 3072", StringComparison.Ordinal) + "is_poolSize 30".Length);
            var use = Issue35(Settings(0, 2048, 8192), cut);
            Assert.Equal(2048, use.TexturePoolMb);
            Assert.Equal(Opening + "set Texture Pool Size one step lower and Shadow Quality to High" + Tail, use.Sentence());
            // Cut inside the last entry, the count still right: not read either.
            Assert.Equal(2048, Issue35(Settings(0, 2048, 8192), full.Substring(0, full.Length - 2)).TexturePoolMb);
            // Cut at an entry's end: fewer entries than the layer found.
            Assert.Equal(2048, Issue35(Settings(0, 2048, 8192), full.Substring(0, full.IndexOf(", r_shadowAtlasHeight", StringComparison.Ordinal))).TexturePoolMb);
            // Without the start's count a line cannot be checked, so it is not read: the texture pool is named as when unknown.
            var uncounted = Read(Issue35Windows.Concat(new[] { Settings(0, 1024, 2048) }));
            Assert.Null(uncounted.TexturePoolMb);
            Assert.Contains("set Texture Pool Size one step lower" + Tail, uncounted.Sentence());
            // A game started again in the same session counts again: its own count applies to its lines.
            Assert.Equal(1024, Read(new[] { Located(10) }.Concat(Issue35Windows).Concat(new[] { Located(3),
                "[   20.000] [ 4120] game settings: r_useRayTracing 0, is_poolSize 1024, r_shadowAtlasHeight 2048" })).TexturePoolMb);
        }

        [Fact]
        public void ALaterLineWithoutAValueKeepsTheEarlierOne()
        {
            // A later line without the texture pool (a cvar the layer did not find in that game start) or with it unreadable
            // keeps the value of the line before; ray tracing unreadable keeps its last value too.
            var use = Read(new[] { Located(10) }.Concat(Issue35Windows).Concat(new[]
            {
                Settings(1, 3072, 8192),
                Located(2),
                "[ 2000.000] [ 4120] game settings: r_useRayTracing 0, r_shadowAtlasHeight 4096",
                Located(3),
                "[ 3000.000] [ 4120] game settings: r_useRayTracing ?, is_poolSize ?, r_shadowAtlasHeight 4096",
            }));
            Assert.Equal(3072, use.TexturePoolMb);
            Assert.Equal(4096, use.ShadowAtlasHeight);
            Assert.False(use.RayTracing);
            Assert.Equal(Opening + "set Texture Pool Size one step lower" + Tail, use.Sentence());
        }

        [Fact]
        public void WithoutTheSettingsLineTheTexturePoolIsNamedAndAnUnreadValueIsUnknown()
        {
            var bare = Read(Issue35Windows);
            Assert.Null(bare.TexturePoolMb);
            Assert.Null(bare.RayTracing);
            Assert.EndsWith("set Texture Pool Size one step lower" + Tail, bare.Sentence());
            Assert.EndsWith("texture pool not logged, shadow atlas not logged, ray tracing not logged", bare.LogText());
            // A value the layer could not read is logged as "?".
            var unread = Read(Issue35Windows.Concat(new[] { Located(3), "[   20.000] [ 4120] game settings: r_useRayTracing ?, is_poolSize ?, r_shadowAtlasHeight 8192" }));
            Assert.Null(unread.TexturePoolMb);
            Assert.Null(unread.RayTracing);
            Assert.Equal(8192, unread.ShadowAtlasHeight);
        }

        [Fact]
        public void PeaksAtTheBudgetGiveNoAmount()
        {
            // Most readings over, but the window's peak not past its last budget (the budget moved): no amount is made up.
            var lines = Enumerable.Range(0, 5).Select(i => FormattableString.Invariant(
                $"[{20 + 10 * i,9:0.000}] [14436] vram: the process uses 6700 MB of local video memory, budget 6900 MB (97%); last 10 s: peak 6900 MB, 8 of 10 reading(s) over the budget"));
            var use = Read(lines);
            Assert.True(use.Over);
            Assert.Null(use.OverByMb);
            Assert.StartsWith("The game went over the video memory Windows gives it for 100% of the session, which can make it stutter", use.Sentence());
        }

        [Fact]
        public void AGigabyteOrMoreOverIsSaidInGigabytes()
        {
            // A 12 GB card at Ultra Nightmare with ray tracing (a player's 0.1.66 session): 3.4 GB past a 9.4 GB budget.
            var lines = Enumerable.Range(0, 6).Select(i => FormattableString.Invariant(
                $"[{20 + 10 * i,9:0.000}] [30044] vram: the process uses 13217 MB of local video memory, budget 9676 MB (137%); last 10 s: peak 13145 MB, 10 of 10 reading(s) over the budget"));
            Assert.StartsWith("The game went about 3.4 GB over the 9.4 GB of video memory Windows gives it for 100% of the session",
                Read(lines).Sentence());
        }

        [Fact]
        public void AnEvenCountTakesTheMeanOfTheMiddleTwo()
        {
            // Four windows past budgets of 6800 and 7000 MB by 100, 400, 200 and 300 MB: 250 MB past a 6900 MB budget.
            var windows = new[] { (100, 6800), (400, 7000), (200, 6800), (300, 7000) };
            var lines = windows.Select((w, i) => FormattableString.Invariant(
                $"[{20 + 10 * i,9:0.000}] [14436] vram: the process uses {w.Item2} MB of local video memory, budget {w.Item2} MB (100%); last 10 s: peak {w.Item2 + w.Item1} MB, 10 of 10 reading(s) over the budget"));
            var use = Read(lines);
            Assert.Equal(250, use.OverByMb);
            Assert.Equal(6900, use.OverBudgetMb);
            Assert.StartsWith("The game went about 250 MB over the 6.7 GB of video memory", use.Sentence());
        }

        [Fact]
        public void ATenthOfTheSessionOverIsSaidAndLessIsNot()
        {
            // T-227: in 198 sessions of 17 players a session had no window mostly over the budget, or 12% of them and more.
            string Window(int i, bool over) => FormattableString.Invariant(
                $"[{20 + 10 * i,9:0.000}] [14436] vram: the process uses 6600 MB of local video memory, budget 6796 MB (97%); last 10 s: peak {(over ? 7000 : 6600)} MB, {(over ? 6 : 0)} of 10 reading(s) over the budget");
            var tenth = Read(Enumerable.Range(0, 10).Select(i => Window(i, i == 4)));
            Assert.True(tenth.Over);
            Assert.StartsWith("The game went about 200 MB over the 6.6 GB of video memory Windows gives it for 10% of the session", tenth.Sentence());
            var less = Read(Enumerable.Range(0, 11).Select(i => Window(i, i == 4)));
            Assert.False(less.Over);
            // Near the budget all along (97%): the headset menu sentence instead, which asks for nothing first.
            Assert.StartsWith("The game used nearly all the video memory Windows gives it", less.Sentence());
            // Too few windows: nothing.
            var two = Read(Enumerable.Range(0, 2).Select(i => Window(i, true)));
            Assert.False(two.Over);
            Assert.Equal(string.Empty, two.Sentence());
        }

        [Fact]
        public void TheLastSessionOverTheBudgetIsAWarningAtStartToo()
        {
            var summary = SessionSummary.FromLines(new[]
            {
                "[    7.657] [10176] xr: runtime 'VirtualDesktopXR' 1.0.10, api 1.0",
                "[    7.800] [24384] xr: 1 frame(s), 0 new image(s), 1 repeat(s); 0 head-tracked, 1 on the screen; pose age average 0.0 ms, max 0.0 ms; display period 8.33 ms, pose lead 0.0 ms",
                Located(10),
            }.Concat(Issue35Windows).Concat(new[] { Settings(0, 3072, 8192) }));
            var ended = new DateTime(2026, 10, 25, 13, 37, 55);
            var facts = LastHeadset.Parse(LastHeadset.Serialize(LastHeadset.AfterSession(new HeadsetFacts(), summary, "20261025-131018", ended)));
            Assert.True(facts.SessionMemoryOver);
            Assert.Equal(StatusKind.Warning, HeadsetView.LastSessionKind(facts));
            Assert.StartsWith("Last session (today 13:37): The game went about 220 MB over", HeadsetView.LastSessionStatus(facts, ended.AddMinutes(5)));
            // A session without a summary (no display period: VR never started) keeps the last one's, the flag with it.
            var kept = LastHeadset.Parse(LastHeadset.Serialize(LastHeadset.AfterSession(facts, null, "20261025-140000", ended.AddMinutes(30))));
            Assert.True(kept.SessionMemoryOver);
            Assert.Equal(StatusKind.Warning, HeadsetView.LastSessionKind(kept));
            // A session within the budget: plain, and the key is not written.
            var within = LastHeadset.AfterSession(facts, SessionSummary.FromLines(new[]
            {
                "[    7.800] [24384] xr: 1 frame(s), 0 new image(s), 1 repeat(s); 0 head-tracked, 1 on the screen; pose age average 0.0 ms, max 0.0 ms; display period 8.33 ms, pose lead 0.0 ms",
            }), "20261025-150000", ended.AddHours(2));
            Assert.False(within.SessionMemoryOver);
            Assert.DoesNotContain("session_video_memory", LastHeadset.Serialize(within));
            Assert.Equal(StatusKind.Info, HeadsetView.LastSessionKind(within));
            Assert.Equal(StatusKind.Info, HeadsetView.LastSessionKind(null));
        }
    }
}
