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
                Assert.Equal(t.Combine("controls", "profiles", "Karen"), sets.Of("Karen").Dir);
                Assert.Equal("Karen", sets.Of(" Karen ").Profile);
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
                Assert.False(sets.HasOwn("Karen"));
                Assert.Equal(sets.Shared.Dir, sets.InUse("Karen").Dir);
                Assert.Equal(sets.Shared.Dir, sets.InUse("").Dir);
                Assert.Equal(sets.Shared.Dir, ControllerData(sets.InUse("Karen")));
                // Its own folder, even an empty one, is its own set: the built-in controls, not those of no profile.
                Directory.CreateDirectory(sets.Of("Karen").Dir);
                Assert.True(sets.HasOwn("Karen"));
                Assert.Equal(sets.Of("Karen").Dir, sets.InUse("Karen").Dir);
                Assert.False(sets.InUse("Karen").HasPlayerMaps);
                Assert.Null(ControllerData(sets.InUse("Karen")));
                t.Write("controls/profiles/Karen/oculus_touch.toml", "karen's touch");
                Assert.Equal(sets.Of("Karen").Dir, ControllerData(sets.InUse("Karen")));
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
                Assert.True(sets.Adopt("Karen"));
                var karen = sets.Of("Karen");
                Assert.Equal(new[] { "Index Mine.TOML", "oculus_touch.toml" }, Names(karen.Dir));
                Assert.Empty(Directory.GetDirectories(karen.Dir));
                Assert.Equal("my touch", t.Read("controls/profiles/Karen/oculus_touch.toml"));
                Assert.Equal(karen.Dir, ControllerData(sets.InUse("Karen")));
                // From now on the two sets are apart.
                t.Write("controls/profiles/Karen/oculus_touch.toml", "karen's touch");
                t.Write("controls/oculus_touch.toml", "my new touch");
                Assert.False(sets.Adopt("Karen"));
                Assert.Equal("karen's touch", t.Read("controls/profiles/Karen/oculus_touch.toml"));
                Assert.Equal("my new touch", t.Read("controls/oculus_touch.toml"));
                // The set of no profile is left as it was.
                Assert.Equal("my notes", t.Read("controls/notes.txt"));
                Assert.Equal("kept aside", t.Read("controls/old/pico4.toml"));
                // No profile, or no name, has nothing to adopt.
                Assert.False(sets.Adopt(""));
                Assert.False(sets.Adopt("a/b"));
                Assert.Equal(new[] { "Karen" }, Directory.GetDirectories(sets.ProfilesDir).Select(Path.GetFileName));
            }
        }

        [Fact]
        public void AdoptBeforeAnyControlsWereSavedGivesAnEmptySet()
        {
            using (var t = new TempDir())
            {
                var sets = new ControlSets(t.Combine("controls"));
                Assert.True(sets.Adopt("Karen"));
                Assert.Empty(Directory.GetFiles(sets.Of("Karen").Dir));
                Assert.Null(ControllerData(sets.InUse("Karen")));
            }
        }

        [Fact]
        public void ACopyLeftHalfDoneIsNotAProfilesSetAndIsReplaced()
        {
            using (var t = new TempDir())
            {
                var sets = OlderPlayer(t);
                t.Write("controls/profiles/.evr-tmp-Karen/oculus_touch.toml", "half");
                Assert.False(sets.HasOwn("Karen"));
                Assert.True(sets.Adopt("Karen"));
                Assert.False(Directory.Exists(t.Combine("controls", "profiles", ".evr-tmp-Karen")));
                Assert.Equal("my touch", t.Read("controls/profiles/Karen/oculus_touch.toml"));
            }
        }

        [Fact]
        public void ANewProfileStartsFromACopyOfTheSetInUse()
        {
            using (var t = new TempDir())
            {
                var sets = OlderPlayer(t);
                // A folder left by a profile of the same name that is gone: replaced.
                t.Write("controls/profiles/Karen/valve_index.toml", "an old profile's");
                sets.StartFrom("Karen", "");
                Assert.Equal(new[] { "Index Mine.TOML", "oculus_touch.toml" }, Names(sets.Of("Karen").Dir));
                // From another profile: its own set, else the set of no profile it uses.
                t.Write("controls/profiles/Karen/oculus_touch.toml", "karen's touch");
                sets.StartFrom("Guest", "Karen");
                Assert.Equal("karen's touch", t.Read("controls/profiles/Guest/oculus_touch.toml"));
                sets.StartFrom("Guest2", "Nobody");
                Assert.Equal("my touch", t.Read("controls/profiles/Guest2/oculus_touch.toml"));
                // From itself: nothing changes.
                sets.StartFrom("Karen", "Karen");
                Assert.Equal("karen's touch", t.Read("controls/profiles/Karen/oculus_touch.toml"));
                Assert.Throws<ArgumentException>(() => sets.StartFrom("", "Karen"));
            }
        }

        [Fact]
        public void DeletingAProfilesControlsLeavesTheOthersAndTheSetOfNoProfile()
        {
            using (var t = new TempDir())
            {
                var sets = OlderPlayer(t);
                sets.Adopt("Karen");
                sets.Adopt("Evening");
                sets.Of("Karen").Prepare(ShippedMaps);
                File.SetAttributes(t.Combine("controls", "profiles", "Karen", "oculus_touch.toml"), FileAttributes.ReadOnly);
                sets.Delete("Karen");
                Assert.False(sets.HasOwn("Karen"));
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
                var karen = sets.Of("Karen");
                karen.Prepare(ShippedMaps);
                Assert.Equal(ControlsFolder.ReadmeFor("Karen"), File.ReadAllText(karen.ReadmeFile));
                Assert.Contains("VR settings profile Karen", ControlsFolder.ReadmeFor("Karen"));
                Assert.Equal(Names(ShippedMaps), Names(karen.DefaultsDir));
                Assert.False(karen.HasPlayerMaps);
                Assert.Equal(8, karen.Families().Count);
                Assert.Equal(ControlsFolder.Readme, ControlsFolder.ReadmeFor(null));
                Assert.Contains("profiles", ControlsFolder.Readme);
            }
        }
    }
}
