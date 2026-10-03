using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Settings;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>Each VR settings profile's own controls (<see cref="ControlSets"/>) and the launch passing the set in use.</summary>
    public class ControlSetsTests
    {
        private static string ShippedMaps => Path.Combine(TestData.Dir, "controllers");

        private static IEnumerable<string> Names(string dir) => Directory.GetFiles(dir).Select(Path.GetFileName).OrderBy(n => n, StringComparer.Ordinal);

        private static string ControllerData(ControlsFolder controls, LauncherSettings s = null)
        {
            var plan = LaunchPlanBuilder.Build(new LaunchInputs
            {
                GameRoot = @"E:\SteamLibrary\steamapps\common\DOOMEternal",
                LayerDir = @"E:\EternalVR\layer",
                LogDir = @"E:\data\logs\20260929-010203",
                Settings = s ?? new LauncherSettings(),
                ForcedCvars = ForcedCvars.Parse(TestData.Read("forced-cvars.txt")),
                Controls = controls,
            });
            return plan.Environment.Where(e => e.Key == "ETERNALVR_CONTROLLER_DATA").Select(e => e.Value).FirstOrDefault();
        }

        /// <summary>The controls of (none) as a player of an older launcher has them: a map of their own, and the rest.</summary>
        private static ControlSets OlderPlayer(TempDir t)
        {
            var sets = new ControlSets(t.Combine("controls"));
            sets.Shared.Prepare(ShippedMaps);
            t.Write("controls/oculus_touch.toml", "my touch");
            t.Write("controls/Index Mine.TOML", "my index");
            t.Write("controls/notes.txt", "my notes");
            t.Write("controls/oculus_touch.toml.bak", "a backup");
            t.Write("controls/old/pico4.toml", "kept aside");
            return sets;
        }

        [Fact]
        public void TheControlsFolderIsTheSetOfNoProfileAndEachProfileHasAFolderUnderIt()
        {
            using (var t = new TempDir())
            {
                var sets = new ControlSets(t.Combine("controls"));
                Assert.Equal(t.Combine("controls"), sets.Shared.Dir);
                Assert.Null(sets.Shared.Profile);
                Assert.Equal(sets.Shared.Dir, sets.Of("").Dir);
                Assert.Equal(sets.Shared.Dir, sets.Of(null).Dir);
                Assert.Equal(t.Combine("controls", "profiles", "Riley"), sets.Of("Riley").Dir);
                Assert.Equal("Riley", sets.Of(" Riley ").Profile);
                // A name that cannot be a profile never reaches another folder.
                Assert.Equal(sets.Shared.Dir, sets.Of(@"..\..\x").Dir);
                Assert.Equal(sets.Shared.Dir, sets.Of("..").Dir);
                // "defaults" and "profiles" are profile names like any other.
                Assert.Equal(t.Combine("controls", "profiles", "defaults"), sets.Of("defaults").Dir);
            }
        }

        [Fact]
        public void AProfileWithoutItsOwnFolderUsesTheControlsOfNoProfile()
        {
            using (var t = new TempDir())
            {
                var sets = OlderPlayer(t);
                Assert.False(sets.HasOwn("Riley"));
                Assert.Equal(sets.Shared.Dir, sets.InUse("Riley").Dir);
                Assert.Equal(sets.Shared.Dir, sets.InUse("").Dir);
                Assert.Equal(sets.Shared.Dir, ControllerData(sets.InUse("Riley")));
                // Its own folder, even an empty one, is its own set: the built-in controls, not those of no profile.
                Directory.CreateDirectory(sets.Of("Riley").Dir);
                Assert.True(sets.HasOwn("Riley"));
                Assert.Equal(sets.Of("Riley").Dir, sets.InUse("Riley").Dir);
                Assert.False(sets.InUse("Riley").HasPlayerMaps);
                Assert.Null(ControllerData(sets.InUse("Riley")));
                t.Write("controls/profiles/Riley/oculus_touch.toml", "riley's touch");
                Assert.Equal(sets.Of("Riley").Dir, ControllerData(sets.InUse("Riley")));
                // The profiles' folders are not maps of no profile, and the layer reads only the files directly in a folder.
                File.Delete(t.Combine("controls", "oculus_touch.toml"));
                File.Delete(t.Combine("controls", "Index Mine.TOML"));
                Assert.False(sets.Shared.HasPlayerMaps);
                Assert.Null(ControllerData(sets.InUse("")));
            }
        }

        [Fact]
        public void AdoptGivesAProfileACopyOfThePlayersMapsOfNoProfileOnce()
        {
            using (var t = new TempDir())
            {
                var sets = OlderPlayer(t);
                Assert.True(sets.Adopt("Riley"));
                var riley = sets.Of("Riley");
                Assert.Equal(new[] { "Index Mine.TOML", "oculus_touch.toml" }, Names(riley.Dir));
                Assert.Empty(Directory.GetDirectories(riley.Dir));
                Assert.Equal("my touch", t.Read("controls/profiles/Riley/oculus_touch.toml"));
                Assert.Equal(riley.Dir, ControllerData(sets.InUse("Riley")));
                // From now on the two sets are apart.
                t.Write("controls/profiles/Riley/oculus_touch.toml", "riley's touch");
                t.Write("controls/oculus_touch.toml", "my new touch");
                Assert.False(sets.Adopt("Riley"));
                Assert.Equal("riley's touch", t.Read("controls/profiles/Riley/oculus_touch.toml"));
                Assert.Equal("my new touch", t.Read("controls/oculus_touch.toml"));
                // The set of no profile is left as it was.
                Assert.Equal("my notes", t.Read("controls/notes.txt"));
                Assert.Equal("kept aside", t.Read("controls/old/pico4.toml"));
                // No profile, or no name, has nothing to adopt.
                Assert.False(sets.Adopt(""));
                Assert.False(sets.Adopt("a/b"));
                Assert.Equal(new[] { "Riley" }, Directory.GetDirectories(sets.ProfilesDir).Select(Path.GetFileName));
            }
        }

        [Fact]
        public void AdoptBeforeAnyControlsWereSavedGivesAnEmptySet()
        {
            using (var t = new TempDir())
            {
                var sets = new ControlSets(t.Combine("controls"));
                Assert.True(sets.Adopt("Riley"));
                Assert.Empty(Directory.GetFiles(sets.Of("Riley").Dir));
                Assert.Null(ControllerData(sets.InUse("Riley")));
            }
        }

        [Fact]
        public void ACopyLeftHalfDoneIsNotAProfilesSetAndIsReplaced()
        {
            using (var t = new TempDir())
            {
                var sets = OlderPlayer(t);
                t.Write("controls/profiles/.evr-tmp-Riley/oculus_touch.toml", "half");
                Assert.False(sets.HasOwn("Riley"));
                Assert.True(sets.Adopt("Riley"));
                Assert.False(Directory.Exists(t.Combine("controls", "profiles", ".evr-tmp-Riley")));
                Assert.Equal("my touch", t.Read("controls/profiles/Riley/oculus_touch.toml"));
            }
        }

        [Fact]
        public void ANewProfileStartsFromACopyOfTheSetInUse()
        {
            using (var t = new TempDir())
            {
                var sets = OlderPlayer(t);
                // A folder left by a profile of the same name that is gone: replaced.
                t.Write("controls/profiles/Riley/valve_index.toml", "an old profile's");
                sets.StartFrom("Riley", "");
                Assert.Equal(new[] { "Index Mine.TOML", "oculus_touch.toml" }, Names(sets.Of("Riley").Dir));
                // From another profile: its own set, else the set of no profile it uses.
                t.Write("controls/profiles/Riley/oculus_touch.toml", "riley's touch");
                sets.StartFrom("Guest", "Riley");
                Assert.Equal("riley's touch", t.Read("controls/profiles/Guest/oculus_touch.toml"));
                sets.StartFrom("Guest2", "Nobody");
                Assert.Equal("my touch", t.Read("controls/profiles/Guest2/oculus_touch.toml"));
                // From itself: nothing changes.
                sets.StartFrom("Riley", "Riley");
                Assert.Equal("riley's touch", t.Read("controls/profiles/Riley/oculus_touch.toml"));
                Assert.Throws<ArgumentException>(() => sets.StartFrom("", "Riley"));
            }
        }

        [Fact]
        public void DeletingAProfilesControlsLeavesTheOthersAndTheSetOfNoProfile()
        {
            using (var t = new TempDir())
            {
                var sets = OlderPlayer(t);
                sets.Adopt("Riley");
                sets.Adopt("Evening");
                sets.Of("Riley").Prepare(ShippedMaps);
                File.SetAttributes(t.Combine("controls", "profiles", "Riley", "oculus_touch.toml"), FileAttributes.ReadOnly);
                sets.Delete("Riley");
                Assert.False(sets.HasOwn("Riley"));
                Assert.True(sets.HasOwn("Evening"));
                Assert.Equal("my touch", t.Read("controls/oculus_touch.toml"));
                Assert.True(File.Exists(sets.Shared.ReadmeFile));
                // No profile: the set of no profile is never deleted this way.
                sets.Delete("");
                sets.Delete(null);
                sets.Delete("..");
                sets.Delete("Nobody");
                Assert.True(sets.Shared.HasPlayerMaps);
            }
        }

        [Fact]
        public void APreparedProfileSetHasItsOwnReadmeAndDefaults()
        {
            using (var t = new TempDir())
            {
                var sets = new ControlSets(t.Combine("controls"));
                var riley = sets.Of("Riley");
                riley.Prepare(ShippedMaps);
                Assert.Equal(ControlsFolder.ReadmeFor("Riley"), File.ReadAllText(riley.ReadmeFile));
                Assert.Contains("VR settings profile Riley", ControlsFolder.ReadmeFor("Riley"));
                Assert.Equal(Names(ShippedMaps), Names(riley.DefaultsDir));
                Assert.False(riley.HasPlayerMaps);
                Assert.Equal(8, riley.Families().Count);
                Assert.Equal(ControlsFolder.Readme, ControlsFolder.ReadmeFor(null));
                Assert.Contains("profiles", ControlsFolder.Readme);
            }
        }
    }
}
