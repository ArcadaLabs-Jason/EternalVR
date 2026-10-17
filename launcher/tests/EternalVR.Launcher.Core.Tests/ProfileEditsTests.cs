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
    /// A VR settings profile as a preset plus the player's own changes (REQ-10, ROADMAP M8): which rows differ from the
    /// preset, putting one or all back, and the profile file holding only the changes (<see cref="ProfileEdits"/>).
    /// </summary>
    public class ProfileEditsTests
    {
        private static CpuSaver Shipped => CpuSaver.Parse(TestData.Read("cpu-saver.txt"));

        private static LauncherSettings Recommended => SettingsPresets.Recommended.Values();

        /// <summary>One change per row that holds a setting: each makes its own row differ from Recommended.</summary>
        private static readonly Dictionary<Setting, Action<LauncherSettings>> OneChange = new Dictionary<Setting, Action<LauncherSettings>>
        {
            [Setting.Turning] = s => s.Turn = TurnMode.Snap,
            [Setting.TurnSpeed] = s => s.TurnRate = 300,
            [Setting.SnapAngle] = s => s.SnapDegrees = 30,
            [Setting.Vignette] = s => s.Vignette = VignetteMode.Strong,
            [Setting.GloryKills] = s => s.GloryKills = GloryKillView.Steady,
            [Setting.WalkInRoom] = s => s.BodyFollow = false,
            [Setting.HeadFade] = s => s.HeadFade = false,
            [Setting.RecenterHold] = s => s.RecenterLongPress = false,
            [Setting.SkipCutscenes] = s => s.SkipCinematics = false,
            [Setting.PlayPosition] = s => s.Posture = PostureMode.Seated,
            [Setting.EyeHeight] = s => s.Height = HeightMode.Real,
            [Setting.ThrowGesture] = s => s.ThrowGesture = true,
            [Setting.SwingGesture] = s => s.SwingGesture = true,
            [Setting.HandsJump] = s => s.HandsJump = true,
            [Setting.PunchSpeed] = s => s.PunchSpeed = 2.2,
            [Setting.AimWith] = s => s.Aim = AimMode.Head,
            [Setting.RevenantAimWith] = s => s.RevenantAim = RevenantAimMode.Hand,
            [Setting.MeleeAimWith] = s => s.MeleeAim = ActionAimMode.Head,
            [Setting.EquipmentAimWith] = s => s.EquipmentAim = ActionAimMode.OffHand,
            [Setting.WeaponHand] = s => s.Hand = Handedness.Left,
            [Setting.Arms] = s => s.ShowArms = false,
            [Setting.MoveToward] = s => s.Locomotion = LocomotionMode.RightHand,
            [Setting.XButton] = s => s.Dossier = DossierPress.Tap,
            [Setting.DossierMapSticks] = s => s.MapSticks = MapPanStick.Other,
            [Setting.WeaponWheel] = s => s.Wheel = WheelSelect.Hand,
            [Setting.ThumbRestWheel] = s => s.ThumbRest = ThumbRestMode.Full,
            [Setting.ThumbRestPicks] = s => s.ThumbRestPicks = ThumbRestPick.Directions,
            [Setting.WeaponDirections] = s => s.WeaponDirections = "up_right=none,down_left=none",
            [Setting.ThumbRestFaceTouch] = s => s.ThumbRestFaceTouch = true,
            [Setting.ThumbRestSlowdown] = s => s.ThumbRestSlowdown = false,
            [Setting.HoldTime] = s => s.HoldTime = 0.4,
            [Setting.AimSteadiness] = s => s.AimSmoothing = 0.5,
            [Setting.AimDot] = s => s.AimDot = false,
            [Setting.AimDotSize] = s => s.AimDotSize = 2.0,
            [Setting.Vibration] = s => s.Vibration = 1.0,
            [Setting.Bhaptics] = s => s.Bhaptics = true,
            [Setting.Resolution] = s => s.RenderScale = 1.2,
            [Setting.AntiAliasing] = s => s.AntiAliasing = AntiAliasingMode.Dlss,
            [Setting.Sharpening] = s => s.Sharpening = SharpeningMode.Off,
            [Setting.ScreenReflections] = s => s.Reflections = ReflectionsMode.Off,
            [Setting.Foveation] = s => s.Foveation = FoveationMode.Balanced,
            [Setting.FramePacing] = s => s.Pacing = FramePacing.Off,
            [Setting.ParallelEyes] = s => s.ParallelEyes = true,
            [Setting.DlssQuality] = s => s.Dlss = DlssQuality.Balanced,
            [Setting.DlssVersion] = s => s.DlssDll = DlssDllChoice.Game,
            [Setting.DlssPreset] = s => s.DlssPreset = "J",
            [Setting.VrMode] = s => s.Mode = VrMode.Mono,
            [Setting.AlternateEyes] = s => s.AlternateEyes = AlternateEyesMode.Auto,
            [Setting.WorldSize] = s => s.WorldScale = 1.1,
            [Setting.EyeDistance] = s => s.IpdMm = 64,
            [Setting.DesktopWindow] = s => s.Mirror = MirrorMode.Right,
            [Setting.DesktopMonitor] = s => s.MirrorDisplay = MirrorSettings.DisplayPrimary,
            [Setting.DesktopSize] = s => s.MirrorSize = "1920x1080",
            [Setting.DesktopCrop] = s => s.MirrorCrop = true,
            [Setting.CutsceneView] = s => s.Cutscenes = CutsceneView.Immersive,
            [Setting.CutsceneShape] = s => s.Cinema = CinemaShape.Full,
            [Setting.CutsceneArms] = s => s.CutsceneArms = true,
            [Setting.HudDistance] = s => s.HudDistance = 2.0,
            [Setting.HudSize] = s => s.HudWidth = 3.0,
            [Setting.HudHeight] = s => s.HudHeight = 0.5,
            [Setting.HudPlace] = s => s.Hud = HudMode.Wrist,
            [Setting.MotionControllers] = s => s.Controllers = false,
            [Setting.ShotsFrom] = s => s.Shots = ShotOrigin.Eye,
            [Setting.MenuLaser] = s => s.MenuBeam = false,
            [Setting.ExtraArguments] = s => s.ExtraArguments = "+com_skipIntroVideo 1",
        };

        /// <summary>A player's settings on this machine: folders, runtime and DLSS file set, as launcher.ini has them.</summary>
        private static LauncherSettings OnThisMachine()
        {
            var s = new LauncherSettings
            {
                GameDir = @"E:\Games\DOOMEternal", LayerDir = @"E:\EternalVR\layer", Runtime = @"E:\runtime.json", DlssDllPath = @"E:\dlss\nvngx_dlss.dll",
            };
            s.Profile = "Riley";
            return s;
        }

        /// <summary>The rows <paramref name="changed"/> marks: every row with one of its keys in it (the window's rule).</summary>
        private static IEnumerable<Setting> Marked(ISet<string> changed) =>
            Enum.GetValues(typeof(Setting)).Cast<Setting>().Where(id => SettingKeys.Of(id).Any(changed.Contains));

        [Fact]
        public void RecommendedIsTheLaunchersDefaults()
        {
            Assert.Equal(new LauncherSettings().Serialize(), Recommended.Serialize());
            Assert.Equal("recommended", Recommended.Preset);
            Assert.Same(SettingsPresets.Recommended, SettingsPresets.Find("Recommended"));
            Assert.Empty(ProfileEdits.Changed(new LauncherSettings(), Recommended, Shipped));
            // Nothing the player sees differs while only this machine's keys and the profile name do.
            Assert.Empty(ProfileEdits.Changed(OnThisMachine(), Recommended, Shipped));
        }

        [Fact]
        public void ThePresetIsKeptInTheSettingsFileAndAnOlderFileIsRecommended()
        {
            Assert.Equal("recommended", LauncherSettings.Parse("schema_version = 2\nturn = snap\n").Preset);
            Assert.Contains("preset = recommended", new LauncherSettings().Serialize());
            // A newer launcher's preset is kept as written, and compared as Recommended.
            var newer = LauncherSettings.Parse("schema_version = 2\npreset = Comfort\n");
            Assert.Equal("comfort", newer.Preset);
            Assert.Contains("preset = comfort", newer.Serialize());
            Assert.Same(SettingsPresets.Recommended, SettingsPresets.Of(newer));
            Assert.Equal("recommended", LauncherSettings.Parse("schema_version = 2\npreset = not a preset!\n").Preset);
        }

        [Fact]
        public void EachChangeMarksItsOwnRowAndNoOther()
        {
            foreach (var kv in OneChange)
            {
                var s = OnThisMachine();
                kv.Value(s);
                var changed = ProfileEdits.Changed(s, Recommended, Shipped);
                Assert.True(changed.Count > 0, kv.Key + " changed nothing in the file");
                Assert.Equal(new[] { kv.Key }, Marked(changed));
                Assert.All(changed, key => Assert.Contains(key, SettingKeys.Of(kv.Key)));
            }
        }

        [Fact]
        public void EveryRowThatHoldsASettingIsCheckedAndEveryProfileKeyHasARow()
        {
            var withKeys = Enum.GetValues(typeof(Setting)).Cast<Setting>().Where(id => SettingKeys.Of(id).Count > 0).ToList();
            Assert.Equal(withKeys.OrderBy(x => x), OneChange.Keys.OrderBy(x => x));
            var shown = new HashSet<string>(withKeys.SelectMany(SettingKeys.Of), StringComparer.OrdinalIgnoreCase);
            var all = OnThisMachine();
            all.WeaponDirections = "up=1";
            foreach (var key in ProfileEdits.Values(all, Shipped).Keys)
                Assert.True(shown.Contains(key) || SettingKeys.FileOnly.Contains(key) || Shipped.Items.Any(i => i.Key == key),
                    key + " is in a profile but no row shows it and it is not listed as set in the file only");
            // Every key named for a row is one the settings file has.
            var written = ProfileEdits.Values(all, Shipped).Keys.ToList();
            Assert.All(shown, key => Assert.Contains(key, written, StringComparer.OrdinalIgnoreCase));
        }

        [Fact]
        public void ACpuSaverItemIsChangedOnlyWhenItIsNotAtItsDefault()
        {
            var saver = Shipped;
            var streaming = saver.Items.First(i => i.DefaultOn);
            var shadow = saver.Items.First(i => !i.DefaultOn);
            var s = new LauncherSettings();
            // The window writes a choice for every item it shows: at the defaults that is no change.
            s.SetCpuSaverChoices(saver.Items.Select(i => new KeyValuePair<string, bool>(i.Id, i.DefaultOn)));
            Assert.Empty(ProfileEdits.Changed(s, Recommended, saver));
            s.SetCpuSaverChoices(new[] { new KeyValuePair<string, bool>(shadow.Id, true) });
            Assert.Equal(new[] { shadow.Key }, ProfileEdits.Changed(s, Recommended, saver));
            // An older launcher's cpu_saver = on turns every item on that has no choice of its own.
            var older = LauncherSettings.Parse("schema_version = 2\ncpu_saver = on\n");
            Assert.Equal(saver.Items.Where(i => !i.DefaultOn).Select(i => i.Key).OrderBy(k => k),
                ProfileEdits.Changed(older, Recommended, saver).OrderBy(k => k));
            // Putting one item back keeps the others on, as that switch had them.
            var back = ProfileEdits.Reset(older, Recommended, new[] { shadow.Key }, saver);
            Assert.False(CpuSaver.IsOn(shadow, back));
            Assert.All(saver.Items.Where(i => i != shadow), i => Assert.True(CpuSaver.IsOn(i, back)));
            Assert.True(CpuSaver.IsOn(streaming, back));
        }

        [Fact]
        public void PuttingOneSettingBackLeavesTheOthersAndThisMachinesKeys()
        {
            var s = OnThisMachine();
            OneChange[Setting.Turning](s);
            OneChange[Setting.Vignette](s);
            OneChange[Setting.Resolution](s);
            s.ResolutionBase = ResolutionBase.Panel;
            var back = ProfileEdits.Reset(s, Recommended, SettingKeys.Of(Setting.Resolution), Shipped);
            Assert.Equal(new[] { Setting.Turning, Setting.Vignette }, Marked(ProfileEdits.Changed(back, Recommended, Shipped)));
            Assert.Equal(1.0, back.RenderScale);
            Assert.Equal(ResolutionBase.Auto, back.ResolutionBase);
            Assert.Equal(TurnMode.Snap, back.Turn);
            Assert.Equal(s.GameDir, back.GameDir);
            Assert.Equal(s.Runtime, back.Runtime);
            Assert.Equal(s.DlssDllPath, back.DlssDllPath);
            Assert.Equal("Riley", back.Profile);
            // A setting only one side writes: weapon_directions is no line at all for the game's wheel.
            s.WeaponDirections = "up=1";
            Assert.Equal(string.Empty, ProfileEdits.Reset(s, Recommended, SettingKeys.Of(Setting.WeaponDirections), Shipped).WeaponDirections);
        }

        [Fact]
        public void ResetAllIsWhatResetToDefaultsGaveAndKeepsWhatItKept()
        {
            var s = LauncherSettings.Parse(OnThisMachine().Serialize() + "future_key = 7\n");
            foreach (var change in OneChange.Values) change(s);
            s.SetCpuSaverChoices(Shipped.Items.Select(i => new KeyValuePair<string, bool>(i.Id, !i.DefaultOn)));
            var all = ProfileEdits.ResetAll(s, Recommended, Shipped);
            Assert.Equal(new[] { "future_key" }, ProfileEdits.Changed(all, Recommended, Shipped));
            Assert.Equal(ProfileEdits.Values(s.WithDefaults(), Shipped), ProfileEdits.Values(all, Shipped));
            Assert.Contains(all.UnknownKeys, kv => kv.Key == "future_key" && kv.Value == "7");
            Assert.Equal(@"E:\Games\DOOMEternal", all.GameDir);
            Assert.Equal(@"E:\runtime.json", all.Runtime);
            Assert.Equal("Riley", all.Profile);
        }

        [Fact]
        public void AProfileFileHoldsOnlyTheChangesAndReadsBackTheSame()
        {
            using (var t = new TempDir())
            {
                var store = new ProfileStore(t.Combine("profiles"));
                var s = OnThisMachine();
                OneChange[Setting.Turning](s);
                OneChange[Setting.HudPlace](s);
                s.SetCpuSaverChoices(Shipped.Items.Select(i => new KeyValuePair<string, bool>(i.Id, i.DefaultOn)));
                store.Save("Riley", s, Shipped);
                var lines = File.ReadAllLines(t.Combine("profiles", "Riley.ini")).Where(l => !l.StartsWith("#")).ToList();
                Assert.Equal(new[] { "schema_version = 2", "preset = recommended", "turn = snap", "hud = wrist" }, lines);
                var loaded = store.Load("Riley", new LauncherSettings { GameDir = @"D:\Other" });
                Assert.Equal(ProfileEdits.Values(s, Shipped), ProfileEdits.Values(loaded, Shipped));
                Assert.Equal(@"D:\Other", loaded.GameDir);
            }
        }

        [Fact]
        public void EveryChangeSurvivesTheProfileFile()
        {
            using (var t = new TempDir())
            {
                var store = new ProfileStore(t.Combine("profiles"));
                var changes = OneChange.Select(kv => (Name: kv.Key.ToString(), Change: kv.Value)).Concat(new (string, Action<LauncherSettings>)[]
                {
                    ("render_size", s => s.RenderSize = "1600x1700"),
                    ("eye_size", s => { s.EyeWidth = 1500; s.EyeHeight = 1600; }),
                    ("bhaptics_intensity", s => s.BhapticsIntensity = 0.5),
                    ("cpu", s => s.SetCpuSaverChoices(Shipped.Items.Select(i => new KeyValuePair<string, bool>(i.Id, !i.DefaultOn)))),
                    ("every change", s => { foreach (var c in OneChange.Values) c(s); }),
                });
                foreach (var (name, change) in changes)
                {
                    var s = OnThisMachine();
                    change(s);
                    store.Save("P", s, Shipped);
                    var loaded = store.Load("P", s);
                    Assert.True(ProfileEdits.Values(s, Shipped).SequenceEqual(ProfileEdits.Values(loaded, Shipped)), name + " did not read back the same");
                    // Without the CPU Saver's items (the window always has them) the lines are compared as written.
                    store.Save("P", s);
                    Assert.Equal(ProfileEdits.Values(s), ProfileEdits.Values(store.Load("P", s)));
                }
            }
        }

        [Fact]
        public void AnOlderLaunchersProfileReadsTheSameAndIsKeptOnceWhenSavedOver()
        {
            using (var t = new TempDir())
            {
                var older = OnThisMachine();
                OneChange[Setting.Vignette](older);
                OneChange[Setting.AimWith](older);
                // As an older launcher (0.1.50 and before) wrote it: every key, no preset.
                var text = string.Join("\n", older.Serialize().Split('\n').Where(l => !l.StartsWith("preset") && !l.StartsWith("profile")
                    && !l.StartsWith("game_dir") && !l.StartsWith("layer_dir") && !l.StartsWith("runtime") && !l.StartsWith("dlss_dll_path")));
                t.Write("profiles/Old.ini", text);
                // And one from before the DLSS group and frame pacing keys (dlss_dll, pace) with the old CPU Saver switch.
                var oldest = "schema_version = 2\naim = head\ndlss_dll = file\ndlss_preset = default\npace = headset\ncpu_saver = on\nturn = snap\n";
                t.Write("profiles/Oldest.ini", oldest);
                var store = new ProfileStore(t.Combine("profiles"));
                foreach (var (name, file) in new[] { ("Old", text), ("Oldest", oldest) })
                {
                    var loaded = store.Load(name, OnThisMachine());
                    var parsed = LauncherSettings.Parse(file);
                    Assert.Equal(ProfileEdits.Values(parsed, Shipped), ProfileEdits.Values(loaded, Shipped));
                    Assert.Equal("recommended", loaded.Preset);
                    loaded.TurnRate = 250;
                    store.Save(name, loaded, Shipped);
                    Assert.Equal(file, t.Read("profiles/backup/" + name + ".ini"));
                    var again = store.Load(name, OnThisMachine());
                    Assert.Equal(ProfileEdits.Values(loaded, Shipped), ProfileEdits.Values(again, Shipped));
                    Assert.Contains("preset = recommended", t.Read("profiles/" + name + ".ini"));
                    // The backup is the older launcher's file: a later save leaves it.
                    store.Save(name, again, Shipped);
                    Assert.Equal(file, t.Read("profiles/backup/" + name + ".ini"));
                }
                Assert.Equal(new[] { "Old", "Oldest" }, store.Names());
                // The oldest file's own values came through the rewrite.
                var oldestNow = store.Load("Oldest", OnThisMachine());
                Assert.Equal(DlssDllChoice.File, oldestNow.DlssDll);
                Assert.Equal(DlssDll.RecommendedPreset, oldestNow.DlssPreset);
                Assert.Equal(FramePacing.Headset, oldestNow.Pacing);
                Assert.Equal(AimMode.Head, oldestNow.Aim);
                Assert.All(Shipped.Items, i => Assert.True(CpuSaver.IsOn(i, oldestNow)));
            }
        }

        [Fact]
        public void APresetThisLauncherDoesNotKnowIsSavedWithEveryLine()
        {
            using (var t = new TempDir())
            {
                var store = new ProfileStore(t.Combine("profiles"));
                var s = LauncherSettings.Parse("schema_version = 2\npreset = comfort\nturn = snap\n");
                store.Save("Newer", s, Shipped);
                var text = t.Read("profiles/Newer.ini");
                Assert.Contains("preset = comfort", text);
                Assert.Contains("vignette = off", text);
                Assert.Contains("turn_rate = 230", text);
                Assert.Equal(ProfileEdits.Values(s, Shipped), ProfileEdits.Values(store.Load("Newer", s), Shipped));
            }
        }

        [Fact]
        public void AnotherPresetGivesTheSettingsAProfileDoesNotName()
        {
            var comfort = new SettingsPreset("comfort", "Comfort", () => new LauncherSettings
            {
                Turn = TurnMode.Snap, Vignette = VignetteMode.Strong, Pacing = FramePacing.Off,
            });
            Func<string, SettingsPreset> presets = id => id == "comfort" ? comfort : SettingsPresets.Find(id);
            var s = ProfileEdits.FromProfileText("schema_version = 2\npreset = comfort\nvignette = light\n", presets);
            Assert.Equal(TurnMode.Snap, s.Turn);
            Assert.Equal(VignetteMode.Light, s.Vignette);
            Assert.Equal(FramePacing.Off, s.Pacing);
            Assert.Equal(new[] { Setting.Vignette }, Marked(ProfileEdits.Changed(s, comfort.Values(), Shipped)));
            // An older key the file has (pace) gives the newer one (frame_pacing), not the preset.
            Assert.Equal(FramePacing.Headset, ProfileEdits.FromProfileText("schema_version = 2\npreset = comfort\npace = headset\n", presets).Pacing);
        }
    }
}
