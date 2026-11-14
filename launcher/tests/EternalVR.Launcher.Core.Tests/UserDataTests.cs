using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using EternalVR.Launcher.Core.Game;
using EternalVR.Launcher.Core.Settings;
using EternalVR.Launcher.Core.Update;
using Xunit;

namespace EternalVR.Launcher.Core.Tests
{
    /// <summary>
    /// User data and updates (ROADMAP M4.5, T-093, T-106): the player's data lives under the data folder, so a release
    /// extracted or installed over the old program folder leaves profiles and controls as they were; an older settings
    /// file is migrated with a backup; a newer one is refused and left as it is.
    /// </summary>
    public class UserDataTests
    {
        /// <summary>Every file under <paramref name="dir"/> with its SHA-256, by path relative to it.</summary>
        private static Dictionary<string, string> Listing(string dir) =>
            Directory.GetFiles(dir, "*", SearchOption.AllDirectories)
                .ToDictionary(f => f.Substring(dir.Length + 1), KnownBuilds.Sha256OfFile, StringComparer.OrdinalIgnoreCase);

        /// <summary>A release as it sits in its folder: its files and the SHA256SUMS.txt listing them.</summary>
        private static void Release(TempDir t, string folder, string version)
        {
            var files = new[]
            {
                ("EternalVR.Launcher.exe", "launcher " + version), ("EternalVR.Launcher.Core.dll", "core " + version),
                ("layer/EternalVR.dll", "layer " + version), ("data/known-builds.txt", "builds " + version),
                ("data/controllers/oculus_touch.toml", "built-in touch map " + version),
            };
            var sums = new System.Text.StringBuilder();
            foreach (var (name, text) in files)
            {
                var path = t.Write(folder + "/" + name, text);
                sums.Append(KnownBuilds.Sha256OfFile(path)).Append("  ").Append(name).Append('\n');
            }
            t.Write(folder + "/SHA256SUMS.txt", sums.ToString());
        }

        [Fact]
        public void ProfilesAndControlsSurviveTheNextReleaseOverTheOldFolder()
        {
            using (var t = new TempDir())
            {
                // Release N unzipped into the player's folder, the player's data in %LOCALAPPDATA%\EternalVR.
                Release(t, "EternalVR", "0.1.92");
                var program = t.Combine("EternalVR");
                var paths = DataPaths.ForLocalAppData(t.Combine("LocalAppData"));
                Assert.False(paths.Root.StartsWith(program + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase));

                var store = new ProfileStore(paths.Profiles);
                store.Save("Riley", new LauncherSettings { Turn = TurnMode.Snap, SnapDegrees = 30, Vignette = VignetteMode.Light });
                var sets = new ControlSets(paths.Controls);
                var riley = sets.Of("Riley");
                riley.Prepare(Path.Combine(program, "data", "controllers"));
                File.WriteAllText(Path.Combine(riley.Dir, "oculus_touch.toml"), "Riley's own touch map");
                var shared = sets.Shared;
                shared.Prepare(Path.Combine(program, "data", "controllers"));
                File.WriteAllText(Path.Combine(shared.Dir, "valve_index.toml"), "my index map");
                new LauncherSettings { Profile = "Riley", WorldScale = 1.1 }.Save(paths.SettingsFile);
                var before = Listing(paths.Root);

                // N+1 by hand: its zip extracted over the old folder, every file replaced (Explorer's "Replace").
                Release(t, "EternalVR", "0.1.93");
                Assert.Equal("launcher 0.1.93", t.Read("EternalVR/EternalVR.Launcher.exe"));
                Assert.Equal(before, Listing(paths.Root));

                // N+2 by the launcher's own update: staged, then installed over the folder.
                Release(t, "staging", "0.1.94");
                UpdatePackage.Install(t.Combine("staging"), program);
                Assert.Equal("layer 0.1.94", t.Read("EternalVR/layer/EternalVR.dll"));
                Assert.Equal(before, Listing(paths.Root));

                // The new launcher reads the same: the profile in use, its settings and both sets of controls.
                var ini = LauncherSettings.Load(paths.SettingsFile);
                Assert.Equal("Riley", ini.Profile);
                Assert.Equal(1.1, ini.WorldScale, 3);
                var loaded = store.Load("Riley", ini);
                Assert.Equal(TurnMode.Snap, loaded.Turn);
                Assert.Equal(30.0, loaded.SnapDegrees, 3);
                Assert.Equal(VignetteMode.Light, loaded.Vignette);
                Assert.Equal(riley.Dir, sets.InUse("Riley").Dir);
                // Its Edit controls refreshes the built-in copies from the new release and leaves the player's maps alone.
                riley.Prepare(Path.Combine(program, "data", "controllers"));
                Assert.Equal("built-in touch map 0.1.94", File.ReadAllText(Path.Combine(riley.DefaultsDir, "oculus_touch.toml")));
                Assert.Equal("Riley's own touch map", File.ReadAllText(Path.Combine(riley.Dir, "oculus_touch.toml")));
                Assert.Equal("my index map", File.ReadAllText(Path.Combine(shared.Dir, "valve_index.toml")));
            }
        }

        [Fact]
        public void AnOlderSettingsFileIsMigratedWithABackup()
        {
            using (var t = new TempDir())
            {
                const string schema1 = "schema_version = 1\nworld_scale = 1.10\naim = head\nmode = mono\n";
                var path = t.Write("data/launcher.ini", schema1);
                var s = LauncherSettings.Load(path);
                Assert.Equal(1.1, s.WorldScale, 3);
                Assert.Equal(AimMode.Hand, s.Aim); // schema 1's head was its default
                s.Save(path);

                var backup = LauncherSettings.SchemaBackupPath(path, 1);
                Assert.Equal(path + ".schema1.bak", backup);
                Assert.Equal(schema1, File.ReadAllText(backup));
                Assert.Equal(LauncherSettings.SchemaVersion, LauncherSettings.SchemaOf(File.ReadAllText(path)));
                Assert.Equal(1.1, LauncherSettings.Load(path).WorldScale, 3);

                // Saved again, at the current schema: the backup stays the older launcher's file, and no other is made.
                new LauncherSettings { WorldScale = 1.15 }.Save(path);
                Assert.Equal(schema1, File.ReadAllText(backup));
                Assert.Equal(new[] { "launcher.ini", "launcher.ini.schema1.bak" }, Directory.GetFiles(t.Combine("data")).Select(Path.GetFileName).OrderBy(n => n, StringComparer.Ordinal));
            }
        }

        [Fact]
        public void ABackupThatCannotBeWrittenDoesNotStopTheSave()
        {
            using (var t = new TempDir())
            {
                const string schema1 = "schema_version = 1\nworld_scale = 1.10\n";
                var path = t.Write("launcher.ini", schema1);
                // A folder where the backup would go: the file cannot be created (as with a full disk or a lock).
                var backup = LauncherSettings.SchemaBackupPath(path, 1);
                Directory.CreateDirectory(backup);
                var s = LauncherSettings.Load(path);
                s.WorldScale = 1.15;
                s.Save(path);
                Assert.Equal(1.15, LauncherSettings.Load(path).WorldScale, 3);
                Assert.True(Directory.Exists(backup));
                // Later saves go on too.
                new LauncherSettings { WorldScale = 1.05 }.Save(path);
                Assert.Equal(1.05, LauncherSettings.Load(path).WorldScale, 3);
            }
        }

        [Fact]
        public void AnExistingBackupIsNeverOverwritten()
        {
            using (var t = new TempDir())
            {
                var path = t.Write("launcher.ini", "schema_version = 1\nworld_scale = 1.10\n");
                var backup = t.Write("launcher.ini.schema1.bak", "an earlier backup");
                LauncherSettings.Load(path).Save(path);
                Assert.Equal("an earlier backup", File.ReadAllText(backup));
                Assert.Equal(LauncherSettings.SchemaVersion, LauncherSettings.SchemaOf(File.ReadAllText(path)));
            }
        }

        [Fact]
        public void AFileWithoutASchemaVersionIsKeptToo()
        {
            using (var t = new TempDir())
            {
                var path = t.Write("launcher.ini", "world_scale = 1.05\n");
                Assert.Equal(0, LauncherSettings.SchemaOf("world_scale = 1.05\n"));
                LauncherSettings.Load(path).Save(path);
                Assert.Equal("world_scale = 1.05\n", File.ReadAllText(LauncherSettings.SchemaBackupPath(path, 0)));
            }
        }

        [Fact]
        public void TheCurrentSchemaMakesNoBackup()
        {
            using (var t = new TempDir())
            {
                var path = t.Combine("launcher.ini");
                new LauncherSettings().Save(path);
                new LauncherSettings { WorldScale = 1.1 }.Save(path);
                Assert.Equal(new[] { "launcher.ini" }, Directory.GetFiles(t.Path).Select(Path.GetFileName));
            }
        }

        [Fact]
        public void ANewerSchemaIsRefusedLeftUnchangedAndNotCopied()
        {
            using (var t = new TempDir())
            {
                var newer = "schema_version = " + (LauncherSettings.SchemaVersion + 1) + "\nworld_scale = 1.1\nfuture_key = 3\n";
                var path = t.Write("launcher.ini", newer);
                var refused = Assert.Throws<SettingsException>(() => LauncherSettings.Load(path));
                Assert.Contains("newer than this launcher understands", refused.Message);
                Assert.Contains("the file was left unchanged", refused.Message);
                Assert.Throws<SettingsException>(() => new LauncherSettings().Save(path));
                Assert.Equal(newer, File.ReadAllText(path));
                Assert.Equal(new[] { "launcher.ini" }, Directory.GetFiles(t.Path).Select(Path.GetFileName));
            }
        }
    }
}
