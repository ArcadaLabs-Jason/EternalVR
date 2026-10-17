using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>
    /// "Use the headset's graphics card" on the Advanced tab (<see cref="LauncherSettings.AdapterSteer"/>): its key, default,
    /// variable, profile handling, and the rule for a variable the player set by hand.
    /// </summary>
    public class AdapterSteerTests
    {
        private const string Variable = "ETERNALVR_ADAPTER_STEER";

        private static LaunchPlan Plan(LauncherSettings s) =>
            LaunchPlanBuilder.Build(new LaunchInputs
            {
                GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
                LayerDir = @"E:\EternalVR\layer",
                Settings = s,
                ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
                ParallelEyesGame = true,
            });

        private static Dictionary<string, string> Env(LauncherSettings s) => Plan(s).Environment.ToDictionary(e => e.Key, e => e.Value);

        private static KeyValuePair<string, string> Kv(string k, string v) => new KeyValuePair<string, string>(k, v);

        private static string[] Lines(string text) => text.Replace("\r\n", "\n").Split('\n');

        [Fact]
        public void OffByDefaultAndThenItsVariableIsNotSet()
        {
            Assert.False(new LauncherSettings().AdapterSteer);
            Assert.Contains("adapter_steer = 0", Lines(new LauncherSettings().Serialize()));
            // An older launcher's file has no such line: off.
            Assert.False(LauncherSettings.Parse("schema_version = 2\nturn = snap\n").AdapterSteer);
            Assert.False(Env(new LauncherSettings()).ContainsKey(Variable));
            Assert.DoesNotContain(Variable + "=", Plan(new LauncherSettings()).Describe());
        }

        [Fact]
        public void TheSettingsFileKeepsIt()
        {
            var s = LauncherSettings.Parse("schema_version = 2\nadapter_steer = 1\n");
            Assert.True(s.AdapterSteer);
            // A key this launcher knows: not kept as an unknown one (it would be written twice).
            Assert.DoesNotContain(s.UnknownKeys, kv => kv.Key == "adapter_steer");
            var text = s.Serialize();
            Assert.Single(Lines(text), line => line.StartsWith("adapter_steer ", StringComparison.Ordinal));
            Assert.Contains("adapter_steer = 1", Lines(text));
            Assert.True(LauncherSettings.Parse(text).AdapterSteer);
            Assert.True(LauncherSettings.Parse("schema_version = 2\nadapter_steer = on\n").AdapterSteer);
            Assert.False(LauncherSettings.Parse("schema_version = 2\nadapter_steer = 0\n").AdapterSteer);
            Assert.False(LauncherSettings.Parse("schema_version = 2\nadapter_steer = maybe\n").AdapterSteer);
        }

        [Fact]
        public void OnItSetsItsVariableInMonoAndStereoAndThePlanLogNamesIt()
        {
            foreach (var mode in new[] { VrMode.Stereo, VrMode.Mono })
            {
                var s = new LauncherSettings { AdapterSteer = true, Mode = mode };
                var plan = Plan(s);
                Assert.Equal("1", plan.Environment.Single(kv => kv.Key == Variable).Value);
                Assert.Contains("env:     " + Variable + "=1", plan.Describe());
                // Only its own variable changes.
                var before = Env(new LauncherSettings { Mode = mode });
                Assert.Equal(before.Keys.Concat(new[] { Variable }).OrderBy(k => k), plan.Environment.Select(e => e.Key).OrderBy(k => k));
                Assert.Null(SettingRules.WhyNot(Setting.AdapterSteer, s));
            }
        }

        [Fact]
        public void TheLayerReadsTheVariable()
        {
            Assert.Contains("L\"" + Variable + "\"", File.ReadAllText(RepoFile("src", "vkcore", "adapter_steer.cpp")));
        }

        [Fact]
        public void AVariableSetByHandStillReachesTheGameWhileTheOptionIsOff()
        {
            // The layer's switches without a launcher option (the sprite effects fix, the wall-climb view) and this one while off.
            var inherited = new[] { Kv("PATH", @"C:\Windows"), Kv("ETERNALVR_STEREO_FX_SYNC", "1"), Kv(Variable, "1") };
            var plan = Plan(new LauncherSettings());
            var game = ChildEnvironment.Merge(inherited, plan.Environment);
            Assert.Equal("1", game["ETERNALVR_STEREO_FX_SYNC"]);
            Assert.Equal("1", game[Variable]);
            // The plan's log lists them as inherited, and nothing as replaced.
            plan.Inherited = ChildEnvironment.Inherited(inherited, plan.Environment);
            plan.Replaced = ChildEnvironment.Replaced(inherited, plan.Environment);
            Assert.Contains("inherit: ETERNALVR_STEREO_FX_SYNC=1", plan.Describe());
            Assert.Contains("inherit: " + Variable + "=1", plan.Describe());
            Assert.Empty(plan.Replaced);
        }

        [Fact]
        public void AValueSetByHandThatASettingReplacesIsLogged()
        {
            var inherited = new[] { Kv(Variable, "0"), Kv("ETERNALVR_UI_WASH", "0"), Kv("ETERNALVR_TURN", "snap"), Kv("PATH", "x"),
                Kv("SteamAppId", "1") };
            var plan = Plan(new LauncherSettings { AdapterSteer = true });
            plan.Inherited = ChildEnvironment.Inherited(inherited, plan.Environment);
            plan.Replaced = ChildEnvironment.Replaced(inherited, plan.Environment);
            // The option wins over the hand-set value, and the log says which value it replaced (so does any other setting's
            // variable, here the turning); a variable the launch does not set stays inherited.
            Assert.Equal("1", ChildEnvironment.Merge(inherited, plan.Environment)[Variable]);
            Assert.Equal(new[] { Variable, "ETERNALVR_TURN" }, plan.Replaced.Select(kv => kv.Key));
            Assert.Equal("0", plan.Replaced[0].Value);
            var log = plan.Describe();
            Assert.Contains("replace: " + Variable + "=0", log);
            Assert.Contains("replace: ETERNALVR_TURN=snap", log);
            Assert.Contains("inherit: ETERNALVR_UI_WASH=0", log);
            // Only the layer's and the loaders' variables: SteamAppId, which every launch sets, is not named.
            Assert.DoesNotContain(" PATH=", log);
            Assert.DoesNotContain("SteamAppId=1", log);
            // The same value set by hand is no replacement.
            Assert.Empty(ChildEnvironment.Replaced(new[] { Kv(Variable, "1") }, plan.Environment));
        }

        [Fact]
        public void TheGraphicsCardIsThisMachinesAndNeverInAProfile()
        {
            var recommended = SettingsPresets.Recommended.Values();
            var s = new LauncherSettings { AdapterSteer = true, Profile = "Riley" };
            Assert.Contains("adapter_steer", SettingKeys.Machine);
            Assert.Empty(SettingKeys.Of(Setting.AdapterSteer));
            Assert.Empty(ProfileEdits.Changed(s, recommended));
            Assert.DoesNotContain("adapter_steer", ProfileEdits.ProfileText(s));
            // Reset to Recommended and the defaults keep it, as they keep the runtime.
            Assert.True(ProfileEdits.ResetAll(s, recommended).AdapterSteer);
            Assert.True(s.WithDefaults().AdapterSteer);
            using (var dir = new TempDir())
            {
                var store = new ProfileStore(dir.Path);
                store.Save("Riley", s);
                Assert.DoesNotContain("adapter_steer", File.ReadAllText(dir.Combine("Riley.ini")));
                // Switching to a profile keeps this PC's choice, either way.
                Assert.True(store.Load("Riley", new LauncherSettings { AdapterSteer = true }).AdapterSteer);
                Assert.False(store.Load("Riley", new LauncherSettings()).AdapterSteer);
                // launcher.ini's value is the one in use at start.
                var ini = new LauncherSettings { Profile = "Riley", AdapterSteer = true };
                ProfileEdits.MarkUnsaved(ini, store.Load("Riley", ini), null);
                var read = LauncherSettings.Parse(ini.Serialize());
                Assert.True(ProfileEdits.ListHolds(read));
                Assert.True(ProfileEdits.InUse(read, store.Load("Riley", new LauncherSettings())).AdapterSteer);
            }
        }

        [Fact]
        public void ItIsAnAdvancedTabRowThatSaysItIsOffByDefault()
        {
            // The Advanced tab's rows follow VR mode in the window order (SettingRules.Setting).
            Assert.True(Setting.AdapterSteer > Setting.VrMode);
            Assert.Contains("Off by default", SettingTexts.For(Setting.AdapterSteer).Tooltip);
        }

        private static string RepoFile(params string[] parts)
        {
            for (var dir = new DirectoryInfo(AppContext.BaseDirectory); dir != null; dir = dir.Parent)
            {
                var path = Path.Combine(new[] { dir.FullName }.Concat(parts).ToArray());
                if (File.Exists(path)) return path;
            }
            throw new FileNotFoundException("not found above the test folder: " + string.Join("/", parts));
        }
    }
}
