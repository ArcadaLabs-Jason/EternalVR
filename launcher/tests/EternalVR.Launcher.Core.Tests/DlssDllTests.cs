using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>The DLSS version (NVIDIA's newest, the game's or the player's file), the preset, the layer's variables, the rules,
    /// the file check and the "In the headset" line.</summary>
    public class DlssDllTests
    {
        private const string Chosen = @"E:\Mods\DLSS 310\nvngx_dlss.dll";
        private const string Downloaded = @"E:\EternalVR data\dlss\310.9.1.0\nvngx_dlss.dll";

        private static readonly DlssRelease Newest = new DlssRelease(new Version(310, 9, 1, 0), new Uri("https://example.invalid/nvngx_dlss.dll"),
            1000, new string('a', 64), new Uri("https://example.invalid/LICENSE.txt"));

        private static Dictionary<string, string> Env(LauncherSettings s, string newest = null) =>
            LaunchPlanBuilder.Build(new LaunchInputs
            {
                GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
                LayerDir = @"E:\EternalVR\layer",
                Settings = s,
                ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
                NewestDlss = newest,
            }).Environment.ToDictionary(e => e.Key, e => e.Value);

        private static LauncherSettings WithFile(string preset = "K") => new LauncherSettings
        {
            AntiAliasing = AntiAliasingMode.Dlss, DlssDll = DlssDllChoice.File, DlssDllPath = Chosen, DlssPreset = preset,
        };

        /// <summary>The first bytes of a PE file: MZ, e_lfanew, "PE\0\0", the machine and the characteristics.</summary>
        private static byte[] Headers(ushort machine, ushort characteristics)
        {
            var b = new byte[0x200];
            b[0] = (byte)'M';
            b[1] = (byte)'Z';
            b[0x3C] = 0x80;
            b[0x80] = (byte)'P';
            b[0x81] = (byte)'E';
            BitConverter.GetBytes(machine).CopyTo(b, 0x84);
            BitConverter.GetBytes(characteristics).CopyTo(b, 0x84 + 18);
            return b;
        }

        [Fact]
        public void TheNewestIsTheDefaultAndReachesTheLayerOnceDownloaded()
        {
            var s = new LauncherSettings();
            Assert.Equal(DlssDllChoice.Newest, s.DlssDll);
            Assert.Equal(DlssDll.RecommendedPreset, s.DlssPreset);
            s.AntiAliasing = AntiAliasingMode.Dlss;
            // Not downloaded yet: the game's own runs, nothing is sent.
            Assert.False(Env(s).ContainsKey("ETERNALVR_DLSS_DLL"));
            Assert.False(Env(s).ContainsKey("ETERNALVR_DLSS_PRESET"));
            var env = Env(s, Downloaded);
            Assert.Equal(Downloaded, env["ETERNALVR_DLSS_DLL"]);
            Assert.Equal("K", env["ETERNALVR_DLSS_PRESET"]);
            s.DlssPreset = DlssDll.AutomaticPreset;
            Assert.Equal("default", Env(s, Downloaded)["ETERNALVR_DLSS_PRESET"]);
            // TAA or mono: no DLSS, no file.
            s.AntiAliasing = AntiAliasingMode.Taa;
            Assert.False(Env(s, Downloaded).ContainsKey("ETERNALVR_DLSS_DLL"));
            s.AntiAliasing = AntiAliasingMode.Dlss;
            s.Mode = VrMode.Mono;
            Assert.False(Env(s, Downloaded).ContainsKey("ETERNALVR_DLSS_DLL"));
        }

        [Fact]
        public void TheGamesDllSendsNothing()
        {
            var s = WithFile();
            s.DlssDll = DlssDllChoice.Game;
            // A path kept from before and a download: still nothing with the game's chosen.
            Assert.False(Env(s, Downloaded).ContainsKey("ETERNALVR_DLSS_DLL"));
            Assert.False(Env(s, Downloaded).ContainsKey("ETERNALVR_DLSS_PRESET"));
        }

        [Fact]
        public void AChosenFileReachesTheLayerOnlyWithDlssInStereo()
        {
            var env = Env(WithFile(), Downloaded);
            Assert.Equal(Chosen, env["ETERNALVR_DLSS_DLL"]);
            Assert.Equal("K", env["ETERNALVR_DLSS_PRESET"]);
            Assert.Equal("default", Env(WithFile("default"))["ETERNALVR_DLSS_PRESET"]);
            // An unknown preset in the file: NVIDIA's pick.
            Assert.Equal("default", Env(WithFile("Z"))["ETERNALVR_DLSS_PRESET"]);

            var taa = WithFile();
            taa.AntiAliasing = AntiAliasingMode.Taa;
            Assert.False(Env(taa).ContainsKey("ETERNALVR_DLSS_DLL"));
            var mono = WithFile();
            mono.Mode = VrMode.Mono;
            Assert.False(Env(mono).ContainsKey("ETERNALVR_DLSS_DLL"));
            var none = WithFile();
            none.DlssDllPath = "  ";
            Assert.False(Env(none).ContainsKey("ETERNALVR_DLSS_DLL"));
        }

        [Fact]
        public void TheChoiceThePathAndThePresetRoundTrip()
        {
            var text = WithFile("j").Serialize();
            Assert.Contains("dlss_version = file", text);
            Assert.DoesNotContain("dlss_dll =", text);
            var back = LauncherSettings.Parse(text);
            Assert.Equal(DlssDllChoice.File, back.DlssDll);
            Assert.Equal(Chosen, back.DlssDllPath);
            Assert.Equal("J", back.DlssPreset);
            Assert.Empty(back.UnknownKeys);
            Assert.Equal(DlssDllChoice.Game, LauncherSettings.Parse("schema_version = 2\ndlss_version = game\n").DlssDll);
            Assert.Equal(DlssDllChoice.Newest, LauncherSettings.Parse("schema_version = 2\ndlss_version = latest\n").DlssDll);
            // Automatic, once chosen, stays.
            Assert.Equal("default", LauncherSettings.Parse("schema_version = 2\ndlss_version = newest\ndlss_preset = default\n").DlssPreset);
            Assert.Equal("default", LauncherSettings.Parse("schema_version = 2\ndlss_preset = H\n").DlssPreset);
            // A file without any DLSS key: the newest, the recommended preset.
            var bare = LauncherSettings.Parse("schema_version = 2\nanti_aliasing = dlss\n");
            Assert.Equal(DlssDllChoice.Newest, bare.DlssDll);
            Assert.Equal(string.Empty, bare.DlssDllPath);
            Assert.Equal("K", bare.DlssPreset);
        }

        [Fact]
        public void AnOlderLaunchersDefaultsBecomeTheNewestWithTheRecommendedPreset()
        {
            // 0.1.9 and older always wrote dlss_dll: game and the DLL's default unless changed.
            var old = LauncherSettings.Parse("schema_version = 2\nanti_aliasing = dlss\ndlss_dll = game\ndlss_dll_path = \ndlss_preset = default\n");
            Assert.Equal(DlssDllChoice.Newest, old.DlssDll);
            Assert.Equal("K", old.DlssPreset);
            Assert.Empty(old.UnknownKeys);
            // A file the player chose stays theirs; a preset they picked stays too.
            var file = LauncherSettings.Parse("schema_version = 2\ndlss_dll = file\ndlss_dll_path = " + Chosen + "\ndlss_preset = default\n");
            Assert.Equal(DlssDllChoice.File, file.DlssDll);
            Assert.Equal(Chosen, file.DlssDllPath);
            Assert.Equal("K", file.DlssPreset);
            Assert.Equal("J", LauncherSettings.Parse("schema_version = 2\ndlss_dll = game\ndlss_preset = J\n").DlssPreset);
            // Written back in the new form.
            Assert.Contains("dlss_version = newest", old.Serialize());
        }

        [Fact]
        public void ResetAndProfilesKeepThisMachinesFile()
        {
            var reset = WithFile().WithDefaults();
            Assert.Equal(Chosen, reset.DlssDllPath);
            Assert.Equal(DlssDllChoice.Newest, reset.DlssDll);
            Assert.Equal("K", reset.DlssPreset);

            using (var t = new TempDir())
            {
                var store = new ProfileStore(t.Combine("profiles"));
                store.Save("Evening", WithFile("J"));
                Assert.DoesNotContain(Chosen, File.ReadAllText(t.Combine("profiles", "Evening.ini")));
                var current = new LauncherSettings { DlssDllPath = @"F:\Other\nvngx_dlss.dll" };
                var loaded = store.Load("Evening", current);
                Assert.Equal(DlssDllChoice.File, loaded.DlssDll);
                Assert.Equal("J", loaded.DlssPreset);
                Assert.Equal(@"F:\Other\nvngx_dlss.dll", loaded.DlssDllPath);
            }
        }

        [Fact]
        public void TheRowsNeedDlssAndThePresetNeedsANewerDlss()
        {
            var newest = new LauncherSettings { AntiAliasing = AntiAliasingMode.Dlss };
            foreach (var row in new[] { Setting.DlssQuality, Setting.DlssVersion, Setting.DlssPreset, Setting.DlssInHeadset })
            {
                Assert.Null(SettingRules.WhyNot(row, newest));
                Assert.Null(SettingRules.WhyNot(row, WithFile()));
                Assert.Equal(SettingRules.NeedsDlss, SettingRules.WhyNot(row, new LauncherSettings()));
                Assert.Equal(SettingRules.NeedsStereo,
                    SettingRules.WhyNot(row, new LauncherSettings { AntiAliasing = AntiAliasingMode.Dlss, Mode = VrMode.Mono }));
            }
            var game = WithFile();
            game.DlssDll = DlssDllChoice.Game;
            Assert.Null(SettingRules.WhyNot(Setting.DlssVersion, game));
            Assert.Equal(SettingRules.NeedsNewerDlss, SettingRules.WhyNot(Setting.DlssPreset, game));
            // The window's lists: one choice per value.
            Assert.Equal(Enum.GetValues(typeof(DlssDllChoice)).Length, SettingTexts.For(Setting.DlssVersion).Choices.Count);
            Assert.Equal(Enum.GetValues(typeof(DlssDllChoice)).Length, DlssDll.VersionOrder.Distinct().Count());
            Assert.Equal(DlssDllChoice.Newest, DlssDll.VersionOrder[0]);
            Assert.Equal(Enum.GetValues(typeof(DlssQuality)).Length, SettingTexts.For(Setting.DlssQuality).Choices.Count);
            Assert.Equal(DlssDll.PresetValues.Length, SettingTexts.For(Setting.DlssPreset).Choices.Count);
            Assert.Equal(1, DlssDll.PresetIndex("k"));
            Assert.Equal(0, DlssDll.PresetIndex("transformer"));
        }

        [Fact]
        public void TheLineSaysWhatRunsInTheHeadset()
        {
            var s = new LauncherSettings { AntiAliasing = AntiAliasingMode.Dlss };
            Assert.Equal("DLSS 310.9.1, preset K, Quality, both eyes", DlssDll.WhatRuns(s, Newest, true, null));
            Assert.Equal("The game's DLSS 2.3, Quality, both eyes, until you download DLSS 310.9.1", DlssDll.WhatRuns(s, Newest, false, null));
            Assert.Equal("The game's DLSS 2.3, Quality, both eyes", DlssDll.WhatRuns(s, null, false, null));
            s.DlssPreset = DlssDll.AutomaticPreset;
            s.Dlss = DlssQuality.Performance;
            Assert.Equal("DLSS 310.9.1, NVIDIA's preset, Performance, both eyes", DlssDll.WhatRuns(s, Newest, true, null));
            s.DlssDll = DlssDllChoice.Game;
            s.Dlss = DlssQuality.UltraPerformance;
            Assert.Equal("The game's DLSS 2.3, Ultra Performance, both eyes", DlssDll.WhatRuns(s, Newest, true, null));

            var file = WithFile();
            var ok = new DlssDll.Check { Version = new Version(310, 5, 3, 0) };
            Assert.Equal("DLSS 310.5.3 from your file, preset K, Quality, both eyes", DlssDll.WhatRuns(file, Newest, true, ok));
            var noPresets = new DlssDll.Check { Version = new Version(2, 5, 1, 0) };
            Assert.Equal("DLSS 2.5.1 from your file, Quality, both eyes", DlssDll.WhatRuns(file, Newest, true, noPresets));
            var gone = new DlssDll.Check { Problem = "The file is not there any more." };
            Assert.Equal("The game's DLSS 2.3, Quality, both eyes: your file cannot be used", DlssDll.WhatRuns(file, Newest, true, gone));
        }

        [Fact]
        public void SharpeningIsTheGamesUnlessChosenAndReachesTheLayerInStereo()
        {
            var s = new LauncherSettings();
            Assert.Equal(SharpeningMode.Game, s.Sharpening);
            Assert.False(Env(s).ContainsKey("ETERNALVR_SHARPENING"));
            var expected = new Dictionary<SharpeningMode, string>
            {
                [SharpeningMode.Off] = "0", [SharpeningMode.Low] = "1", [SharpeningMode.Medium] = "2", [SharpeningMode.High] = "3",
            };
            foreach (var kv in expected)
            {
                s.Sharpening = kv.Key;
                Assert.Equal(kv.Value, Env(s)["ETERNALVR_SHARPENING"]);
                Assert.Equal(kv.Key, LauncherSettings.Parse(s.Serialize()).Sharpening);
            }
            s.Mode = VrMode.Mono;
            Assert.False(Env(s).ContainsKey("ETERNALVR_SHARPENING"));
            Assert.Equal(SettingRules.NeedsStereo, SettingRules.WhyNot(Setting.Sharpening, s));
            Assert.Null(SettingRules.WhyNot(Setting.Sharpening, new LauncherSettings()));
            Assert.Equal(SharpeningMode.Game, LauncherSettings.Parse("schema_version = 2\nsharpening = extreme\n").Sharpening);
            Assert.Equal(Enum.GetValues(typeof(SharpeningMode)).Length, SettingTexts.For(Setting.Sharpening).Choices.Count);
        }

        [Fact]
        public void TheFileCheckWantsNvidiasSixtyFourBitDll()
        {
            Assert.Null(DlssDll.PeProblem(Headers(0x8664, 0x2022)));
            Assert.Equal("The file is not a 64-bit DLL.", DlssDll.PeProblem(Headers(0x014C, 0x2102)));
            Assert.NotNull(DlssDll.PeProblem(Headers(0x8664, 0x0022))); // a program, not a DLL
            Assert.NotNull(DlssDll.PeProblem(new byte[] { (byte)'M', (byte)'Z' }));
            Assert.NotNull(DlssDll.PeProblem(null));

            using (var t = new TempDir())
            {
                Assert.False(DlssDll.Inspect(null).Ok);
                Assert.Contains("named nvngx_dlss.dll", DlssDll.Inspect(t.Combine("dlss", "nvngx_dlss_310.dll")).Problem);
                Assert.Contains("not there", DlssDll.Inspect(t.Combine("dlss", "nvngx_dlss.dll")).Problem);
                var text = t.Write("text/nvngx_dlss.dll", "not a dll");
                Assert.Equal("The file is not a Windows DLL.", DlssDll.Inspect(text).Problem);
                var bare = t.Combine("bare", "nvngx_dlss.dll");
                Directory.CreateDirectory(Path.GetDirectoryName(bare));
                File.WriteAllBytes(bare, Headers(0x8664, 0x2022));
                var check = DlssDll.Inspect(bare);
                Assert.False(check.Ok);
                Assert.Contains("version information", check.Problem);
                Assert.False(check.HasPresets);
            }
        }

        [Fact]
        public void TheLineUnderTheChoiceSaysTheVersion()
        {
            Assert.Equal("Version 310.9.1.0", DlssDll.Describe(new DlssDll.Check { Version = new Version(310, 9, 1, 0) }));
            Assert.Equal("Version 2.2.6.0 (not newer than the game's)", DlssDll.Describe(new DlssDll.Check { Version = new Version(2, 2, 6, 0) }));
            Assert.True(new DlssDll.Check { Version = new Version(3, 1, 0, 0) }.HasPresets);
            Assert.False(new DlssDll.Check { Version = new Version(2, 5, 1, 0) }.HasPresets);
            Assert.Equal("No file is chosen.", DlssDll.Describe(DlssDll.Inspect("")));
        }

        [Fact]
        public void TheDownloadIsReadyOnlyAtItsSize()
        {
            using (var t = new TempDir())
            {
                var path = DlssDownloads.PathFor(t.Combine("dlss"), Newest);
                Assert.False(DlssDownloads.HasSize(path, Newest));
                Directory.CreateDirectory(Path.GetDirectoryName(path));
                File.WriteAllBytes(path, new byte[999]);
                Assert.False(DlssDownloads.HasSize(path, Newest));
                File.WriteAllBytes(path, new byte[1000]);
                Assert.True(DlssDownloads.HasSize(path, Newest));
                // The size alone is not the file: the launch checks its SHA-256 too.
                Assert.False(DlssDownloads.Matches(path, Newest));
            }
        }
    }
}
