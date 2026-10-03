using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using EternalVR.Launcher.Core.Data;
using EternalVR.Launcher.Core.Game;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Preflight;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    public class LaunchPlanTests
    {
        private static LaunchInputs Inputs(LauncherSettings s = null) => new LaunchInputs
        {
            GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
            LayerDir = @"E:\EternalVR\layer",
            LogDir = @"E:\data\logs\20260926-010203",
            Settings = s ?? new LauncherSettings(),
            ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
        };

        private static Dictionary<string, string> Env(LaunchPlan p) => p.Environment.ToDictionary(e => e.Key, e => e.Value);

        [Fact]
        public void DefaultPlanMatchesTheRigRecipe()
        {
            var p = LaunchPlanBuilder.Build(Inputs());
            Assert.EndsWith(GameLayout.RetailExe, p.ExePath);
            Assert.Equal(@"E:\SteamLibrary\steamapps\common\DOOMEternal", p.WorkingDirectory);
            var env = Env(p);
            Assert.Equal("782330", env["SteamAppId"]);
            Assert.Equal(@"E:\EternalVR\layer", env["VK_ADD_IMPLICIT_LAYER_PATH"]);
            Assert.Equal("1", env["ETERNALVR_ENABLE_LAYER"]);
            Assert.Equal(@"E:\data\logs\20260926-010203", env["ETERNALVR_LOG_DIR"]);
            Assert.Equal("1.00", env["ETERNALVR_WORLD_SCALE"]);
            Assert.Equal("1", env["ETERNALVR_SKIP_CINEMATICS"]);
            Assert.False(env.ContainsKey("XR_RUNTIME_JSON"));
            // The comfort set is the layer's in stereo (ComfortCvarsTests).
            foreach (var arg in new[] { "+com_skipIntroVideo 1", "+com_skipSignInManager 1", "+r_hdrDisplay 0" })
                Assert.Contains(arg, p.CommandLine);
            // The normal game start: the player uses the menus.
            Assert.DoesNotContain("+map", p.CommandLine);
        }

        [Fact]
        public void TheSignInManagerIsSkippedOnlyForTheSteamBuild()
        {
            // The Game Pass build crashed a second after starting with the sign-in manager skipped (2026-10-03).
            var gamePass = Inputs();
            gamePass.Platform = GamePlatform.GamePass;
            var p = LaunchPlanBuilder.Build(gamePass);
            Assert.DoesNotContain("com_skipSignInManager", p.CommandLine);
            Assert.Contains("+com_skipIntroVideo 1", p.CommandLine);
            Assert.Contains("+com_skipSignInManager 1", LaunchPlanBuilder.Build(Inputs()).CommandLine);
        }

        [Fact]
        public void DefaultsAreStereoWithControllersAndHandAim()
        {
            var p = LaunchPlanBuilder.Build(Inputs());
            var env = Env(p);
            Assert.Equal("stereo", env["ETERNALVR_MODE"]);
            Assert.Equal("1", env["ETERNALVR_CONTROLLERS"]);
            Assert.Equal("hand", env["ETERNALVR_AIM"]);
            Assert.Equal("1", env["ETERNALVR_UI_LAYER"]);
            Assert.Equal("panel", env["ETERNALVR_HUD"]);
            // The stereo cvars with per-eye temporal history, TAA by default (docs/VR_STEREO.md), and a small desktop mirror: each eye
            // renders at the headset's size (the render size, docs/rig-findings/render-size.md), not the window's.
            foreach (var arg in new[] { "+r_TAASafeMode 0", "+r_antialiasing 1", "+r_TAAAntiGhosting 0", "+rs_enable 0", "+r_swapInterval 0",
                                        "+r_fullscreen 0", "+r_windowWidth 1280", "+r_windowHeight 720" })
                Assert.Contains(arg, p.CommandLine);
            Assert.Equal("auto", env["ETERNALVR_RENDER_SIZE"]);
            Assert.Equal("1.00", env["ETERNALVR_RENDER_SCALE"]);
            Assert.Equal("0,0,1280,720", env["ETERNALVR_WINDOW"]);
            Assert.Equal("0,0,1280,720", env["ETERNALVR_MIRROR_WINDOW"]);
            Assert.True(p.Window.Mirror);
            Assert.Null(p.Window.Display);
        }

        [Fact]
        public void StereoWindowGoesOnTheDisplayThatHoldsTheEye()
        {
            var i = Inputs(new LauncherSettings { RenderSize = "off" });
            i.Displays = new[]
            {
                new DisplayArea("tv", 0, 0, 2560, 1392, true),
                new DisplayArea("vdd", 2560, 0, 3840, 2112, false),
            };
            var p = LaunchPlanBuilder.Build(i);
            Assert.Equal("2560,0,2064,2100", Env(p)["ETERNALVR_WINDOW"]);
            Assert.False(p.Window.Reduced);
            Assert.Contains("+r_windowWidth 2064", p.CommandLine);
            Assert.Contains("window:  2064x2100 per eye on vdd", p.Describe());
        }

        [Fact]
        public void StereoWindowShrinksToASmallDisplay()
        {
            var w = StereoWindow.Fit(new[] { new DisplayArea("a", -1920, 0, 1920, 1040, false), new DisplayArea("b", 0, 0, 2560, 1392, true) }, 2064, 2100);
            Assert.Equal("b", w.Display.Name);
            Assert.True(w.Reduced);
            Assert.Equal(1392, w.Height);
            Assert.Equal(1368, w.Width); // the eye's aspect is kept
            // Equal fits: the primary display wins.
            var tie = StereoWindow.Fit(new[] { new DisplayArea("a", 0, 0, 3840, 2112, false), new DisplayArea("b", 3840, 0, 3840, 2112, true) }, 2064, 2100);
            Assert.Equal("b", tie.Display.Name);
            Assert.Equal("3840,0,2064,2100", tie.EnvironmentValue);
        }

        [Fact]
        public void MonoKeepsTheGameWindowAndTheStereoCvarsOut()
        {
            var p = LaunchPlanBuilder.Build(Inputs(new LauncherSettings { Mode = VrMode.Mono, Aim = AimMode.Head, Hud = HudMode.Wrist }));
            var env = Env(p);
            Assert.False(env.ContainsKey("ETERNALVR_MODE"));
            Assert.False(env.ContainsKey("ETERNALVR_WINDOW"));
            // The UI layer stays on: menus over the game need it for their panel and pointer.
            Assert.Equal("1", env["ETERNALVR_UI_LAYER"]);
            Assert.Equal("wrist", env["ETERNALVR_HUD"]);
            Assert.Equal("head", env["ETERNALVR_AIM"]);
            Assert.Null(p.Window);
            foreach (var name in new[] { "r_TAASafeMode", "r_antialiasing", "r_TAAAntiGhosting", "rs_enable", "r_swapInterval", "r_fullscreen", "r_windowWidth" })
                Assert.DoesNotContain("+" + name, p.CommandLine);
            Assert.Contains("+r_hdrDisplay 0", p.CommandLine);
        }

        [Fact]
        public void HandAimWithoutControllersFallsBackToTheHead()
        {
            var env = Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { Controllers = false })));
            Assert.Equal("0", env["ETERNALVR_CONTROLLERS"]);
            Assert.Equal("head", env["ETERNALVR_AIM"]);
        }

        [Fact]
        public void WristHudNeedsTheControllers()
        {
            Assert.Equal("wrist", Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { Hud = HudMode.Wrist })))["ETERNALVR_HUD"]);
            Assert.Equal("panel", Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { Hud = HudMode.Wrist, Controllers = false })))["ETERNALVR_HUD"]);
            Assert.Equal("weapon", Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { Hud = HudMode.Weapon })))["ETERNALVR_HUD"]);
            Assert.Equal("panel", Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { Hud = HudMode.Weapon, Controllers = false })))["ETERNALVR_HUD"]);
            Assert.Equal("panel", Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { Hud = HudMode.Panel })))["ETERNALVR_HUD"]);
            Assert.Equal(HudMode.Panel, LauncherSettings.Parse(new LauncherSettings { Hud = HudMode.Panel }.Serialize()).Hud);
            Assert.Equal(HudMode.Wrist, LauncherSettings.Parse(new LauncherSettings { Hud = HudMode.Wrist }.Serialize()).Hud);
            Assert.Equal(HudMode.Weapon, LauncherSettings.Parse(new LauncherSettings { Hud = HudMode.Weapon }.Serialize()).Hud);
            Assert.Contains("hud = weapon", new LauncherSettings { Hud = HudMode.Weapon }.Serialize());
            Assert.Equal(HudMode.Weapon, LauncherSettings.Parse("hud = Weapon").Hud);
            // The row's choices in the enum's order.
            Assert.Equal(new[] { "On the HUD panel", "On your wrist", "On your weapon" }, SettingTexts.For(Setting.HudPlace).Choices);
            // A file from before the key, or a value this version does not know: the default (the panel).
            Assert.Equal(HudMode.Panel, LauncherSettings.Parse("schema_version = 2\nworld_scale = 1.10\n").Hud);
            Assert.Equal(HudMode.Panel, LauncherSettings.Parse("hud = elbow").Hud);
        }

        [Fact]
        public void ExtraArgumentsComeAfterTheForcedOnes()
        {
            var p = LaunchPlanBuilder.Build(Inputs(new LauncherSettings { ExtraArguments = "+r_TAASafeMode 1" }));
            Assert.True(p.CommandLine.IndexOf("+r_TAASafeMode 0", StringComparison.Ordinal) < p.CommandLine.IndexOf("+r_TAASafeMode 1", StringComparison.Ordinal));
        }

        [Fact]
        public void SettingsMapToLayerVariables()
        {
            var s = new LauncherSettings { WorldScale = 1.5, Aim = AimMode.View, SkipCinematics = false, Runtime = @"C:\Steam VR\steamxr_win64.json" };
            var env = Env(LaunchPlanBuilder.Build(Inputs(s)));
            Assert.Equal("1.20", env["ETERNALVR_WORLD_SCALE"]); // clamped to the launcher's range
            Assert.Equal("view", env["ETERNALVR_AIM"]);
            Assert.Equal("0", env["ETERNALVR_SKIP_CINEMATICS"]);
            Assert.Equal(@"C:\Steam VR\steamxr_win64.json", env["XR_RUNTIME_JSON"]);
        }

        [Fact]
        public void RoomScaleDefaultsReachTheLayer()
        {
            var env = Env(LaunchPlanBuilder.Build(Inputs()));
            Assert.Equal("auto", env["ETERNALVR_POSTURE"]);
            Assert.Equal("slayer", env["ETERNALVR_HEIGHT"]);
            Assert.Equal("2.0", env["ETERNALVR_RECENTER_HOLD"]);
            Assert.False(env.ContainsKey("ETERNALVR_IPD")); // the headset's own
        }

        [Fact]
        public void RoomScaleSettingsMapToLayerVariables()
        {
            var s = new LauncherSettings { Posture = PostureMode.Seated, Height = HeightMode.Real, IpdMm = 66.5, RecenterLongPress = false, WorldScale = 0.85 };
            var env = Env(LaunchPlanBuilder.Build(Inputs(s)));
            Assert.Equal("seated", env["ETERNALVR_POSTURE"]);
            Assert.Equal("real", env["ETERNALVR_HEIGHT"]);
            Assert.Equal("0", env["ETERNALVR_RECENTER_HOLD"]);
            Assert.Equal("66.5", env["ETERNALVR_IPD"]);
            Assert.Equal("0.85", env["ETERNALVR_WORLD_SCALE"]);
            Assert.Equal("standing", Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { Posture = PostureMode.Standing })))["ETERNALVR_POSTURE"]);
            // An IPD outside the range is clamped into it.
            Assert.Equal("80.0", Env(LaunchPlanBuilder.Build(Inputs(new LauncherSettings { IpdMm = 95 })))["ETERNALVR_IPD"]);
        }

        [Fact]
        public void HkcuRouteLeavesTheLayerPathOut()
        {
            var i = Inputs();
            i.Route = LayerRoute.HkcuRegistration;
            var p = LaunchPlanBuilder.Build(i);
            Assert.False(Env(p).ContainsKey("VK_ADD_IMPLICIT_LAYER_PATH"));
            Assert.Equal("1", Env(p)["ETERNALVR_ENABLE_LAYER"]);
        }

        [Fact]
        public void NoSettingsLocationMeansNothingForced()
        {
            var i = Inputs(new LauncherSettings { ExtraArguments = "+map game/sp/e1m1_intro/e1m1_intro" });
            i.ForceCvars = false;
            var p = LaunchPlanBuilder.Build(i);
            Assert.Equal(new[] { "+map", "game/sp/e1m1_intro/e1m1_intro" }, p.Arguments);
        }

        [Fact]
        public void LayerDisableVariablesAreAdded()
        {
            var layer = new InstalledLayer(LayerApi.OpenXR, "XR_APILAYER_NOVENDOR_toolkit", "t.json", "HKLM", true, "DISABLE_XR_APILAYER_NOVENDOR_toolkit", "1");
            var i = Inputs();
            i.LayerDecisions = new[]
            {
                new LayerDecision(layer, LayerAction.Disable, "off", new KeyValuePair<string, string>("DISABLE_XR_APILAYER_NOVENDOR_toolkit", "1")),
                new LayerDecision(layer, LayerAction.Warn, "warn", null),
            };
            Assert.Equal("1", Env(LaunchPlanBuilder.Build(i))["DISABLE_XR_APILAYER_NOVENDOR_toolkit"]);
        }

        [Theory]
        [InlineData("+m_sensitivity 3", new[] { "m_sensitivity" })]
        [InlineData("+set r_fullscreen 1 +seta m_invertY \"1\" + com_skipIntroVideo 1", new[] { "r_fullscreen", "m_invertY", "com_skipIntroVideo" })]
        [InlineData("+r_antialiasing 0 +R_ANTIALIASING 1 -nosteam", new[] { "r_antialiasing" })]
        [InlineData("+map game/sp/e1m1_intro/e1m1_intro", new[] { "map" })]
        [InlineData("+ +1bad + \"\"", new string[0])]
        [InlineData(null, new string[0])]
        public void ExtraArgumentsNameTheCvarsTheySet(string extra, string[] expected)
        {
            Assert.Equal(expected, SessionKeys.FromArguments(extra));
        }

        [Fact]
        public void ArgumentsAreSplitAndQuoted()
        {
            Assert.Equal(new[] { "+a", "b c", "+d" }, LaunchPlanBuilder.SplitArguments("  +a \"b c\"   +d "));
            Assert.Empty(LaunchPlanBuilder.SplitArguments("   "));
            Assert.Equal("\"b c\"", LaunchPlan.QuoteIfNeeded("b c"));
            Assert.Equal("\"\"", LaunchPlan.QuoteIfNeeded(""));
            Assert.Equal("+x", LaunchPlan.QuoteIfNeeded("+x"));
        }

        [Theory]
        [InlineData(@"C:\My Games\", @"""C:\My Games\\""")]
        [InlineData(@"a ""b"" c", @"""a \""b\"" c""")]
        [InlineData(@"a\""b", @"""a\\\""b""")]
        [InlineData(@"C:\Program Files\x.json", @"""C:\Program Files\x.json""")]
        public void QuotingFollowsTheWindowsRules(string arg, string expected)
        {
            // A trailing backslash must not escape the closing quote (CommandLineToArgvW).
            Assert.Equal(expected, LaunchPlan.QuoteIfNeeded(arg));
        }

        [Theory]
        [InlineData(@"C:\Program Files\Virtual Desktop Streamer\OpenXR\virtualdesktop-openxr.json", true)]
        [InlineData(@"C:\Program Files\Meta Horizon\Support\oculus-runtime\oculus_openxr_64.json", false)]
        [InlineData(null, false)]
        public void VdxrIsRecognised(string manifest, bool expected)
        {
            Assert.Equal(expected, LaunchPlanBuilder.IsVdxr(manifest));
        }

        [Theory]
        [InlineData(@"C:\Program Files (x86)\Steam\steamapps\common\SteamVR\steamxr_win64.json", true)]
        [InlineData(@"D:/SteamLibrary/steamapps/common/SteamVR/STEAMXR_WIN64.JSON", true)]
        [InlineData(@"C:\Program Files\Virtual Desktop Streamer\OpenXR\virtualdesktop-openxr.json", false)]
        [InlineData(@"C:\SteamVR\oculus_openxr_64.json", false)]
        [InlineData(null, false)]
        public void SteamVrIsRecognised(string manifest, bool expected)
        {
            Assert.Equal(expected, LaunchPlanBuilder.IsSteamVr(manifest));
        }

        [Fact]
        public void MissingPathsThrow()
        {
            var i = Inputs();
            i.GameRoot = null;
            Assert.Throws<ArgumentException>(() => LaunchPlanBuilder.Build(i));
        }
    }

    public class StartWatchTests
    {
        [Theory]
        [InlineData(false, 3.0, false, StartOutcome.Running)]
        [InlineData(false, 30.0, true, StartOutcome.Running)]
        [InlineData(true, 4.0, true, StartOutcome.HandOff)]
        [InlineData(true, 600.0, true, StartOutcome.Exited)]
        [InlineData(true, 600.0, false, StartOutcome.Exited)]
        public void Decide(bool exited, double seconds, bool other, StartOutcome expected)
        {
            Assert.Equal(expected, StartWatch.Decide(exited, seconds, exited ? 0.5 : 0, other));
        }

        [Fact]
        public void AnEarlyExitWaitsForTheProcessSteamStartsAfterIt()
        {
            // Steam starts the new process only after the first has exited: seeing no other process at
            // the moment of the exit is not yet a plain early exit.
            Assert.Equal(StartOutcome.WaitingForHandOff, StartWatch.Decide(true, 2.0, 0.0, false));
            Assert.Equal(StartOutcome.WaitingForHandOff, StartWatch.Decide(true, 9.0, 7.0, false));
            Assert.Equal(StartOutcome.HandOff, StartWatch.Decide(true, 5.0, 3.0, true));
            // The grace runs past the watch window when the exit came late in it.
            Assert.Equal(StartOutcome.HandOff, StartWatch.Decide(true, 14.0, 5.0, true));
            Assert.Equal(StartOutcome.ExitedEarly, StartWatch.Decide(true, 2.0 + StartWatch.HandOffGraceSeconds, StartWatch.HandOffGraceSeconds, false));
            Assert.Equal(StartOutcome.Exited, StartWatch.Decide(true, 25.0, 1.0, true));
        }

        [Fact]
        public void ACrashBeforeTheLayerLoadedSaysEternalVrIsNotTheCause()
        {
            const int accessViolation = unchecked((int)0xC0000005);
            var before = StartWatch.EarlyExitMessage(1.2, accessViolation, layerLoaded: false);
            Assert.Contains("before EternalVR loaded", before);
            Assert.Contains("0xC0000005", before);
            Assert.Contains("VR code had not run yet", before);
            var after = StartWatch.EarlyExitMessage(3.0, accessViolation, layerLoaded: true);
            Assert.DoesNotContain("before EternalVR loaded", after);
            Assert.StartsWith("The game closed 3 s after it started", after);
            // A plain exit without the marker is not called a crash.
            Assert.DoesNotContain("crashed", StartWatch.EarlyExitMessage(2.0, 1, layerLoaded: false));
        }
    }

    public class LauncherSettingsTests
    {
        [Fact]
        public void RoundTrip()
        {
            var s = new LauncherSettings { GameDir = @"D:\Games\DOOMEternal", Runtime = @"C:\x.json", WorldScale = 0.9, Aim = AimMode.View, SkipCinematics = false, ExtraArguments = "+map x",
                                           Mode = VrMode.Mono, Controllers = false, EyeWidth = 1440, EyeHeight = 1470 };
            var r = LauncherSettings.Parse(s.Serialize());
            Assert.Equal(VrMode.Mono, r.Mode);
            Assert.False(r.Controllers);
            Assert.Equal(1440, r.EyeWidth);
            Assert.Equal(1470, r.EyeHeight);
            Assert.Equal(AimMode.Head, LauncherSettings.Parse(new LauncherSettings { Aim = AimMode.Head }.Serialize()).Aim);
            Assert.Equal(s.GameDir, r.GameDir);
            Assert.Equal(s.Runtime, r.Runtime);
            Assert.Equal(0.9, r.WorldScale, 3);
            Assert.Equal(AimMode.View, r.Aim);
            Assert.False(r.SkipCinematics);
            Assert.Equal("+map x", r.ExtraArguments);
        }

        [Fact]
        public void DefaultsAndClamping()
        {
            var d = LauncherSettings.Parse("");
            Assert.Equal(1.0, d.WorldScale);
            Assert.Equal(AimMode.Hand, d.Aim);
            Assert.Equal(VrMode.Stereo, d.Mode);
            Assert.True(d.Controllers);
            Assert.Equal(2064, d.EyeWidth);
            Assert.Equal(2100, d.EyeHeight);
            Assert.Equal(2064, LauncherSettings.Parse("schema_version = 2\neye_size = 99x99").EyeWidth); // out of range: default
            Assert.True(d.SkipCinematics);
            Assert.Equal(LauncherSettings.SystemRuntime, d.Runtime);
            Assert.Equal(0.85, LauncherSettings.Parse("world_scale = 0.1").WorldScale);
            Assert.Equal(1.0, LauncherSettings.Parse("world_scale = abc").WorldScale);
        }

        [Fact]
        public void RoomScaleSettingsRoundTripAndDefault()
        {
            var s = new LauncherSettings { Posture = PostureMode.Standing, Height = HeightMode.Real, IpdMm = 63.0, RecenterLongPress = false };
            var r = LauncherSettings.Parse(s.Serialize());
            Assert.Equal(PostureMode.Standing, r.Posture);
            Assert.Equal(HeightMode.Real, r.Height);
            Assert.Equal(63.0, r.IpdMm, 3);
            Assert.False(r.RecenterLongPress);
            // A schema 2 file written before these keys existed takes the defaults.
            var old = LauncherSettings.Parse("schema_version = 2\nworld_scale = 1.10\n");
            Assert.Equal(PostureMode.Auto, old.Posture);
            Assert.Equal(HeightMode.Slayer, old.Height);
            Assert.Equal(0.0, old.IpdMm);
            Assert.True(old.RecenterLongPress);
            Assert.Equal(PostureMode.Auto, LauncherSettings.Parse("posture = lying").Posture);
            Assert.Equal(0.0, LauncherSettings.Parse("ipd_mm = -3").IpdMm);
            Assert.Equal(50.0, LauncherSettings.Parse("ipd_mm = 20").IpdMm);
            Assert.Contains("schema_version = 2", s.Serialize());
        }

        [Fact]
        public void SchemaOneFilesTakeTheNewDefaults()
        {
            var v1 = LauncherSettings.Parse("schema_version = 1\nworld_scale = 1.10\naim = head\nskip_cinematics = 0\nmode = mono\ncontrollers = 0\n");
            Assert.Equal(AimMode.Hand, v1.Aim); // schema 1 wrote head as its default
            Assert.Equal(VrMode.Stereo, v1.Mode);
            Assert.True(v1.Controllers);
            Assert.Equal(1.1, v1.WorldScale, 3);
            Assert.False(v1.SkipCinematics);
            Assert.Equal(AimMode.View, LauncherSettings.Parse("schema_version = 1\naim = view\n").Aim);
            Assert.Equal(AimMode.Head, LauncherSettings.Parse("schema_version = 2\naim = head\n").Aim);
            Assert.Contains("schema_version = 2", v1.Serialize());
        }

        [Fact]
        public void NewerSchemaIsRefusedAndTheFileLeftUnchanged()
        {
            using (var t = new TempDir())
            {
                var path = t.Write("launcher.ini", "schema_version = 3\nworld_scale = 1.1\n");
                Assert.Throws<SettingsException>(() => LauncherSettings.Load(path));
                Assert.Throws<SettingsException>(() => new LauncherSettings().Save(path));
                Assert.Equal("schema_version = 3\nworld_scale = 1.1\n", File.ReadAllText(path));
            }
        }

        [Fact]
        public void SaveWritesAtomicallyAndLoadsBack()
        {
            using (var t = new TempDir())
            {
                var path = t.Combine("sub", "launcher.ini");
                new LauncherSettings { WorldScale = 1.1 }.Save(path);
                new LauncherSettings { WorldScale = 1.15 }.Save(path);
                Assert.Equal(1.15, LauncherSettings.Load(path).WorldScale, 3);
                Assert.False(File.Exists(path + ".evr-tmp"));
            }
        }
    }

    public class LauncherOptionsTests
    {
        [Fact]
        public void NoOptionsOpensTheWindow()
        {
            Assert.False(LauncherOptions.Parse(new string[0]).Headless);
        }

        [Fact]
        public void DryRunWithOverrides()
        {
            var o = LauncherOptions.Parse(new[] { "--dry-run", "--data-root", "tmp/data", "--game-dir", "g" });
            Assert.True(o.DryRun);
            Assert.True(o.Headless);
            Assert.True(Path.IsPathRooted(o.DataRoot));
        }

        [Theory]
        [InlineData("--test-exe", "x.exe")]
        [InlineData("--test-exe", "x.exe", "--data-root", "d", "--saved-games", "s")]
        [InlineData("--launch")]
        [InlineData("--data-root")]
        [InlineData("--bogus")]
        [InlineData("--test-exe", "x.exe", "--data-root", "d", "--saved-games", "s", "--steam-root", "r", "--register-hkcu")]
        public void InvalidCombinationsAreRejected(params string[] args)
        {
            Assert.Throws<ArgumentException>(() => LauncherOptions.Parse(args));
        }

        [Fact]
        public void TestLaunchNeedsAllOverrides()
        {
            var o = LauncherOptions.Parse(new[] { "--test-exe", "x.exe", "--data-root", "d", "--saved-games", "s", "--steam-root", "r", "--launch" });
            Assert.True(o.Launch);
        }
    }

    public class DataFileTests
    {
        [Fact]
        public void ShippedDataLoads()
        {
            var data = LauncherData.Load(TestData.Dir);
            Assert.NotEmpty(data.ForcedCvars.All);
            Assert.NotEmpty(data.KnownLayers.All);
        }

        [Fact]
        public void KnownBuildsHoldTheRigBuild()
        {
            var b = KnownBuilds.Parse(TestData.Read("known-builds.txt")).Find("69DC13E88D1C19133EAD7950DC64EBCBD4A5A3F6BD6F9C336EBFFE56DF6A1C11");
            Assert.NotNull(b);
            Assert.Equal("25216728", b.BuildId);
        }

        [Fact]
        public void ForcedCvarsHoldTheRecipeAndNoDuplicates()
        {
            var names = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")).Names.ToList();
            Assert.Contains("r_hdrDisplay", names);
            // The layer holds the same comfort set at run time in stereo (stereo_seq::stereoComfortCvars); mono forces it.
            foreach (var held in new[] { "r_motionblur", "r_dof", "r_chromaticAberration", "r_vignette", "pm_noBob", "view_skipKicks", "view_skipShakes", "hands_fovScale", "meatHook_playerViewOverrideMode", "view_skipDamageEffect", "view_showPlayerDamageViewEffect", "view_damageBlur", "g_skipViewEffects" })
                Assert.Contains(held, names);
            // The restore puts the stereo keys back too.
            Assert.Contains("r_TAASafeMode", names);
            Assert.Contains("r_windowWidth", names);
            var shipped = ForcedCvars.Parse(TestData.Read("forced-cvars.txt"));
            Assert.DoesNotContain(shipped.For(false), c => c.StereoOnly);
            Assert.Contains(shipped.For(true), c => c.Name == "r_fullscreen" && c.Value == "0");
            Assert.Throws<FormatException>(() => ForcedCvars.Parse("a | 1 | flat"));
            Assert.Throws<FormatException>(() => ForcedCvars.Parse("a | $eye_width"));
            Assert.Throws<FormatException>(() => ForcedCvars.Parse("a | $other | stereo"));
            Assert.Throws<FormatException>(() => ForcedCvars.Parse("a | 1\nA | 2"));
            Assert.Throws<FormatException>(() => ForcedCvars.Parse("+a | 1"));
            Assert.Throws<FormatException>(() => ForcedCvars.Parse("a |"));
        }

        [Fact]
        public void SessionKeysHoldTheWindowKeysAndJoinTheForcedOnes()
        {
            var session = SessionKeys.Parse(TestData.Read("session-keys.txt")).Names;
            foreach (var k in new[] { "r_windowPosX", "r_windowPosY", "r_windowWidth", "r_windowHeight", "r_fullscreen", "r_mode" })
                Assert.Contains(k, session);
            // Knock-on keys the game saves itself because of a stereo setting.
            foreach (var k in new[] { "r_SSDO", "r_SSR", "r_blurRadialScale" })
                Assert.Contains(k, session);
            var restored = LauncherData.Load(TestData.Dir).RestoredKeys;
            Assert.Contains("r_windowPosX", restored);
            Assert.Contains("r_hdrDisplay", restored);
            Assert.Equal(restored.Count, restored.Distinct(StringComparer.OrdinalIgnoreCase).Count());
            // Listed in both files (r_fullscreen): restored once.
            Assert.Single(restored, k => k == "r_fullscreen");
            Assert.Equal(new[] { "a", "b" }, SessionKeys.RestoredKeys(ForcedCvars.Parse("a | 1"), SessionKeys.Parse("# c\nA\nb\n")));
            Assert.Throws<FormatException>(() => SessionKeys.Parse("a\nA"));
            Assert.Throws<FormatException>(() => SessionKeys.Parse("a | 1"));
            Assert.Throws<FormatException>(() => SessionKeys.Parse("+a"));
            Assert.Throws<FormatException>(() => SessionKeys.Parse("a b"));
        }

        [Fact]
        public void HashCheckOfAFile()
        {
            using (var t = new TempDir())
            {
                var exe = t.Write("game.exe", "hello");
                var hash = KnownBuilds.Sha256OfFile(exe);
                Assert.Equal("2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824", hash);
                var builds = KnownBuilds.Parse(hash + " | 1 | test");
                Assert.Equal(BuildStatus.Known, builds.Check(exe).Status);
                Assert.Equal(BuildStatus.Unknown, KnownBuilds.Parse("").Check(exe).Status);
                Assert.Equal(BuildStatus.Missing, builds.Check(t.Combine("nope.exe")).Status);
                Assert.Throws<FormatException>(() => KnownBuilds.Parse("xyz | 1 | bad"));
            }
        }

        [Fact]
        public void RecordsIgnoreCommentsAndBlankLines()
        {
            var r = DataFile.ParseRecords("# c\n\n a | b \r\nc\n");
            Assert.Equal(2, r.Count);
            Assert.Equal(new[] { "a", "b" }, r[0]);
            Assert.Equal("", DataFile.Field(r[1], 3));
        }
    }

    public class DataPathsTests
    {
        [Fact]
        public void SessionIdsNeverReuseAFolder()
        {
            using (var t = new TempDir())
            {
                var paths = new DataPaths(t.Combine("data"));
                paths.EnsureCreated();
                var now = new DateTime(2026, 9, 26, 1, 2, 3);
                Assert.Equal("20260926-010203", paths.UniqueSessionId(now));
                Directory.CreateDirectory(Path.Combine(paths.Snapshots, "20260926-010203"));
                Directory.CreateDirectory(Path.Combine(paths.SaveBackups, "20260926-010203-2"));
                Assert.Equal("20260926-010203-3", paths.UniqueSessionId(now));
            }
        }
    }
}
