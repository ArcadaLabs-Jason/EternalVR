using System;
using System.Globalization;
using System.IO;

namespace EternalVR.Launcher.Core.Settings
{
    /// <summary>Reading and writing launcher.ini, and the backup of an older schema's file (T-093, T-106).</summary>
    public sealed partial class LauncherSettings
    {
        public static LauncherSettings Load(string path) =>
            File.Exists(path) ? Parse(File.ReadAllText(path)) : new LauncherSettings();

        /// <summary>
        /// Writes atomically (temporary file, then replace). An older launcher's file (a lower <c>schema_version</c>, or none)
        /// is first copied to <see cref="SchemaBackupPath"/>, once (T-093): the migration to this schema drops or reads anew
        /// what the older one wrote, and the copy keeps the file as that launcher left it. A copy that cannot be written
        /// does not stop the save.
        /// </summary>
        public void Save(string path)
        {
            if (File.Exists(path))
            {
                var old = File.ReadAllText(path);
                // Never overwrite a file from a newer launcher.
                Parse(old);
                KeepOlderSchema(path, old);
            }
            Directory.CreateDirectory(Path.GetDirectoryName(path));
            FileUtil.WriteAllTextAtomic(path, Serialize());
        }

        /// <summary>Where an older launcher's settings file is kept the first time this one saves over it: <c>launcher.ini.schema1.bak</c>.</summary>
        public static string SchemaBackupPath(string path, int schemaVersion) =>
            path + ".schema" + schemaVersion.ToString(CultureInfo.InvariantCulture) + ".bak";

        /// <summary>The file's <c>schema_version</c>; 0 without one (read as the oldest), and for one that is not a number.</summary>
        public static int SchemaOf(string text) =>
            ReadMap(text).TryGetValue("schema_version", out var v) && int.TryParse(v, NumberStyles.Integer, CultureInfo.InvariantCulture, out var n) ? n : 0;

        /// <summary>
        /// Writes the older file's text to its backup, only when no backup is there (created new: an existing one, even one
        /// that appears meanwhile, is never overwritten). A backup that cannot be written (a full disk, a read-only folder,
        /// an antivirus lock) does not stop the save: the migration goes on without it. False when none was written.
        /// </summary>
        internal static bool KeepOlderSchema(string path, string text)
        {
            int version = SchemaOf(text);
            if (version >= SchemaVersion) return false;
            var backup = SchemaBackupPath(path, version);
            bool created = false;
            try
            {
                using (var stream = new FileStream(backup, FileMode.CreateNew, FileAccess.Write, FileShare.None))
                {
                    created = true;
                    using (var writer = new StreamWriter(stream, new System.Text.UTF8Encoding(false)))
                        writer.Write(text);
                }
                return true;
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException)
            {
                // A backup this call began but could not finish is removed, so a later save can try again.
                if (created)
                {
                    try { File.Delete(backup); }
                    catch (Exception d) when (d is IOException || d is UnauthorizedAccessException) { }
                }
                return false;
            }
        }
    }
}
