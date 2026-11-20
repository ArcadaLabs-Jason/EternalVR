using System.Linq;
using EternalVR.Launcher.Core.Preflight;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>
    /// RTSS warns only while its profiles limit DOOM Eternal's frame rate (RtssProfiles, known-layers.txt
    /// warn-if-rtss-limit). The samples follow RTSS 7.3.7's own ProfileTemplates\Global: tab-aligned keys, CRLF lines,
    /// the [Framerate] section among others.
    /// </summary>
    public class RtssProfilesTests
    {
        internal static string Global(string framerate = "Limit\t\t\t\t\t= 0\r\nLimitDenominator\t\t\t= 1\r\nLimitTime\t\t\t\t= 0\r\n"
                                         + "LimitTimeDenominator\t\t\t= 1\r\nIdleLimitTime\t\t\t\t= 0\r\nThrottleTime\t\t\t\t= 0\r\n"
                                         + "LimitHotkeys\t\t\t\t= 1\r\nSyncDisplay           \t\t\t= 0\r\nSyncFlush\t\t\t\t= 1\r\n"
                                         + "SyncScanline0\t\t\t\t= 0\r\nSyncScanline1\t\t\t\t= 0\r\nSyncLimiter\t\t\t\t= 0\r\n") =>
            "[Settings]\r\nPowerUser\t\t\t\t= 0\r\n\r\n[OSD]                                                \t\r\nEnableOSD\t\t\t\t= 1\r\n"
            + "RefreshPeriod\t\t\t\t= 500\r\n\r\n[Framerate]\r\n" + framerate + "\r\n[Hooking]\r\nEnableHooking\t\t\t\t= 1\r\n";

        internal static RtssLimit FromFiles(string global, string game = null, string manifest = "RivaTuner Statistics Server/Vulkan/RTSSVkLayer64.json")
        {
            using (var t = new TempDir())
            {
                var path = t.Write(manifest, "{}");
                if (global != null) t.Write("RivaTuner Statistics Server/Profiles/Global", global);
                if (game != null) t.Write("RivaTuner Statistics Server/Profiles/DOOMEternalx64vk.exe.cfg", game);
                return RtssProfiles.Read(path);
            }
        }

        internal static LayerDecision Decide(RtssLimit limit)
        {
            var known = KnownLayers.Parse(TestData.Read("known-layers.txt"));
            var rtss = new InstalledLayer(LayerApi.Vulkan, "VK_LAYER_RTSS", "RTSSVkLayer64.json", "HKLM", true, "DISABLE_RTSS_LAYER", "1");
            var d = known.Evaluate(new[] { rtss }, false, _ => limit).Single();
            UiText.AssertNoAdvice(d.Message, "rtss");
            Assert.Null(d.Environment);
            return d;
        }

        [Fact]
        public void NoLimitInTheProfilesIsNoWarning()
        {
            var limit = FromFiles(Global());
            Assert.True(limit.Readable);
            Assert.False(limit.LimitsTheGame);
            var d = Decide(limit);
            Assert.Equal(LayerAction.Ignore, d.Action);
            Assert.Equal("Vulkan layer VK_LAYER_RTSS: ok (no frame rate limit for DOOM Eternal in RTSS's profiles)", d.Message);
        }

        [Fact]
        public void AGlobalLimitWarnsWithItsNumber()
        {
            var limit = FromFiles(Global().Replace("Limit\t\t\t\t\t= 0", "Limit\t\t\t\t\t= 60"));
            Assert.Equal(new[] { "a limit of 60 frames per second in its Global profile" }, limit.Limits);
            var d = Decide(limit);
            Assert.Equal(LayerAction.Warn, d.Action);
            Assert.StartsWith("Vulkan layer VK_LAYER_RTSS: warning (RTSS limits DOOM Eternal's frame rate: a limit of 60 frames per second "
                + "in its Global profile. RivaTuner Statistics Server works, but a frame rate limit set in RTSS halves", d.Message);
        }

        [Fact]
        public void TheGamesOwnProfileOverridesTheGlobalOne()
        {
            var limited = Global().Replace("Limit\t\t\t\t\t= 0", "Limit\t\t\t\t\t= 60");
            Assert.False(FromFiles(limited, "[Framerate]\r\nLimit\t\t\t\t\t= 0\r\n").LimitsTheGame);
            // Keys the game's profile leaves out come from the global one.
            Assert.False(FromFiles(limited, "[OSD]\r\nEnableOSD\t= 0\r\n[Framerate]\r\nLimit = 0\r\n").LimitsTheGame);
            Assert.Equal(new[] { "a limit of 59.94 frames per second in its DOOMEternalx64vk.exe.cfg profile" },
                FromFiles(Global(), "[Framerate]\r\nLimit\t\t= 5994\r\nLimitDenominator\t= 100\r\n").Limits);
            Assert.Equal(new[] { "a limit of 60 frames per second in its Global profile" },
                FromFiles(limited, "[OSD]\r\nLimit = 0\r\n").Limits);
        }

        [Fact]
        public void FrameTimeIdleScanlineAndThrottleSettingsCountAsLimits()
        {
            Assert.Equal(new[] { "a frame time limit of 16667 microseconds (about 60 frames per second) in its Global profile" },
                FromFiles(Global().Replace("\r\nLimitTime\t\t\t\t= 0", "\r\nLimitTime\t\t\t\t= 16667")).Limits);
            Assert.Equal(new[] { "a limit of 33333 microseconds a frame while the game is not the active window in its "
                + "DOOMEternalx64vk.exe.cfg profile" }, FromFiles(Global(), "[Framerate]\r\nIdleLimitTime = 33333\r\n").Limits);
            Assert.Equal(new[] { "scanline sync, which ties frames to the display's refresh, in its Global profile" },
                FromFiles(Global().Replace("SyncScanline0\t\t\t\t= 0", "SyncScanline0\t\t\t\t= -1")).Limits);
            Assert.Equal(new[] { "ThrottleTime 2, a setting RTSS does not document, in its Global profile" },
                FromFiles(Global().Replace("ThrottleTime\t\t\t\t= 0", "ThrottleTime\t\t\t\t= 2")).Limits);
        }

        [Fact]
        public void UnreadableProfilesWarnAsBefore()
        {
            Assert.Contains("there is no", FromFiles(null).Problem);
            Assert.Equal("its Global profile has no [Framerate] Limit", FromFiles("[OSD]\r\nLimit = 0\r\n").Problem);
            Assert.Equal("Limit = sixty in its Global profile is not a number",
                FromFiles(Global().Replace("Limit\t\t\t\t\t= 0", "Limit\t\t\t\t\t= sixty")).Problem);
            Assert.Equal("its install folder is not known", RtssProfiles.Read("RTSSVkLayer64.json").Problem);

            var d = Decide(FromFiles(null));
            Assert.Equal(LayerAction.Warn, d.Action);
            Assert.Contains("a frame rate limit set in RTSS halves the headset's frame rate (60 gives 30)", d.Message);
            Assert.Contains("its profiles could not be read to see whether it sets one: there is no ", d.Message);
        }

        [Fact]
        public void TheInstallFolderIsAboveTheVulkanFolder()
        {
            var install = System.IO.Path.Combine("x", "RivaTuner Statistics Server");
            Assert.Equal(install, RtssProfiles.InstallDir(System.IO.Path.Combine(install, "Vulkan", "RTSSVkLayer64.json")));
            Assert.Equal(install, RtssProfiles.InstallDir(System.IO.Path.Combine(install, "RTSSVkLayer64.json")));
            Assert.Null(RtssProfiles.InstallDir(null));
        }
    }
}
