using System.Collections.Generic;
using System.Collections.Specialized;
using System.Diagnostics;
using System.Linq;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    public class ChildEnvironmentTests
    {
        private static KeyValuePair<string, string> Kv(string k, string v) => new KeyValuePair<string, string>(k, v);

        [Fact]
        public void NamesThatDifferOnlyInCaseAreFound()
        {
            var groups = ChildEnvironment.CaseDuplicates(new[] { "TMP", "Path", "tmp", "TEMP", "PATH", "Tmp" });
            Assert.Equal(2, groups.Count);
            Assert.Equal(new[] { "TMP", "tmp", "Tmp" }, groups[0]);
            Assert.Equal(new[] { "Path", "PATH" }, groups[1]);
            Assert.Empty(ChildEnvironment.CaseDuplicates(new[] { "A", "B" }));
        }

        [Fact]
        public void TheMergeKeepsTheFirstOfNamesThatDifferOnlyInCase()
        {
            // As Git Bash hands it over: both spellings, the Windows one first.
            var inherited = new[] { Kv("TMP", @"C:\Temp"), Kv("tmp", "/tmp"), Kv("Path", @"C:\Windows"), Kv("PATH", "/usr/bin") };
            var env = ChildEnvironment.Merge(inherited, new[] { Kv("SteamAppId", "782330") });
            Assert.Equal(3, env.Count);
            Assert.Equal(@"C:\Temp", env["tmp"]);
            Assert.Equal(@"C:\Windows", env["PATH"]);
            Assert.Equal("782330", env["SteamAppId"]);
        }

        [Fact]
        public void OfNamesThatDifferOnlyInCaseTheOneWindowsReadsStays()
        {
            // The hashtable .NET builds from the block has no order: Windows' own lookup decides.
            var inherited = new[] { Kv("tmp", "/tmp"), Kv("TMP", @"C:\Temp"), Kv("Other", "x") };
            var asked = new List<string>();
            var env = ChildEnvironment.Merge(inherited, null, name => { asked.Add(name); return @"C:\Temp"; });
            Assert.Equal(2, env.Count);
            Assert.Equal(@"C:\Temp", env["TMP"]);
            Assert.Equal("TMP", env.Keys.Single(k => k.Equals("tmp", System.StringComparison.OrdinalIgnoreCase)));
            Assert.Equal(new[] { "tmp" }, asked); // only names with a twin are looked up
            // A lookup that matches neither keeps the first.
            Assert.Equal("/tmp", ChildEnvironment.Merge(inherited, null, _ => null)["TMP"]);
        }

        [Fact]
        public void TheStartInfosDictionaryIsTakenEvenWhenItsFirstUseThrows()
        {
            // .NET Framework: the first use throws "Item has already been added" with the dictionary half filled.
            var half = new StringDictionary { ["TMP"] = "partial" };
            int uses = 0;
            var got = ChildEnvironment.Open(() =>
            {
                if (uses++ == 0) throw new System.ArgumentException("Item has already been added. Key in dictionary: 'TMP'  Key being added: 'tmp'");
                return half;
            });
            Assert.Same(half, got);
            Assert.Equal(2, uses);

            ChildEnvironment.Fill(got, ChildEnvironment.Merge(new[] { Kv("TMP", @"C:\Temp"), Kv("tmp", "/tmp") }, new[] { Kv("EVR_X", "1") }));
            Assert.Equal(2, got.Count);
            Assert.Equal(@"C:\Temp", got["TMP"]);
            Assert.Equal("1", got["EVR_X"]);
        }

        [Fact]
        public void ApplySetsTheMergedCopyOnTheStartInfoOnly()
        {
            var psi = new ProcessStartInfo("x.exe");
            var path = System.Environment.GetEnvironmentVariable("PATH");
            ChildEnvironment.Apply(psi, new[] { Kv("EVR_TEST_APPLY", "1"), Kv("PATH", null) });
            Assert.Equal("1", psi.EnvironmentVariables["EVR_TEST_APPLY"]);
            Assert.False(psi.EnvironmentVariables.ContainsKey("PATH"));
            // An inherited variable is kept: TEMP on Windows, HOME elsewhere (a Linux shell sets no TEMP).
            var inherited = System.OperatingSystem.IsWindows() ? "TEMP" : "HOME";
            Assert.NotNull(System.Environment.GetEnvironmentVariable(inherited));
            Assert.Equal(System.Environment.GetEnvironmentVariable(inherited), psi.EnvironmentVariables[inherited]);
            // The launcher's own environment is untouched.
            Assert.Null(System.Environment.GetEnvironmentVariable("EVR_TEST_APPLY"));
            Assert.Equal(path, System.Environment.GetEnvironmentVariable("PATH"));
        }

        [Fact]
        public void TheLaunchsVariablesWinAndANullRemoves()
        {
            var inherited = new[] { Kv("xr_runtime_json", @"C:\old.json"), Kv("ETERNALVR_DISABLE_LAYER", "1"), Kv("KEEP", "x") };
            var env = ChildEnvironment.Merge(inherited, new[] { Kv("XR_RUNTIME_JSON", @"C:\chosen.json"), Kv("ETERNALVR_DISABLE_LAYER", null) });
            Assert.Equal(@"C:\chosen.json", env["XR_RUNTIME_JSON"]);
            Assert.Equal("XR_RUNTIME_JSON", env.Keys.Single(k => k.StartsWith("XR", System.StringComparison.OrdinalIgnoreCase)));
            Assert.False(env.ContainsKey("ETERNALVR_DISABLE_LAYER"));
            Assert.Equal("x", env["KEEP"]);
        }

        [Fact]
        public void TheLoadersAndTheLayersInheritedVariablesAreListed()
        {
            var inherited = new[]
            {
                Kv("PATH", @"C:\Windows"), Kv("XR_RUNTIME_JSON", @"C:\vd.json"), Kv("VK_LOADER_LAYERS_DISABLE", "*"),
                Kv("ETERNALVR_STEREO_EXPERIMENT", "1"), Kv("ETERNALVR_MODE", "mono"), Kv("vk_instance_layers", "VK_LAYER_x"),
            };
            var planned = new[] { Kv("ETERNALVR_MODE", "stereo"), Kv("SteamAppId", "782330") };
            var listed = ChildEnvironment.Inherited(inherited, planned);
            // The plan's own ETERNALVR_MODE replaces the inherited one, so it is not listed; PATH is not the loaders'.
            Assert.Equal(new[] { "ETERNALVR_STEREO_EXPERIMENT", "vk_instance_layers", "VK_LOADER_LAYERS_DISABLE", "XR_RUNTIME_JSON" }, listed.Select(kv => kv.Key));
        }

        [Fact]
        public void TheLaunchPlanLogsWhatTheGameInherits()
        {
            var p = LaunchPlanBuilder.Build(new LaunchInputs { GameRoot = @"E:\DOOMEternal", LayerDir = @"E:\EternalVR\layer" });
            p.Inherited = new[] { Kv("XR_RUNTIME_JSON", @"C:\vd.json") };
            Assert.Contains(@"inherit: XR_RUNTIME_JSON=C:\vd.json", p.Describe());
        }

        [Theory]
        [InlineData(null, null, @"C:\active.json", @"C:\active.json")]
        [InlineData("system", @"C:\env.json", @"C:\active.json", @"C:\env.json")]
        [InlineData("", "  ", @"C:\active.json", @"C:\active.json")]
        [InlineData(@"C:\chosen.json", @"C:\env.json", @"C:\active.json", @"C:\chosen.json")]
        public void TheSystemDefaultIsWhatTheOpenXrLoaderPicks(string chosen, string inherited, string active, string expected)
        {
            Assert.Equal(expected, LaunchPlanBuilder.EffectiveRuntime(chosen, inherited, active));
            Assert.Equal(LaunchPlanBuilder.IsSystemRuntime(chosen) && !string.IsNullOrWhiteSpace(inherited),
                LaunchPlanBuilder.RuntimeFromEnvironment(chosen, inherited));
        }

        [Fact]
        public void TheSystemDefaultSetsNoRuntimeVariable()
        {
            // The game keeps the XR_RUNTIME_JSON it inherits, the one the window, preflight and the report name.
            var env = LaunchPlanBuilder.OpenXrEnvironment(new LauncherSettings { Runtime = LauncherSettings.SystemRuntime }, null);
            Assert.DoesNotContain(env, kv => kv.Key == LaunchPlanBuilder.RuntimeVariable);
            var chosen = LaunchPlanBuilder.OpenXrEnvironment(new LauncherSettings { Runtime = @"C:\chosen.json" }, null);
            Assert.Contains(Kv(LaunchPlanBuilder.RuntimeVariable, @"C:\chosen.json"), chosen);
        }
    }
}
