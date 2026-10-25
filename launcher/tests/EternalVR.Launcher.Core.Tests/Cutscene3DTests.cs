using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>"Stereoscopic cutscenes": the flat cutscene screen shown as a stereo pair; on by default (T-164).</summary>
    public class Cutscene3DTests
    {
        private static LaunchInputs Inputs(LauncherSettings s = null) => new LaunchInputs
        {
            GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
            LayerDir = @"E:\EternalVR\layer",
            LogDir = @"E:\data\logs\20261007-120000",
            Settings = s ?? new LauncherSettings(),
            ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
        };

        private static Dictionary<string, string> Env(LaunchPlan p) => p.Environment.ToDictionary(e => e.Key, e => e.Value);

        [Fact]
        public void OnByDefaultAndPassedExplicitly()
        {
            Assert.True(new LauncherSettings().Cutscene3D);
            Assert.True(SettingsPresets.Recommended.Values().Cutscene3D);
            Assert.Equal("1", Env(LaunchPlanBuilder.Build(Inputs()))["ETERNALVR_CUTSCENE_3D"]);
            Assert.Equal("0", Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { Cutscene3D = false })))["ETERNALVR_CUTSCENE_3D"]);
        }

        [Fact]
        public void KeptInTheIniAndUnknownToNoOne()
        {
            Assert.True(LauncherSettings.Parse(new LauncherSettings { Cutscene3D = true }.Serialize()).Cutscene3D);
            Assert.Contains("cutscene_3d = 1", new LauncherSettings { Cutscene3D = true }.Serialize());
            Assert.Contains("cutscene_3d = 1", new LauncherSettings().Serialize());
            Assert.False(LauncherSettings.Parse(new LauncherSettings { Cutscene3D = false }.Serialize()).Cutscene3D);
            // A file without the key (0.1.82 and older) takes the default: on.
            Assert.True(LauncherSettings.Parse("schema_version = 2\ncutscene_view = cinema\n").Cutscene3D);
            Assert.Empty(LauncherSettings.Parse("schema_version = 2\ncutscene_3d = 1\n").UnknownKeys);
        }

        [Fact]
        public void AppliesOnlyToTheFlatScreenInStereoWithBothEyesFromOneFrame()
        {
            Assert.Null(SettingRules.WhyNot(Setting.Cutscene3D, new LauncherSettings()));
            Assert.Equal(SettingRules.NeedsStereo, SettingRules.WhyNot(Setting.Cutscene3D, new LauncherSettings { Mode = VrMode.Mono }));
            Assert.Equal(SettingRules.NeedsCinema, SettingRules.WhyNot(Setting.Cutscene3D, new LauncherSettings { Cutscenes = CutsceneView.Immersive }));
            Assert.Equal(SettingRules.NotWithAlternateEyes, SettingRules.WhyNot(Setting.Cutscene3D, new LauncherSettings { AlternateEyes = AlternateEyesMode.On }));
            Assert.Equal(SettingRules.NotWithAlternateEyes, SettingRules.WhyNot(Setting.Cutscene3D, new LauncherSettings { AlternateEyes = AlternateEyesMode.Auto }));
            Assert.Equal(SettingRules.NotWithParallelEyes, SettingRules.WhyNot(Setting.Cutscene3D, new LauncherSettings { ParallelEyes = true }));
        }

        [Fact]
        public void AnOlderIniWithAProfileStaysTrustedAndTheSettingIsOn()
        {
            // launcher.ini as a launcher without the key wrote it: a profile, one change not saved into it, and the check
            // value of exactly those lines. Reading it with this launcher must not count as an edit by hand.
            var older = "schema_version = 2\nprofile = Riley\npreset = recommended\ncutscene_view = cinema\nturn = snap\nprofile_unsaved = turn\n";
            var check = ProfileEdits.Fingerprint(LauncherSettings.ReadMap(older));
            var ini = LauncherSettings.Parse(older + "profile_check = " + check + "\n");
            Assert.True(ProfileEdits.ListHolds(ini));
            Assert.True(ini.Cutscene3D);
            // Positive control: a line edited by hand after the check was made does break it.
            var edited = LauncherSettings.Parse(older.Replace("turn = snap", "turn = smooth") + "profile_check = " + check + "\n");
            Assert.False(ProfileEdits.ListHolds(edited));
        }

        [Fact]
        public void HasItsText()
        {
            var text = SettingTexts.For(Setting.Cutscene3D);
            Assert.Equal("Stereoscopic cutscenes", text.Label);
            Assert.DoesNotContain("\u2014", text.Tooltip);
        }
    }
}
