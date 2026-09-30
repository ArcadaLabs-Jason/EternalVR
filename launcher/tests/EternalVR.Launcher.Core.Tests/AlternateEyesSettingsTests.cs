using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>The Advanced tab's "Alternate eyes" option: its key, its variable, its rule and its words.</summary>
    public class AlternateEyesSettingsTests
    {
        private static Dictionary<string, string> Env(LauncherSettings s) =>
            LaunchPlanBuilder.Build(new LaunchInputs
            {
                GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
                LayerDir = @"E:\EternalVR\layer",
                Settings = s,
                ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
            }).Environment.ToDictionary(e => e.Key, e => e.Value);

        [Fact]
        public void OffByDefaultAndPassedExplicitly()
        {
            Assert.Equal(AlternateEyesMode.Off, new LauncherSettings().AlternateEyes);
            Assert.Equal("0", Env(new LauncherSettings())["ETERNALVR_ALTERNATE_EYES"]);
        }

        [Fact]
        public void OnAndAutoInStereoOnly()
        {
            Assert.Equal("1", Env(new LauncherSettings { AlternateEyes = AlternateEyesMode.On })["ETERNALVR_ALTERNATE_EYES"]);
            Assert.Equal("auto", Env(new LauncherSettings { AlternateEyes = AlternateEyesMode.Auto })["ETERNALVR_ALTERNATE_EYES"]);
            Assert.Equal("0", Env(new LauncherSettings { AlternateEyes = AlternateEyesMode.On, Mode = VrMode.Mono })["ETERNALVR_ALTERNATE_EYES"]);
            Assert.Equal("0", Env(new LauncherSettings { AlternateEyes = AlternateEyesMode.Auto, Mode = VrMode.Mono })["ETERNALVR_ALTERNATE_EYES"]);
        }

        [Fact]
        public void StoredLikeTheOtherSettings()
        {
            var on = new LauncherSettings { AlternateEyes = AlternateEyesMode.On };
            Assert.Contains("alternate_eyes = 1", on.Serialize());
            Assert.Equal(AlternateEyesMode.On, LauncherSettings.Parse(on.Serialize()).AlternateEyes);
            var auto = new LauncherSettings { AlternateEyes = AlternateEyesMode.Auto };
            Assert.Contains("alternate_eyes = auto", auto.Serialize());
            Assert.Equal(AlternateEyesMode.Auto, LauncherSettings.Parse(auto.Serialize()).AlternateEyes);
            Assert.Contains("alternate_eyes = 0", new LauncherSettings().Serialize());
            Assert.Equal(AlternateEyesMode.Off, LauncherSettings.Parse(new LauncherSettings().Serialize()).AlternateEyes);
            // A file without the key (an older launcher's), or with a value it does not know, keeps it off.
            Assert.Equal(AlternateEyesMode.Off, LauncherSettings.Parse("schema_version = 2\nmode = stereo\n").AlternateEyes);
            Assert.Equal(AlternateEyesMode.Off, LauncherSettings.Parse("schema_version = 2\nalternate_eyes = maybe\n").AlternateEyes);
            Assert.Equal(AlternateEyesMode.On, LauncherSettings.Parse("schema_version = 2\nalternate_eyes = TRUE\n").AlternateEyes);
            Assert.Equal(AlternateEyesMode.Auto, LauncherSettings.Parse("schema_version = 2\nalternate_eyes = AUTO\n").AlternateEyes);
            // A known key: not kept as an unknown one.
            Assert.Empty(LauncherSettings.Parse("schema_version = 2\nalternate_eyes = auto\n").UnknownKeys);
        }

        [Fact]
        public void ResetAndProfilesTurnItOffOrKeepIt()
        {
            Assert.Equal(AlternateEyesMode.Off, new LauncherSettings { AlternateEyes = AlternateEyesMode.Auto }.WithDefaults().AlternateEyes);
            Assert.Equal(AlternateEyesMode.Auto, new LauncherSettings { AlternateEyes = AlternateEyesMode.Auto }.Clone().AlternateEyes);
        }

        [Fact]
        public void OnlyAppliesInStereo()
        {
            Assert.Null(SettingRules.WhyNot(Setting.AlternateEyes, new LauncherSettings()));
            Assert.Equal(SettingRules.NeedsStereo, SettingRules.WhyNot(Setting.AlternateEyes, new LauncherSettings { Mode = VrMode.Mono }));
        }

        [Fact]
        public void ItsWordsSayWhatItCosts()
        {
            var text = SettingTexts.For(Setting.AlternateEyes);
            Assert.Equal("Alternate eyes", text.Label);
            // In the order of AlternateEyesMode: the combo box's index is the mode.
            Assert.Equal(new[] { "Off", "Auto (when needed)", "On (for slower processors)" }, text.Choices);
            Assert.Equal("Auto (when needed)", text.Choices[(int)AlternateEyesMode.Auto]);
            Assert.Equal("On (for slower processors)", text.Choices[(int)AlternateEyesMode.On]);
            Assert.Contains("half the rate", text.Tooltip);
            Assert.Contains("doubled", text.Tooltip);
            Assert.Contains("only when your processor cannot keep up", text.Tooltip);
            // Plain words: no em or en dashes.
            Assert.DoesNotContain("\u2014", text.Tooltip);
            Assert.DoesNotContain("\u2013", text.Tooltip);
        }
    }
}
