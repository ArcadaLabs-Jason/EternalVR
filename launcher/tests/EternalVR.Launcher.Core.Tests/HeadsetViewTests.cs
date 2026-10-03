using System;
using System.Linq;
using EternalVR.Launcher.Core.Headsets;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Report;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>The Play tab's Headset box, Each eye line, Resolution's list and Frame pacing's choice in words (<see cref="HeadsetView"/>).</summary>
    public class HeadsetViewTests
    {
        private static readonly HeadsetTable Table = HeadsetTable.Parse(TestData.Read("headsets.txt"));
        private static readonly DateTime Now = new DateTime(2026, 10, 1, 11, 30, 0);
        private const string VdManifest = @"D:\Runtimes\VD\virtualdesktop-openxr.json";
        private const string SteamManifest = @"D:\Steam\steamapps\common\SteamVR\steamxr_win64.json";

        private static ViewLimits Limits(uint recW, uint recH) => new ViewLimits
        {
            Recommended = new Extent(recW, recH),
            MaxImageRect = new Extent(16384, 16384),
            MaxSwapchain = new Extent(16384, 16384),
        };

        private static HeadsetFacts Read(string runtime, string system, uint recW, uint recH, string manifest = VdManifest) => new HeadsetFacts
        {
            Limits = Limits(recW, recH),
            RuntimeName = runtime,
            SystemName = system,
            RuntimeManifest = manifest,
            ReadAt = new DateTime(2026, 10, 1, 9, 12, 0),
        };

        private static HeadsetFacts Quest3 => Read("VirtualDesktopXR", "Meta Quest 3", 2496, 2688);

        private static HeadsetIdentity Id(HeadsetFacts f, SteamVrHeadset seen = null) => HeadsetIdentity.Identify(f.RuntimeName, f.SystemName, seen, Table);

        private static string EachEye(HeadsetFacts f, double scale = 1.0, ResolutionBase b = ResolutionBase.Auto, string renderSize = "auto",
            AntiAliasingMode aa = AntiAliasingMode.Taa, DlssQuality q = DlssQuality.Quality, SteamVrHeadset seen = null) =>
            HeadsetView.EachEye(new LauncherSettings { RenderScale = scale, ResolutionBase = b, RenderSize = renderSize, AntiAliasing = aa, Dlss = q },
                f, f == null ? null : Id(f, seen));

        [Fact]
        public void EachEyeBeforeAnyProbeIsSetAtLaunch()
        {
            Assert.Equal("Set from your headset at Launch VR", EachEye(null));
            Assert.Equal("Set from your headset at Launch VR", EachEye(new HeadsetFacts(), 1.5, ResolutionBase.Ask));
        }

        [Fact]
        public void EachEyeOnAutoComparesWithTheAskAndThePanel()
        {
            // Quest 3 through Virtual Desktop at High: 2496x2688 fitted into 2064x2208 pixels gives 2056x2216, 82% per side.
            Assert.Equal("2056 x 2216   82% of what VD asks for, 100% of the native panel", EachEye(Quest3));
            // 1.20: 2472x2656, 44% more pixels than Auto at 1.00, which earns the note.
            Assert.Equal("2472 x 2656   99% of what VD asks for, 120% of the native panel\n"
                + "44% more pixels than Auto", EachEye(Quest3, 1.2));
            // Lower: no note.
            Assert.Equal("1648 x 1776   66% of what VD asks for, 80% of the native panel", EachEye(Quest3, 0.8));
        }

        [Fact]
        public void EachEyeOnTheAskAndOnThePanel()
        {
            Assert.Equal("2496 x 2688   100% of what VD asks for, 121% of the native panel\n"
                + "47% more pixels than Auto", EachEye(Quest3, 1.0, ResolutionBase.Ask));
            Assert.Equal("2064 x 2208   83% of what VD asks for, 100% of the native panel", EachEye(Quest3, 1.0, ResolutionBase.Panel));
            // A quarter more pixels is the threshold: 1.10 of the panel is 21% more, no note.
            Assert.DoesNotContain("more pixels", EachEye(Quest3, 1.1, ResolutionBase.Panel));
        }

        [Fact]
        public void EachEyeWithDlssSaysAboutWhatItDraws()
        {
            Assert.Equal("2496 x 2688   100% of what VD asks for, 121% of the native panel\nDLSS Quality draws about 1664 x 1792\n"
                + "47% more pixels than Auto",
                EachEye(Quest3, 1.0, ResolutionBase.Ask, aa: AntiAliasingMode.Dlss));
            Assert.Contains("DLSS Performance draws about 1028 x 1108", EachEye(Quest3, aa: AntiAliasingMode.Dlss, q: DlssQuality.Performance));
            Assert.Contains("DLSS Balanced draws about 1192 x 1285", EachEye(Quest3, aa: AntiAliasingMode.Dlss, q: DlssQuality.Balanced));
            Assert.Contains("DLSS Ultra Performance draws about 685 x 739", EachEye(Quest3, aa: AntiAliasingMode.Dlss, q: DlssQuality.UltraPerformance));
            Assert.Contains("DLAA draws about 2056 x 2216", EachEye(Quest3, aa: AntiAliasingMode.Dlss, q: DlssQuality.Dlaa));
            Assert.DoesNotContain("DLSS", EachEye(Quest3, aa: AntiAliasingMode.Off));
        }

        [Fact]
        public void EachEyeOnSteamVrAndWithoutAKnownPanel()
        {
            var index = Read("SteamVR/OpenXR", "SteamVR/OpenXR : lighthouse", 2016, 2224, SteamManifest);
            var seen = new SteamVrHeadset { Manufacturer = "Valve Corporation", Model = "Index", Driver = "lighthouse" };
            Assert.Equal("2016 x 2224   100% of what SteamVR asks for, 140% of the native panel", EachEye(index, seen: seen));
            // PlayStation VR2 is named by its tracking system alone.
            var psvr2 = Read("SteamVR/OpenXR", "SteamVR/OpenXR : playstation_vr2", 2804, 2860, SteamManifest);
            Assert.Equal("2112 x 2160   75% of what SteamVR asks for, 106% of the native panel", EachEye(psvr2));
            Assert.Equal("2000 x 2040   71% of what SteamVR asks for, 100% of the native panel", EachEye(psvr2, 1.0, ResolutionBase.Panel));
            // A lighthouse headset SteamVR has not named: no panel to compare with, and the native panel falls back to Auto.
            var lighthouse = Read("SteamVR/OpenXR", "SteamVR/OpenXR : lighthouse", 3188, 3540, SteamManifest);
            Assert.Equal("2024 x 2248   63% of what SteamVR asks for", EachEye(lighthouse));
            Assert.Equal("2024 x 2248   63% of what SteamVR asks for\nThis headset's panel is not known: Auto is used",
                EachEye(lighthouse, 1.0, ResolutionBase.Panel));
        }

        [Fact]
        public void EachEyeForAFixedSizeOffAndMono()
        {
            Assert.Equal("2064 x 2208 (fixed size, render_size in launcher.ini)", EachEye(null, 1.0, ResolutionBase.Auto, "2064x2208"));
            Assert.Equal("2064 x 2208 (fixed size, render_size in launcher.ini)", EachEye(Quest3, 1.5, ResolutionBase.Ask, "2064x2208"));
            // The layer rounds a fixed size to multiples of 8 too.
            Assert.Equal("2064 x 2104 (fixed size, render_size in launcher.ini)", EachEye(null, 1.0, ResolutionBase.Auto, "2060x2100"));
            Assert.Equal("The game window's size (render_size off)", EachEye(Quest3, 1.0, ResolutionBase.Auto, "off"));
            var mono = new LauncherSettings { Mode = VrMode.Mono };
            Assert.Equal("The game's own resolution (mono)", HeadsetView.EachEye(mono, Quest3, Id(Quest3)));
            Assert.Equal(SettingRules.NeedsStereo, SettingRules.WhyNot(Setting.EachEye, mono));
            Assert.Null(SettingRules.WhyNot(Setting.EachEye, new LauncherSettings()));
        }

        [Fact]
        public void EachEyeAfterACappedSessionSaysWhatItGotThenThePlan()
        {
            // An AMD Radeon RX 6000 on a 1080p display: the window held each eye at 958x1009 of the planned 2056x2216.
            var cap = new RenderCap(new Extent(958, 1009), new Extent(2056, 2216));
            var s = new LauncherSettings();
            Assert.Equal("958 x 1009 last session, 45% of the planned 2056 x 2216\n"
                + "Your graphics driver renders at the window's size\n"
                + "Planned: 2056 x 2216   82% of what VD asks for, 100% of the native panel",
                HeadsetView.EachEye(s, Quest3, Id(Quest3), cap));
            // The plan follows Resolution, with its notes; the last session's line stays until a session is not capped.
            Assert.Equal("958 x 1009 last session, 45% of the planned 2056 x 2216\n"
                + "Your graphics driver renders at the window's size\n"
                + "Planned: 2472 x 2656   99% of what VD asks for, 120% of the native panel\n"
                + "44% more pixels than Auto",
                HeadsetView.EachEye(new LauncherSettings { RenderScale = 1.2 }, Quest3, Id(Quest3), cap));
            Assert.EndsWith("\nPlanned: 2064 x 2208 (fixed size, render_size in launcher.ini)",
                HeadsetView.EachEye(new LauncherSettings { RenderSize = "2064x2208" }, Quest3, Id(Quest3), cap));
            Assert.EndsWith("the window's size\nSet from your headset at Launch VR", HeadsetView.EachEye(s, null, null, cap));
            // Off and mono say what they always say.
            Assert.Equal("The game window's size (render_size off)", HeadsetView.EachEye(new LauncherSettings { RenderSize = "off" }, Quest3, Id(Quest3), cap));
            Assert.Equal("The game's own resolution (mono)", HeadsetView.EachEye(new LauncherSettings { Mode = VrMode.Mono }, Quest3, Id(Quest3), cap));
        }

        [Fact]
        public void EachEyeAfterAFullSizeSessionIsThePlanAlone()
        {
            var full = new RenderCap(new Extent(2056, 2216), new Extent(2056, 2216));
            Assert.False(full.Capped);
            Assert.Equal("2056 x 2216   82% of what VD asks for, 100% of the native panel", HeadsetView.EachEye(new LauncherSettings(), Quest3, Id(Quest3), full));
            Assert.Equal(EachEye(Quest3), HeadsetView.EachEye(new LauncherSettings(), Quest3, Id(Quest3), null));
        }

        [Fact]
        public void TheBoxBeforeAnyProbeSaysWhatToDo()
        {
            var v = HeadsetView.For(new HeadsetFacts(), null, VdManifest, "VirtualDesktopXR (Bundled)", Now);
            Assert.Equal("Not read yet (runtime: Virtual Desktop (VDXR))", v.HeadsetLine);
            Assert.Equal("Read at Launch VR, or with Detect again.", v.State);
            Assert.False(v.Old);
            Assert.Equal("not known yet", v.PanelLine);
            Assert.Equal("The runtime asks for", v.AsksLabel);
            Assert.Equal("not read yet", v.AsksLine);
            Assert.Equal("shown after your first session", v.RefreshLine);
            // No runtime set at all.
            Assert.Equal("Not read yet", HeadsetView.For(new HeadsetFacts(), null, null, null, Now).HeadsetLine);
            // A try that found no headset.
            var failed = new HeadsetFacts { FailedAt = Now.AddMinutes(-5), FailedReason = "the runtime reports no headset" };
            var f = HeadsetView.For(failed, null, VdManifest, "VirtualDesktopXR (Bundled)", Now);
            Assert.Equal("The last try (today 11:25) failed: the runtime reports no headset. Read again at Launch VR.", f.State);
            Assert.True(f.Old);
            // Not read when the launcher opened, with nothing read before.
            var skipped = HeadsetView.For(new HeadsetFacts(), null, VdManifest, "VirtualDesktopXR (Bundled)", Now, "the Virtual Desktop Streamer is not running");
            Assert.Equal("Not read when the launcher opened (the Virtual Desktop Streamer is not running). Read at Launch VR.", skipped.State);
            Assert.False(skipped.Old);
        }

        [Fact]
        public void TheBoxAsReadWithVirtualDesktop()
        {
            var facts = Quest3;
            facts.SessionRuntime = "VirtualDesktopXR";
            facts.SessionRefresh = "90 Hz, steady";
            facts.SessionRefreshHz = 90;
            var v = HeadsetView.For(facts, Id(facts), VdManifest, "VirtualDesktopXR (Bundled)", Now);
            Assert.Equal("Meta Quest 3 via Virtual Desktop (VDXR)", v.HeadsetLine);
            Assert.Null(v.State);
            Assert.False(v.Old);
            Assert.Equal("2064 x 2208 per eye", v.PanelLine);
            Assert.Equal("VD asks for", v.AsksLabel);
            Assert.Equal("2496 x 2688 per eye (1.21x the panel)\nread at Launch VR, today 09:12", v.AsksLine);
            Assert.Equal("90 Hz, steady (last session)", v.RefreshLine);
            Assert.Contains("Virtual Desktop's VR Graphics Quality", v.AsksTip);
            Assert.DoesNotContain("untested", v.AsksTip);
            Assert.Contains("Virtual Desktop's Frame rate", v.RefreshTip);
            Assert.Contains("Virtual Desktop's SSW", v.RefreshTip);
            // The same manifest written another way is the same runtime.
            Assert.Null(HeadsetView.For(facts, Id(facts), VdManifest.Replace('\\', '/') + "/", "VirtualDesktopXR (Bundled)", Now).State);
        }

        [Fact]
        public void ARuntimeChangedSinceOrAFailedTryMakesTheValuesOld()
        {
            var facts = Quest3;
            var changed = HeadsetView.For(facts, Id(facts), SteamManifest, "SteamVR", Now);
            Assert.Equal("Last read with Virtual Desktop (VDXR); the runtime is now SteamVR. Read again at Launch VR.", changed.State);
            Assert.True(changed.Old);
            Assert.EndsWith("read at Launch VR, today 09:12 (old)", changed.AsksLine);

            facts.FailedAt = Now.AddDays(-1).AddHours(-2);
            facts.ReadAt = Now.AddDays(-2);
            facts.FailedReason = "the runtime reports no headset";
            var failed = HeadsetView.For(facts, Id(facts), VdManifest, "VirtualDesktopXR (Bundled)", Now);
            Assert.Equal("Old values: the last try (yesterday 09:30) failed: the runtime reports no headset. Read again at Launch VR.",
                failed.State);
            Assert.EndsWith("read at Launch VR, 29 Sep 11:30 (old)", failed.AsksLine);
            // A failure before the last answer no longer counts.
            facts.ReadAt = Now.AddHours(-1);
            facts.ReadBy = HeadsetReadBy.Detect;
            var fresh = HeadsetView.For(facts, Id(facts), VdManifest, "VirtualDesktopXR (Bundled)", Now);
            Assert.Null(fresh.State);
            Assert.EndsWith("read by Detect again, today 10:30", fresh.AsksLine);
            facts.ReadBy = HeadsetReadBy.Start;
            Assert.EndsWith("read when the launcher opened, today 10:30", HeadsetView.For(facts, Id(facts), VdManifest, "VirtualDesktopXR", Now).AsksLine);
        }

        [Fact]
        public void AnOlderLaunchersFileHasSizesButNoName()
        {
            var v = HeadsetView.For(new HeadsetFacts { Limits = Limits(2496, 2688) }, null, VdManifest, "VirtualDesktopXR (Bundled)", Now);
            Assert.Equal("Not named yet (read by an older version of the launcher)", v.HeadsetLine);
            Assert.Equal("Named the next time it is read.", v.State);
            Assert.Equal("2496 x 2688 per eye\nread at an earlier Launch VR", v.AsksLine);
            Assert.Equal("not known yet", v.PanelLine);
        }

        [Fact]
        public void SteamVrHeadsetsAndUnlistedOnes()
        {
            var lighthouse = Read("SteamVR/OpenXR", "SteamVR/OpenXR : lighthouse", 3188, 3540, SteamManifest);
            var unknown = HeadsetView.For(lighthouse, Id(lighthouse), SteamManifest, "SteamVR", Now);
            Assert.Equal("A SteamVR headset (lighthouse)", unknown.HeadsetLine);
            Assert.Equal("not in our list", unknown.PanelLine);
            Assert.Equal("SteamVR asks for", unknown.AsksLabel);
            Assert.Equal("3188 x 3540 per eye\nread at Launch VR, today 09:12", unknown.AsksLine);
            Assert.Contains("SteamVR's Settings (Video, Render resolution", unknown.AsksTip);
            Assert.Contains("no headset native choice", unknown.PanelTip);

            var seen = new SteamVrHeadset { Manufacturer = "Valve Corporation", Model = "Index", Driver = "lighthouse" };
            var index = HeadsetView.For(lighthouse, Id(lighthouse, seen), SteamManifest, "SteamVR", Now);
            Assert.Equal("Valve Index (last seen by SteamVR) via SteamVR", index.HeadsetLine);
            Assert.Equal("1440 x 1600 per eye", index.PanelLine);
            Assert.StartsWith("3188 x 3540 per eye (2.21x the panel)", index.AsksLine);

            // Virtual Desktop names a Quest 2 as "Oculus Quest2": the list's name is added to the panel.
            var quest2 = Read("VirtualDesktopXR", "Oculus Quest2", 2688, 2688);
            Assert.Equal("1832 x 1920 per eye (Meta Quest 2)", HeadsetView.For(quest2, Id(quest2), VdManifest, "VirtualDesktopXR (Bundled)", Now).PanelLine);
            // Meta Link is untested so far: its tooltips say so.
            var link = Read("Oculus", "Meta Quest 3", 1632, 1760, @"D:\Meta\oculus_openxr_64.json");
            var linkView = HeadsetView.For(link, Id(link), @"D:\Meta\oculus_openxr_64.json", "Oculus OpenXR", Now);
            Assert.Equal("Meta Link asks for", linkView.AsksLabel);
            Assert.Contains("Meta Horizon Link app", linkView.AsksTip);
            Assert.Contains("untested", linkView.AsksTip);
        }

        [Fact]
        public void TheRefreshSaysWhenItWasAnotherRoute()
        {
            var facts = Quest3;
            facts.SessionRuntime = "SteamVR/OpenXR";
            facts.SessionRefresh = "144 Hz, throttled to 72 for 18% of play";
            Assert.Equal("144 Hz, throttled to 72 for 18% of play (last session, with SteamVR)",
                HeadsetView.For(facts, Id(facts), VdManifest, "VirtualDesktopXR (Bundled)", Now).RefreshLine);
        }

        [Fact]
        public void ResolutionsListNamesTheRuntimeAndTheHeadsetOnlyWhenKnown()
        {
            var vd = HeadsetView.ResolutionChoices(Id(Quest3), ResolutionBase.Auto, "VirtualDesktopXR (Bundled)");
            Assert.Equal(new[] { ResolutionBase.Auto, ResolutionBase.Ask, ResolutionBase.Panel }, vd.Select(c => c.Key));
            Assert.Equal(new[] { "Auto (fits about 4.6 MP per eye)", "Virtual Desktop native", "Quest 3 native" }, vd.Select(c => c.Value));
            // Without the current runtime's name, the runtime that last answered names it.
            Assert.Equal("Virtual Desktop native", HeadsetView.ResolutionChoices(Id(Quest3), ResolutionBase.Auto)[1].Value);
            // A lighthouse headset SteamVR has not named: no native choice, no guess.
            var lighthouse = Read("SteamVR/OpenXR", "SteamVR/OpenXR : lighthouse", 3188, 3540, SteamManifest);
            Assert.Equal(new[] { "Auto (fits about 4.6 MP per eye)", "SteamVR native" },
                HeadsetView.ResolutionChoices(Id(lighthouse), ResolutionBase.Ask, "SteamVR").Select(c => c.Value));
            // Named by SteamVR's last seen model: "Index native", "Steam Frame native".
            var seen = new SteamVrHeadset { Manufacturer = "Valve Corporation", Model = "Index", Driver = "lighthouse" };
            Assert.Equal("Index native", HeadsetView.ResolutionChoices(Id(lighthouse, seen), ResolutionBase.Panel, "SteamVR").Last().Value);
            var deckard = new SteamVrHeadset { Manufacturer = "Valve", Model = "Deckard MP", Driver = "lighthouse" };
            Assert.Equal("Steam Frame native", HeadsetView.NativeChoice(Id(lighthouse, deckard)));
            // Quest Link, or Virtual Desktop through SteamVR, with only SteamVR's "oculus" tracking system: no native choice.
            var oculus = Read("SteamVR/OpenXR", "SteamVR/OpenXR : oculus", 2016, 2224, SteamManifest);
            Assert.Null(HeadsetView.NativeChoice(Id(oculus)));
            Assert.Equal(2, HeadsetView.ResolutionChoices(Id(oculus), ResolutionBase.Auto, "SteamVR").Count);
            // Meta's own runtime: "Quest Link native".
            var link = Read("Oculus", "Meta Quest 3", 1632, 1760, @"D:\Meta\oculus_openxr_64.json");
            Assert.Equal(new[] { "Auto (fits about 4.6 MP per eye)", "Quest Link native", "Quest 3 native" },
                HeadsetView.ResolutionChoices(Id(link), ResolutionBase.Auto, "Oculus OpenXR").Select(c => c.Value));
            // The runtime the next launch uses wins over the one that last answered.
            Assert.Equal("SteamVR native", HeadsetView.ResolutionChoices(Id(Quest3), ResolutionBase.Auto, "SteamVR")[1].Value);
            // A player on the headset's size whose headset is not known keeps the choice, told that Auto is used.
            Assert.Equal("Headset native (not known: Auto is used)", HeadsetView.ResolutionChoices(Id(lighthouse), ResolutionBase.Panel).Last().Value);
            Assert.Equal("Runtime native", HeadsetView.ResolutionChoices(null, ResolutionBase.Auto)[1].Value);
            Assert.Equal("Pimax Play native", HeadsetView.RuntimeChoice(null, "Pimax OpenXR"));
            Assert.Equal("My Runtime native", HeadsetView.RuntimeChoice(null, "My Runtime"));
        }

        [Fact]
        public void ResolutionsTooltipSaysTheRuntimesSizeIsItsQualitySettingNotThePanel()
        {
            var tip = HeadsetView.ResolutionTip(Id(Quest3), "VirtualDesktopXR (Bundled)");
            Assert.Equal("Virtual Desktop native: what Virtual Desktop asks for at its current quality setting "
                + "(Virtual Desktop's VR Graphics Quality), not the panel. Quest 3 native: your headset's own panel, 2064 x 2208 per eye.", tip);
            var lighthouse = Read("SteamVR/OpenXR", "SteamVR/OpenXR : lighthouse", 3188, 3540, SteamManifest);
            var steam = HeadsetView.ResolutionTip(Id(lighthouse), "SteamVR");
            Assert.Contains("SteamVR's resolution slider", steam);
            Assert.EndsWith("No headset native choice: the launcher does not know your headset's model.", steam);
        }

        [Fact]
        public void TheBoxWhileReadingAndWhenNotReadAtStart()
        {
            var facts = Quest3;
            var reading = HeadsetView.For(facts, Id(facts), VdManifest, "VirtualDesktopXR (Bundled)", Now, reading: true);
            Assert.Equal("Reading the headset from Virtual Desktop (VDXR)...", reading.State);
            Assert.False(reading.Old);
            // Not read at start: the last values stay, said to be old, and Launch VR reads them again.
            var skipped = HeadsetView.For(facts, Id(facts), VdManifest, "VirtualDesktopXR (Bundled)", Now, "SteamVR is not running");
            Assert.Equal("Old values: not read when the launcher opened (SteamVR is not running). Read again at Launch VR.",
                skipped.State);
            Assert.True(skipped.Old);
            Assert.EndsWith("(old)", skipped.AsksLine);
        }

        [Fact]
        public void FramePacingNamesTheLastRefreshRate()
        {
            Assert.Equal("Matched to the headset (default)", HeadsetView.PacingChoice(new HeadsetFacts()));
            Assert.Equal("Matched to the headset (90 Hz last session)", HeadsetView.PacingChoice(new HeadsetFacts { SessionRefreshHz = 90 }));
        }

        [Fact]
        public void TheLastSessionIsTheStatusAtStart()
        {
            Assert.Null(HeadsetView.LastSessionStatus(new HeadsetFacts(), Now));
            var facts = new HeadsetFacts
            {
                SessionAt = Now.AddDays(-1),
                SessionText = "The game kept up with your headset: about 90 new frames a second at 90 Hz.",
            };
            Assert.Equal("Last session (yesterday 11:30): The game kept up with your headset: about 90 new frames a second at 90 Hz.",
                HeadsetView.LastSessionStatus(facts, Now));
        }

        [Fact]
        public void TimesAreSaidPlainly()
        {
            Assert.Equal("today 09:12", HeadsetView.When(new DateTime(2026, 10, 1, 9, 12, 0), Now));
            Assert.Equal("yesterday 21:40", HeadsetView.When(new DateTime(2026, 9, 30, 21, 40, 0), Now));
            Assert.Equal("28 Sep 21:40", HeadsetView.When(new DateTime(2026, 9, 28, 21, 40, 0), Now));
            Assert.Equal("28 Dec 2025 21:40", HeadsetView.When(new DateTime(2025, 12, 28, 21, 40, 0), Now));
            Assert.Equal("2496 x 2688", HeadsetView.Spaced(new Extent(2496, 2688)));
        }

        [Fact]
        public void NoEmDashesInTheWindowsWords()
        {
            var facts = Quest3;
            var v = HeadsetView.For(facts, Id(facts), SteamManifest, "SteamVR", Now);
            foreach (var text in new[] { v.HeadsetLine, v.State, v.PanelLine, v.AsksLine, v.RefreshLine, v.PanelTip, v.AsksTip, v.RefreshTip,
                SettingTexts.For(Setting.Resolution).Tooltip, SettingTexts.For(Setting.Headset).Tooltip, SettingTexts.For(Setting.FramePacing).Tooltip })
            {
                Assert.DoesNotContain("\u2014", text);
                Assert.DoesNotContain("\u2013", text);
            }
        }
    }
}
