using System;
using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>
    /// Profiles follow their preset (T-134): a setting the player never changed takes the preset's value of the launcher that
    /// reads it, in the profile in use too (<see cref="ProfileEdits.InUse"/>).
    /// </summary>
    public class ProfileFollowTests
    {
        private static CpuSaver Shipped => CpuSaver.Parse(TestData.Read("cpu-saver.txt"));

        /// <summary>
        /// "Recommended" as a later launcher might have it, with a new default turn speed (250 instead of 230). The test reads
        /// the profile with it, as that launcher would read the same file.
        /// </summary>
        private static readonly SettingsPreset Later = new SettingsPreset("later", "Recommended", () => new LauncherSettings { TurnRate = 250 });

        private static SettingsPreset Presets(string id) => id == "later" ? Later : SettingsPresets.Find(id);

        [Fact]
        public void AChangedDefaultReachesTheProfileInUseAndItsUnsavedChangesStay()
        {
            // The profile as saved: Vignette changed from the preset; the turn speed never touched.
            var file = "schema_version = 2\npreset = later\nvignette = light\n";
            var saved = ProfileEdits.FromProfileText(file, Presets);
            saved.Profile = "Riley";
            Assert.Equal(250, saved.TurnRate);
            // launcher.ini as the earlier launcher wrote it: every key, the turn speed at the earlier default (230), and one
            // change not saved into the profile yet (snap turning).
            var older = new LauncherSettings { Profile = "Riley", Preset = "later", Vignette = VignetteMode.Light, Turn = TurnMode.Snap, GameDir = @"E:\Games\DOOMEternal" };
            older.ProfileUnsaved = new[] { "turn" };
            // The check value of the lines written with the list (the launcher writes both, ProfileEdits.MarkUnsaved).
            older.ProfileCheck = LauncherSettings.Parse(older.Serialize() + "future_key = 7\n").FileCheck;
            var ini = LauncherSettings.Parse(older.Serialize() + "future_key = 7\n");
            Assert.True(ProfileEdits.ListHolds(ini));
            Assert.Equal(230, ini.TurnRate);
            Assert.Equal(new[] { "turn" }, ini.ProfileUnsaved);

            // Without the fix launcher.ini kept 230 and the window marked it as a change from the preset.
            Assert.Contains("turn_rate", ProfileEdits.Changed(ini, Later.Values(), Shipped));

            var inUse = ProfileEdits.InUse(ini, saved, Shipped);
            Assert.Equal(250, inUse.TurnRate);
            Assert.Equal(TurnMode.Snap, inUse.Turn);
            Assert.Equal(VignetteMode.Light, inUse.Vignette);
            Assert.Equal(@"E:\Games\DOOMEternal", inUse.GameDir);
            Assert.Equal("Riley", inUse.Profile);
            Assert.Contains(inUse.UnknownKeys, kv => kv.Key == "future_key");
            Assert.Equal(new[] { "turn", "vignette" }, ProfileEdits.Changed(inUse, Later.Values(), Shipped).Where(k => k != "future_key").OrderBy(k => k));
            Assert.Equal(new[] { "turn" }, ProfileEdits.Changed(inUse, saved, Shipped).Where(k => k != "future_key"));
        }

        /// <summary>The profile Riley as saved (Vignette light) and launcher.ini as the launcher writes it with snap turning not saved into it.</summary>
        private static (LauncherSettings Saved, string Written) Written()
        {
            var saved = ProfileEdits.FromProfileText("schema_version = 2\npreset = recommended\nvignette = light\n");
            saved.Profile = "Riley";
            var shown = new LauncherSettings { Profile = "Riley", Vignette = VignetteMode.Light, Turn = TurnMode.Snap };
            var s = ProfileEdits.ForSave(shown, null, null, saved, Shipped, out bool merged);
            Assert.False(merged);
            Assert.Equal(new[] { "turn" }, s.ProfileUnsaved);
            return (saved, s.Serialize());
        }

        /// <summary>launcher.ini's text with one line's value changed, every other line (the list and its check value too) as it was.</summary>
        private static string Edit(string text, string key, string value) =>
            string.Join("\n", text.Replace("\r\n", "\n").Split('\n').Select(l => l.StartsWith(key + " =", StringComparison.Ordinal) ? key + " = " + value : l));

        /// <summary>What the list alone (the code before the check value) would have started with: the profile file plus the listed keys.</summary>
        private static LauncherSettings ByTheListAlone(LauncherSettings ini, LauncherSettings saved) =>
            ProfileEdits.Reset(saved, ini, ini.ProfileUnsaved, Shipped);

        [Fact]
        public void AnUntouchedLauncherIniKeepsItsList()
        {
            var (saved, written) = Written();
            var ini = LauncherSettings.Parse(written);
            Assert.True(ProfileEdits.ListHolds(ini));
            var inUse = ProfileEdits.InUse(ini, saved, Shipped);
            Assert.Equal(TurnMode.Snap, inUse.Turn);
            Assert.Equal(VignetteMode.Light, inUse.Vignette);
        }

        [Fact]
        public void AHandEditWithTheLauncherClosedIsKept()
        {
            var (saved, written) = Written();
            // CONTROLS.md tells players to set bhaptics_intensity in launcher.ini by hand.
            var ini = LauncherSettings.Parse(Edit(written, "bhaptics_intensity", "0.50"));
            // By the list alone the edit was dropped for the profile's value.
            Assert.Equal(1.0, ByTheListAlone(ini, saved).BhapticsIntensity);
            Assert.False(ProfileEdits.ListHolds(ini));
            var inUse = ProfileEdits.InUse(ini, saved, Shipped);
            Assert.Equal(0.5, inUse.BhapticsIntensity);
            Assert.Equal(TurnMode.Snap, inUse.Turn);
        }

        [Fact]
        public void AHandEditMergedAtTheLaunchersLastSaveIsKept()
        {
            var (saved, written) = Written();
            var shown = LauncherSettings.Parse(written);
            // Edited while the launcher was open; its save at closing merges the edit in (LauncherSettings.MergeHandEdits).
            var disk = Edit(written, "bhaptics_intensity", "0.50");
            var s = ProfileEdits.ForSave(shown, written, disk, saved, Shipped, out bool merged);
            Assert.True(merged);
            Assert.Contains("bhaptics_intensity", s.ProfileUnsaved);
            var ini = LauncherSettings.Parse(s.Serialize());
            Assert.True(ProfileEdits.ListHolds(ini));
            Assert.Equal(0.5, ProfileEdits.InUse(ini, saved, Shipped).BhapticsIntensity);
            // The list worked out before the merge (the order before) misses the edit: by it alone the edit was dropped.
            var before = LauncherSettings.Parse(written);
            ProfileEdits.MarkUnsaved(before, saved, Shipped);
            var mergedBefore = LauncherSettings.Parse(LauncherSettings.MergeHandEdits(written, before.Serialize(), disk));
            Assert.DoesNotContain("bhaptics_intensity", mergedBefore.ProfileUnsaved);
            Assert.Equal(1.0, ByTheListAlone(mergedBefore, saved).BhapticsIntensity);
        }

        [Fact]
        public void ASettingChangedByAnOlderLauncherSurvivesTheUpgradeBack()
        {
            var (saved, written) = Written();
            // An older launcher keeps profile_unsaved and profile_check as unknown keys, as found, and writes its change.
            var ini = LauncherSettings.Parse(Edit(written, "turn_rate", "300"));
            Assert.Equal(new[] { "turn" }, ini.ProfileUnsaved);
            Assert.Equal(230, ByTheListAlone(ini, saved).TurnRate);
            Assert.False(ProfileEdits.ListHolds(ini));
            Assert.Equal(300, ProfileEdits.InUse(ini, saved, Shipped).TurnRate);
        }

        [Fact]
        public void AProfileLineEditedByHandDropsTheListAndAGameFolderDoesNot()
        {
            var (saved, written) = Written();
            // The list is Riley's: with Morgan named by hand, Morgan starts from launcher.ini as it is.
            var ini = LauncherSettings.Parse(Edit(written, "profile", "Morgan"));
            Assert.Equal("Morgan", ini.Profile);
            Assert.False(ProfileEdits.ListHolds(ini));
            // A path of this machine is not a setting a profile has.
            Assert.Contains("game_dir =", written);
            Assert.True(ProfileEdits.ListHolds(LauncherSettings.Parse(Edit(written, "game_dir", @"D:\Games\DOOMEternal"))));
        }

        [Fact]
        public void AnOlderLaunchIniOrAMissingProfileFileIsUsedAsItIs()
        {
            var ini = LauncherSettings.Parse("schema_version = 2\nprofile = Riley\nturn_rate = 300\n");
            Assert.Null(ini.ProfileUnsaved);
            var saved = new LauncherSettings { Profile = "Riley" };
            Assert.Same(ini, ProfileEdits.InUse(ini, saved));
            ini.ProfileUnsaved = new string[0];
            Assert.Same(ini, ProfileEdits.InUse(ini, null));
        }

        [Fact]
        public void TheUnsavedListIsLauncherIniOnlyAndNeverInAProfile()
        {
            var s = new LauncherSettings { Profile = "Riley", ProfileUnsaved = new[] { "turn", "cpu_saver_own_shadow" } };
            Assert.Contains("profile_unsaved = turn,cpu_saver_own_shadow", s.Serialize());
            Assert.Equal(new[] { "turn", "cpu_saver_own_shadow" }, LauncherSettings.Parse(s.Serialize()).ProfileUnsaved);
            // Nothing saved yet differs: an empty list, which still says the file was read.
            s.ProfileUnsaved = new string[0];
            Assert.Empty(LauncherSettings.Parse(s.Serialize()).ProfileUnsaved);
            // No profile in use: no line.
            Assert.DoesNotContain("profile_unsaved", new LauncherSettings { ProfileUnsaved = new[] { "turn" } }.Serialize());
            s.ProfileUnsaved = new[] { "turn" };
            Assert.DoesNotContain("profile_unsaved", ProfileEdits.ProfileText(s, Shipped));
            Assert.Empty(ProfileEdits.Changed(s, new LauncherSettings(), Shipped));
        }

        [Fact]
        public void ACpuSaverChoiceForAnItemThisLauncherDoesNotListIsKept()
        {
            var s = LauncherSettings.Parse("schema_version = 2\ncpu_saver_future_item = on\ncpu_saver_own_shadow = off\n");
            var text = ProfileEdits.ProfileText(s, Shipped);
            Assert.Contains("cpu_saver_future_item = on", text);
            // A listed item at its default is not written: it follows the data file's default.
            Assert.DoesNotContain("cpu_saver_own_shadow", text);
            var back = ProfileEdits.FromProfileText(text);
            Assert.True(back.CpuSaverChoice("future_item") == true);
            Assert.Contains("cpu_saver_future_item", ProfileEdits.Changed(s, new LauncherSettings(), Shipped));
            // Putting every setting back keeps it, as a key this launcher does not show.
            Assert.True(ProfileEdits.Reset(s, new LauncherSettings(), new[] { "turn" }, Shipped).CpuSaverChoice("future_item") == true);
        }
    }
}
