using System;
using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Data;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>The CPU Saver: its data file, the launch plan's variable, the settings and the restore.</summary>
    public class CpuSaverTests
    {
        private static CpuSaver Shipped => CpuSaver.Parse(TestData.Read("cpu-saver.txt"));

        private static LaunchInputs Inputs(LauncherSettings s, bool forceCvars = true, CpuSaver saver = null) => new LaunchInputs
        {
            GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
            LayerDir = @"E:\EternalVR\layer",
            LogDir = @"E:\data\logs\20260928-010203",
            Settings = s,
            ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
            CpuSaver = saver ?? Shipped,
            ForceCvars = forceCvars,
        };

        private static Dictionary<string, string> Env(LaunchInputs inputs) =>
            LaunchPlanBuilder.Build(inputs).Environment.ToDictionary(e => e.Key, e => e.Value);

        private static LauncherSettings With(params (string Id, bool On)[] choices)
        {
            var s = new LauncherSettings();
            s.SetCpuSaverChoices(choices.Select(c => new KeyValuePair<string, bool>(c.Id, c.On)));
            return s;
        }

        private const string Sample =
            "# comment\n"
            + "item | stream | on | streaming | Only what you see | Mostly lossless\ntip | Loads less.\nis_a | 0\n\n"
            + "item | shadow | off | saver | No own shadow\ntip | First half,\ntip | second half.\nr_a | 1\n"
            + "item | fades | off | saver | Fade nearer\ntip | Caps.\nr_b | 1,1,1,2,2\nr_c | <=0.8\n";

        [Fact]
        public void TheShippedItemsAreFewAndLeaveTheForcedCvarsAlone()
        {
            var saver = Shipped;
            Assert.NotEmpty(saver.Items);
            // A preset, not a second config: a handful of items, each of a cvar or two.
            Assert.True(saver.Items.Count <= 8, "a preset, not a second config");
            Assert.True(saver.All.Count <= 12, "a preset, not a second config");
            Assert.All(saver.Items, i => Assert.InRange(i.Cvars.Count, 1, 3));
            var forced = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")).Names.ToList();
            var session = SessionKeys.Parse(TestData.Read("session-keys.txt")).Names.ToList();
            foreach (var name in saver.Names)
            {
                // The layer holds the forced cvars itself (and the stereo set wins); the preset must not fight them.
                Assert.DoesNotContain(forced, n => string.Equals(n, name, StringComparison.OrdinalIgnoreCase));
                Assert.DoesNotContain(session, n => string.Equals(n, name, StringComparison.OrdinalIgnoreCase));
                // Never a multiplayer or network cvar.
                Assert.False(name.StartsWith("net_", StringComparison.OrdinalIgnoreCase) || name.StartsWith("mp_", StringComparison.OrdinalIgnoreCase), name);
            }
            foreach (var item in saver.Items)
            {
                Assert.False(string.IsNullOrWhiteSpace(item.Label), item.Id);
                Assert.True(item.Tooltip.Length > 40, item.Id);
                Assert.DoesNotContain("\u2014", item.Tooltip);
            }
        }

        [Fact]
        public void TheShippedItems()
        {
            var saver = Shipped;
            Assert.Equal(new[] { "texture_streaming", "own_shadow", "near_sun_shadows", "model_detail", "decal_distance", "distant_shadows_lights" },
                saver.Items.Select(i => i.Id));
            // Texture streaming: its own row, on by default; the CPU Saver's items: off by default.
            var streaming = saver.Items[0];
            Assert.True(streaming.DefaultOn);
            Assert.Equal(Setting.TextureStreaming, streaming.Row);
            Assert.Equal("is_cacheGreedily=0", string.Join(";", streaming.Cvars.Select(c => c.Name + "=" + c.Value)));
            Assert.Contains("lossless", streaming.Note);
            Assert.All(saver.Items.Skip(1), i => Assert.Equal(string.Empty, i.Note));
            Assert.All(saver.Items.Skip(1), i => Assert.False(i.DefaultOn, i.Id));
            Assert.All(saver.Items.Skip(1), i => Assert.Equal(Setting.CpuSaver, i.Row));
            // Jason's name for the group (2026-09-29): "CPU Saver", not "Processor saver".
            Assert.Equal("CPU Saver (experimental)", SettingTexts.For(Setting.CpuSaver).Label);
            Assert.Equal(new[] { "texture_streaming" }, saver.InRow(Setting.TextureStreaming).Select(i => i.Id));
            // The distant shadows and lights go together, as one item.
            Assert.Equal(new[] { "r_shadowsDistanceFadeMultiplier", "r_lightDistanceFadeMultiplier" }, saver.Items.Last().Cvars.Select(c => c.Name));
            // Every cvar of the measured preset is still there.
            Assert.Equal(new[] { "is_cacheGreedily", "r_skipPlayerShadow", "r_shadowNumAccurateSunSlices", "r_lodScale",
                "r_decalDistanceFadeMultiplier", "r_shadowsDistanceFadeMultiplier", "r_lightDistanceFadeMultiplier" }, saver.Names);
            Assert.Equal("r_lodScale at most 1.625", saver.Items.Single(i => i.Id == "model_detail").CvarText);
        }

        [Fact]
        public void ParseChecksEveryLine()
        {
            var saver = CpuSaver.Parse(Sample);
            Assert.Equal(new[] { "stream", "shadow", "fades" }, saver.Items.Select(i => i.Id));
            Assert.Equal(new[] { "is_a", "r_a", "r_b", "r_c" }, saver.Names);
            Assert.Equal("is_a=0;r_a=1;r_b=1,1,1,2,2;r_c=<=0.8", saver.EnvironmentValueAll);
            Assert.Equal("First half, second half.", saver.Items[1].Tooltip);
            Assert.Equal("No own shadow", saver.Items[1].Label);
            Assert.Equal("Mostly lossless", saver.Items[0].Note);
            Assert.Equal(string.Empty, saver.Items[1].Note);
            Assert.Equal("r_b 1,1,1,2,2, r_c at most 0.8", saver.Items[2].CvarText);
            Assert.Empty(CpuSaver.Parse("# nothing yet\n").Items);
            Assert.Equal(string.Empty, CpuSaver.Parse("# nothing yet\n").EnvironmentValue(new LauncherSettings()));
            const string head = "item | a | off | saver | A\ntip | Tip.\n";
            foreach (var bad in new[]
            {
                // Cvar lines, as before.
                "r_a", "r_a | 1 | stereo", "r_a |", "+r_a | 1", "r a | 1", "r_a=1 | 1", "r_a | 1;r_b=2", "r_a | ?", "r_a | 1\nR_A | 2",
                "r_a | <=", "r_a | <=x", "r_a | <= 1", "r_a | <1", "r_a | <=1;r_b",
            })
                Assert.Throws<FormatException>(() => CpuSaver.Parse(head + bad));
            foreach (var bad in new[]
            {
                "r_a | 1\n" + head + "r_b | 1",                                  // a cvar before the first item
                "tip | Tip.\n" + head + "r_b | 1",                               // a tip before the first item
                head,                                                            // an item without cvars
                "item | a | off | saver | A\nr_a | 1",                           // an item without a tip
                head + "r_a | 1\nitem | a | off | saver | B\ntip | T.\nr_b | 1",   // an id twice
                head + "r_a | 1\nitem | b | off | saver | B\ntip | T.\nR_A | 1",   // a cvar in two items
                "item | A | off | saver | A\ntip | T.\nr_a | 1",                 // ids are lower case
                "item | a-b | off | saver | A\ntip | T.\nr_a | 1",
                "item | 1a | off | saver | A\ntip | T.\nr_a | 1",
                "item | a | yes | saver | A\ntip | T.\nr_a | 1",                 // on or off
                "item | a | off | picture | A\ntip | T.\nr_a | 1",               // a row the window does not have
                "item | a | off | saver |\ntip | T.\nr_a | 1",                   // no label
                "item | a | off | saver\ntip | T.\nr_a | 1",
                "item | a | off | saver | A | B | C\ntip | T.\nr_a | 1",
                "item | a | off | saver | A |\ntip | T.\nr_a | 1",               // an empty note
                "item | a | off | saver | A\ntip | T. | more\nr_a | 1",
            })
                Assert.Throws<FormatException>(() => CpuSaver.Parse(bad));
        }

        [Fact]
        public void OnlyTextureStreamingIsOnByDefault()
        {
            var s = new LauncherSettings();
            var saver = Shipped;
            Assert.Equal(new[] { "texture_streaming" }, saver.Selected(s).Select(i => i.Id));
            Assert.Equal("is_cacheGreedily=0", Env(Inputs(s))[CpuSaver.EnvironmentName]);
            // A launcher.ini without any saver key (every existing player's) gets the same.
            Assert.Equal("is_cacheGreedily=0", Env(Inputs(LauncherSettings.Parse("schema_version = 2\nmode = stereo\n")))[CpuSaver.EnvironmentName]);
            // Reset to defaults: back to texture streaming alone.
            var reset = With(("texture_streaming", false), ("own_shadow", true)).WithDefaults();
            Assert.Equal(new[] { "texture_streaming" }, saver.Selected(reset).Select(i => i.Id));
        }

        [Fact]
        public void TheLayerGetsTheCvarsOfTheItemsThatAreOn()
        {
            Assert.Equal("is_cacheGreedily=0;r_skipPlayerShadow=1", Env(Inputs(With(("own_shadow", true))))[CpuSaver.EnvironmentName]);
            Assert.Equal("r_lodScale=<=1.625;r_shadowsDistanceFadeMultiplier=<=1;r_lightDistanceFadeMultiplier=<=1",
                Env(Inputs(With(("texture_streaming", false), ("distant_shadows_lights", true), ("model_detail", true))))[CpuSaver.EnvironmentName]);
            // Every item on: the whole preset, in file order.
            var all = With(Shipped.Items.Select(i => (i.Id, true)).ToArray());
            Assert.Equal(Shipped.EnvironmentValueAll, Env(Inputs(all))[CpuSaver.EnvironmentName]);
            // Nothing on: no variable at all, and exactly the plan of a launch without the saver.
            var none = With(("texture_streaming", false));
            Assert.False(Env(Inputs(none)).ContainsKey(CpuSaver.EnvironmentName));
            var without = LaunchPlanBuilder.Build(new LaunchInputs
            {
                GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal", LayerDir = @"E:\EternalVR\layer",
                LogDir = @"E:\data\logs\20260928-010203", Settings = none,
                ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
            });
            Assert.Equal(without.Describe(), LaunchPlanBuilder.Build(Inputs(none)).Describe());
            // A choice for an item the data no longer has changes nothing.
            Assert.False(Env(Inputs(With(("texture_streaming", false), ("gone", true)))).ContainsKey(CpuSaver.EnvironmentName));
        }

        [Fact]
        public void StereoOnlyAndOnlyWithASettingsLocation()
        {
            var all = With(Shipped.Items.Select(i => (i.Id, true)).ToArray());
            all.Mode = VrMode.Mono;
            Assert.False(Env(Inputs(all)).ContainsKey(CpuSaver.EnvironmentName));
            Assert.False(Env(Inputs(new LauncherSettings { Mode = VrMode.Mono })).ContainsKey(CpuSaver.EnvironmentName));
            // Without a settings location nothing is forced, and nothing could be restored.
            Assert.False(Env(Inputs(new LauncherSettings(), forceCvars: false)).ContainsKey(CpuSaver.EnvironmentName));
            // Nothing goes on the command line: the layer holds the cvars.
            all.Mode = VrMode.Stereo;
            var args = LaunchPlanBuilder.Build(Inputs(all)).Arguments;
            foreach (var name in Shipped.Names) Assert.DoesNotContain("+" + name, args);
        }

        [Fact]
        public void EachItemHasItsOwnKey()
        {
            var s = With(("texture_streaming", false), ("own_shadow", true));
            var text = s.Serialize();
            Assert.Contains("cpu_saver_texture_streaming = off", text);
            Assert.Contains("cpu_saver_own_shadow = on", text);
            Assert.DoesNotContain("cpu_saver =", text);
            var back = LauncherSettings.Parse(text);
            Assert.False(back.CpuSaverChoice("texture_streaming"));
            Assert.True(back.CpuSaverChoice("own_shadow"));
            Assert.Null(back.CpuSaverChoice("model_detail"));
            Assert.Empty(back.UnknownKeys);
            Assert.Equal(text, back.Serialize());
            // A fresh launcher.ini names no item: each takes its default.
            Assert.DoesNotContain("cpu_saver", new LauncherSettings().Serialize());
            // on/off, 1/0, true/false; anything else is no choice (the default), and the key is not kept.
            var parsed = LauncherSettings.Parse("schema_version = 2\ncpu_saver_a = ON\ncpu_saver_b = 0\ncpu_saver_c = maybe\nCPU_SAVER_D = true\n");
            Assert.Equal(new[] { "a=True", "b=False", "d=True" }, parsed.CpuSaverChoices.Select(kv => kv.Key + "=" + kv.Value));
            Assert.Empty(parsed.UnknownKeys);
            // A key that is no item id stays an unknown key, written back as found.
            Assert.Single(LauncherSettings.Parse("schema_version = 2\ncpu_saver_Bad-Id = on\n").UnknownKeys);
            // Setting again replaces the choice in place; a clone does not share later changes.
            var clone = s.Clone();
            s.SetCpuSaverChoices(new[] { new KeyValuePair<string, bool>("OWN_SHADOW", false) });
            Assert.Equal(new[] { "texture_streaming=False", "own_shadow=False" }, s.CpuSaverChoices.Select(kv => kv.Key + "=" + kv.Value));
            Assert.True(clone.CpuSaverChoice("own_shadow"));
        }

        [Fact]
        public void AnOlderLaunchersSwitchCarriesOver()
        {
            var saver = Shipped;
            // cpu_saver = on: every item on.
            var on = LauncherSettings.Parse("schema_version = 2\ncpu_saver = on\n");
            Assert.True(on.CpuSaverAllOn);
            Assert.Equal(saver.Items.Select(i => i.Id), saver.Selected(on).Select(i => i.Id));
            Assert.Equal(saver.EnvironmentValueAll, Env(Inputs(on))[CpuSaver.EnvironmentName]);
            Assert.True(LauncherSettings.Parse("schema_version = 2\ncpu_saver = 1\n").CpuSaverAllOn);
            // Kept through a save until the window chooses each item, then gone.
            Assert.Contains("cpu_saver = on", on.Serialize());
            Assert.Equal(saver.Items.Select(i => i.Id), saver.Selected(LauncherSettings.Parse(on.Serialize())).Select(i => i.Id));
            on.SetCpuSaverChoices(saver.Items.Select(i => new KeyValuePair<string, bool>(i.Id, i.Id != "decal_distance")));
            Assert.False(on.CpuSaverAllOn);
            Assert.DoesNotContain("cpu_saver = on", on.Serialize());
            Assert.Equal(saver.Items.Count - 1, saver.Selected(LauncherSettings.Parse(on.Serialize())).Count());
            // An item's own key wins over the old switch.
            var mixed = LauncherSettings.Parse("schema_version = 2\ncpu_saver = on\ncpu_saver_texture_streaming = off\n");
            Assert.DoesNotContain(saver.Selected(mixed), i => i.Id == "texture_streaming");
            Assert.Contains(saver.Selected(mixed), i => i.Id == "own_shadow");
            // cpu_saver = off, anything else, or missing: the saver's items off, texture streaming on.
            foreach (var text in new[] { "cpu_saver = off", "cpu_saver = maybe", "" })
            {
                var s = LauncherSettings.Parse("schema_version = 2\n" + text + "\n");
                Assert.False(s.CpuSaverAllOn);
                Assert.Equal(new[] { "texture_streaming" }, saver.Selected(s).Select(i => i.Id));
                Assert.Empty(s.UnknownKeys);
                Assert.DoesNotContain("cpu_saver", s.Serialize());
            }
            var streamingOff = LauncherSettings.Parse("schema_version = 2\ncpu_saver = off\ncpu_saver_texture_streaming = off\n");
            Assert.Empty(saver.Selected(streamingOff));
            // Reset to defaults drops the old switch.
            Assert.False(LauncherSettings.Parse("schema_version = 2\ncpu_saver = on\n").WithDefaults().CpuSaverAllOn);
        }

        [Fact]
        public void AProfileKeepsTheChoices()
        {
            using (var dir = new TempDir())
            {
                var store = new ProfileStore(dir.Path);
                var s = With(("texture_streaming", false), ("near_sun_shadows", true));
                store.Save("Quest", s);
                var loaded = store.Load("Quest", new LauncherSettings());
                Assert.Equal(new[] { "near_sun_shadows" }, Shipped.Selected(loaded).Select(i => i.Id));
                // A profile an older launcher saved with the saver on.
                System.IO.File.WriteAllText(System.IO.Path.Combine(dir.Path, "Old.ini"), "schema_version = 2\ncpu_saver = on\n");
                Assert.Equal(Shipped.Items.Count, Shipped.Selected(store.Load("Old", new LauncherSettings())).Count());
            }
        }

        [Fact]
        public void TheRowsNeedStereo()
        {
            foreach (var row in CpuSaver.Rows.Select(r => r.Value))
            {
                Assert.Equal(SettingRules.NeedsStereo, SettingRules.WhyNot(row, new LauncherSettings { Mode = VrMode.Mono }));
                Assert.Null(SettingRules.WhyNot(row, new LauncherSettings()));
                Assert.NotNull(SettingTexts.For(row));
                Assert.Empty(SettingTexts.For(row).Choices);
            }
            Assert.Contains("lossless", SettingTexts.For(Setting.TextureStreaming).Tooltip);
        }

        [Fact]
        public void TheRestorePutsEveryItemsKeysBack()
        {
            var data = LauncherData.Load(TestData.Dir);
            // Every item's cvars, whether it is on or not.
            foreach (var name in data.CpuSaver.Items.SelectMany(i => i.Cvars).Select(c => c.Name)) Assert.Contains(name, data.RestoredKeys);
            Assert.Contains("is_cacheGreedily", data.RestoredKeys);
            Assert.Contains("r_lightDistanceFadeMultiplier", data.RestoredKeys);
            Assert.Equal(data.RestoredKeys.Count, data.RestoredKeys.Distinct(StringComparer.OrdinalIgnoreCase).Count());
            Assert.Equal(new[] { "a", "b", "c" },
                SessionKeys.RestoredKeys(ForcedCvars.Parse("a | 1"), SessionKeys.Parse("b\n"), new[] { "B", "c" }));
        }
    }
}
