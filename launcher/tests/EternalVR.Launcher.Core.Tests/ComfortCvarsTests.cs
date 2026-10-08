using System;
using System.Linq;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>
    /// The comfort set (view bob, kicks, shakes, the damage tint and blur and the rest): in stereo the layer holds it at run time
    /// (stereo_seq::stereoComfortCvars), so it is not on the command line and a multiplayer guard trip gives the player's values back;
    /// in mono the layer holds none, so the command line keeps it. HDR output stays on the command line in both.
    /// </summary>
    public class ComfortCvarsTests
    {
        /// <summary>stereo_seq::stereoComfortCvars without r_hdrDisplay and r_waterPostProcess (held in stereo only, never on the
        /// command line; session-keys.txt restores it).</summary>
        private static readonly string[] ComfortSet =
        {
            "r_motionblur", "r_dof", "r_chromaticAberration", "r_vignette", "pm_noBob", "view_skipKicks", "view_skipShakes", "hands_fovScale",
            "meatHook_playerViewOverrideMode", "view_skipDamageEffect", "view_showPlayerDamageViewEffect", "view_damageBlur", "g_skipViewEffects",
        };

        private static readonly ForcedCvars Shipped = ForcedCvars.Parse(TestData.Read("forced-cvars.txt"));

        private static string CommandLine(VrMode mode) => LaunchPlanBuilder.Build(new LaunchInputs
        {
            GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
            LayerDir = @"E:\EternalVR\layer",
            Settings = new LauncherSettings { Mode = mode },
            ForcedCvars = Shipped,
        }).CommandLine + " ";

        [Fact]
        public void StereoLeavesTheComfortSetToTheLayer()
        {
            var line = CommandLine(VrMode.Stereo);
            foreach (var name in ComfortSet)
                Assert.DoesNotContain("+" + name + " ", line);
            Assert.Contains("+r_hdrDisplay 0 ", line);
        }

        [Fact]
        public void MonoKeepsTheComfortSetOnTheCommandLine()
        {
            var line = CommandLine(VrMode.Mono);
            foreach (var arg in new[] { "+r_motionblur 0", "+r_dof 0", "+r_chromaticAberration 0", "+r_vignette 0", "+pm_noBob 1", "+view_skipKicks 1",
                                        "+view_skipShakes 1", "+view_skipDamageEffect 1", "+view_showPlayerDamageViewEffect 0", "+view_damageBlur 0",
                                        "+g_skipViewEffects 1", "+hands_fovScale 1", "+meatHook_playerViewOverrideMode 1", "+r_hdrDisplay 0" })
                Assert.Contains(arg + " ", line);
        }

        [Fact]
        public void TheShippedListForcesTheComfortSetInMonoOnlyAndTheRestoreKeepsIt()
        {
            foreach (var name in ComfortSet)
            {
                var cvar = Assert.Single(Shipped.All, c => c.Name == name);
                Assert.True(cvar.MonoOnly);
                Assert.False(cvar.StereoOnly);
                Assert.Contains(name, Shipped.Names);
            }
            // The session restore puts the player's own values back after either mode.
            var restored = Data.LauncherData.Load(TestData.Dir).RestoredKeys;
            foreach (var name in ComfortSet.Concat(new[] { "r_hdrDisplay" }))
                Assert.Contains(name, restored);
            Assert.DoesNotContain(Shipped.For(true), c => c.MonoOnly);
            Assert.DoesNotContain(Shipped.For(false), c => c.StereoOnly);
        }

        [Fact]
        public void ForcedCvarsTakeAStereoOrMonoScope()
        {
            var list = ForcedCvars.Parse("a | 1 | mono\nb | 2 | Stereo\nc | 3");
            Assert.Equal(new[] { "a", "c" }, list.For(false).Select(c => c.Name));
            Assert.Equal(new[] { "b", "c" }, list.For(true).Select(c => c.Name));
            Assert.Equal(new[] { "a", "b", "c" }, list.Names);
            Assert.Throws<FormatException>(() => ForcedCvars.Parse("a | 1 | flat"));
            Assert.Throws<FormatException>(() => ForcedCvars.Parse("a | $eye_width | mono"));
        }
    }
}
