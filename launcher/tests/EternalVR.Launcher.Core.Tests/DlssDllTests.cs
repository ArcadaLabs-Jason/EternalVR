using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>A newer DLSS DLL of the player's own: the settings, the layer's variables, the rules and the file check.</summary>
    public class DlssDllTests
    {
        private const string Chosen = @"E:\Mods\DLSS 310\nvngx_dlss.dll";

        private static Dictionary<string, string> Env(LauncherSettings s) =>
            LaunchPlanBuilder.Build(new LaunchInputs
            {
                GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
                LayerDir = @"E:\EternalVR\layer",
                Settings = s,
                ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
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
        public void TheGamesDllIsTheDefaultAndSendsNothing()
        {
            var s = new LauncherSettings();
            Assert.Equal(DlssDllChoice.Game, s.DlssDll);
            Assert.Equal("default", s.DlssPreset);
            s.AntiAliasing = AntiAliasingMode.Dlss;
            var env = Env(s);
            Assert.False(env.ContainsKey("ETERNALVR_DLSS_DLL"));
            Assert.False(env.ContainsKey("ETERNALVR_DLSS_PRESET"));
            // A path kept from before, with the game's chosen: still nothing.
            s.DlssDllPath = Chosen;
            Assert.False(Env(s).ContainsKey("ETERNALVR_DLSS_DLL"));
        }

        [Fact]
        public void AChosenFileReachesTheLayerOnlyWithDlssInStereo()
        {
            var env = Env(WithFile());
            Assert.Equal(Chosen, env["ETERNALVR_DLSS_DLL"]);
            Assert.Equal("K", env["ETERNALVR_DLSS_PRESET"]);
            Assert.Equal("default", Env(WithFile("default"))["ETERNALVR_DLSS_PRESET"]);
            // An unknown preset in the file: the DLL's own.
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
            var back = LauncherSettings.Parse(WithFile("j").Serialize());
            Assert.Equal(DlssDllChoice.File, back.DlssDll);
            Assert.Equal(Chosen, back.DlssDllPath);
            Assert.Equal("J", back.DlssPreset);
            Assert.Empty(back.UnknownKeys);
            // A file from before the choice existed: the game's DLL, the DLL's own preset.
            var old = LauncherSettings.Parse("schema_version = 2\nanti_aliasing = dlss\n");
            Assert.Equal(DlssDllChoice.Game, old.DlssDll);
            Assert.Equal(string.Empty, old.DlssDllPath);
            Assert.Equal("default", old.DlssPreset);
            Assert.Equal(DlssDllChoice.Game, LauncherSettings.Parse("schema_version = 2\ndlss_dll = newest\n").DlssDll);
            Assert.Equal("default", LauncherSettings.Parse("schema_version = 2\ndlss_preset = H\n").DlssPreset);
        }

        [Fact]
        public void ResetAndProfilesKeepThisMachinesFile()
        {
            var reset = WithFile().WithDefaults();
            Assert.Equal(Chosen, reset.DlssDllPath);
            Assert.Equal(DlssDllChoice.Game, reset.DlssDll);
            Assert.Equal("default", reset.DlssPreset);

            using (var t = new TempDir())
            {
                var store = new ProfileStore(t.Combine("profiles"));
                store.Save("Jason", WithFile());
                Assert.DoesNotContain(Chosen, File.ReadAllText(t.Combine("profiles", "Jason.ini")));
                var current = new LauncherSettings { DlssDllPath = @"F:\Other\nvngx_dlss.dll" };
                var loaded = store.Load("Jason", current);
                Assert.Equal(DlssDllChoice.File, loaded.DlssDll);
                Assert.Equal("K", loaded.DlssPreset);
                Assert.Equal(@"F:\Other\nvngx_dlss.dll", loaded.DlssDllPath);
            }
        }

        [Fact]
        public void TheRowsNeedDlssAndThePresetNeedsAFile()
        {
            Assert.Null(SettingRules.WhyNot(Setting.DlssVersion, WithFile()));
            Assert.Null(SettingRules.WhyNot(Setting.DlssPreset, WithFile()));
            var game = WithFile();
            game.DlssDll = DlssDllChoice.Game;
            Assert.Null(SettingRules.WhyNot(Setting.DlssVersion, game));
            Assert.Equal(SettingRules.NeedsDlssFile, SettingRules.WhyNot(Setting.DlssPreset, game));
            var taa = WithFile();
            taa.AntiAliasing = AntiAliasingMode.Taa;
            Assert.Equal(SettingRules.NeedsDlss, SettingRules.WhyNot(Setting.DlssVersion, taa));
            Assert.Equal(SettingRules.NeedsDlss, SettingRules.WhyNot(Setting.DlssPreset, taa));
            var mono = WithFile();
            mono.Mode = VrMode.Mono;
            Assert.Equal(SettingRules.NeedsStereo, SettingRules.WhyNot(Setting.DlssVersion, mono));
            // The window's lists: one choice per value.
            Assert.Equal(Enum.GetValues(typeof(DlssDllChoice)).Length, SettingTexts.For(Setting.DlssVersion).Choices.Count);
            Assert.Equal(DlssDll.PresetValues.Length, SettingTexts.For(Setting.DlssPreset).Choices.Count);
            Assert.Equal(1, DlssDll.PresetIndex("k"));
            Assert.Equal(0, DlssDll.PresetIndex("transformer"));
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
    }
}
