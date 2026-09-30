using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using EternalVR.Launcher.Core.Game;

namespace EternalVR.Launcher.Core
{
    public static class FileUtil
    {
        private static readonly Encoding Utf8NoBom = new UTF8Encoding(false);
        private const string ExtendedPrefix = @"\\?\";

        /// <summary>
        /// The path in Windows' extended-length form (<c>\\?\C:\...</c>, <c>\\?\UNC\server\share\...</c>), which the
        /// file functions take beyond the classic 260-character limit whether or not long paths are turned on in
        /// Windows. A data folder deep in the disk plus a Game Pass save container's names passes that limit. Other
        /// systems, and a path already in that form, are returned as they are.
        /// </summary>
        public static string Long(string path)
        {
            if (string.IsNullOrEmpty(path) || Path.DirectorySeparatorChar != '\\' || path.StartsWith(ExtendedPrefix, StringComparison.Ordinal))
                return path;
            var full = Path.GetFullPath(path);
            if (full.StartsWith(@"\\", StringComparison.Ordinal)) return ExtendedPrefix + @"UNC\" + full.Substring(2);
            return ExtendedPrefix + full;
        }

        /// <summary>Writes a temporary file next to the target, then moves it into place.</summary>
        public static void WriteAllTextAtomic(string path, string text) => WriteAllBytesAtomic(path, Utf8NoBom.GetBytes(text));

        /// <summary>
        /// Writes a temporary file next to the target, flushes it to the disk, then moves it into place,
        /// so a power loss leaves either the old or the new file, never a torn or empty one.
        /// </summary>
        public static void WriteAllBytesAtomic(string path, byte[] bytes)
        {
            path = Long(path);
            var tmp = path + ".evr-tmp";
            RemoveStaleTemp(tmp);
            using (var fs = new FileStream(tmp, FileMode.Create, FileAccess.Write, FileShare.None, 4096, FileOptions.WriteThrough))
            {
                fs.Write(bytes, 0, bytes.Length);
                fs.Flush(true);
            }
            ReplaceWith(tmp, path);
        }

        /// <summary>
        /// Copies <paramref name="source"/> over <paramref name="target"/> through a temporary file and
        /// checks the SHA-256 of the result. Throws if the check fails.
        /// </summary>
        public static void CopyVerified(string source, string target)
        {
            source = Long(source);
            target = Long(target);
            Directory.CreateDirectory(Path.GetDirectoryName(target));
            var expected = KnownBuilds.Sha256OfFile(source);
            var tmp = target + ".evr-tmp";
            RemoveStaleTemp(tmp);
            File.Copy(source, tmp, overwrite: false);
            FlushToDisk(tmp);
            ReplaceWith(tmp, target);
            var actual = KnownBuilds.Sha256OfFile(target);
            if (!string.Equals(expected, actual, StringComparison.OrdinalIgnoreCase))
                throw new IOException($"copy of {source} to {target} did not verify");
        }

        /// <summary>
        /// Deletes a folder tree we own, clearing read-only attributes first (copies of read-only
        /// configs and saves keep that attribute, and a plain recursive delete fails on them).
        /// </summary>
        public static void DeleteDirectory(string dir)
        {
            dir = Long(dir);
            if (!Directory.Exists(dir)) return;
            foreach (var f in Directory.GetFiles(dir, "*", SearchOption.AllDirectories))
            {
                var a = File.GetAttributes(f);
                if ((a & FileAttributes.ReadOnly) != 0) File.SetAttributes(f, a & ~FileAttributes.ReadOnly);
            }
            Directory.Delete(dir, recursive: true);
        }

        /// <summary>A temporary file left by an interrupted write (it may be read-only as a copy of a read-only file).</summary>
        private static void RemoveStaleTemp(string tmp)
        {
            if (!File.Exists(tmp)) return;
            File.SetAttributes(tmp, FileAttributes.Normal);
            File.Delete(tmp);
        }

        private static void FlushToDisk(string path)
        {
            var a = File.GetAttributes(path);
            bool readOnly = (a & FileAttributes.ReadOnly) != 0;
            if (readOnly) File.SetAttributes(path, a & ~FileAttributes.ReadOnly);
            using (var fs = new FileStream(path, FileMode.Open, FileAccess.ReadWrite, FileShare.Read))
                fs.Flush(true);
            if (readOnly) File.SetAttributes(path, File.GetAttributes(path) | FileAttributes.ReadOnly);
        }

        private static void ReplaceWith(string tmp, string target)
        {
            if (File.Exists(target))
            {
                // Preserve read-only attributes as found, but allow the replace.
                var attrs = File.GetAttributes(target);
                if ((attrs & FileAttributes.ReadOnly) != 0) File.SetAttributes(target, attrs & ~FileAttributes.ReadOnly);
                File.Replace(tmp, target, null);
                if ((attrs & FileAttributes.ReadOnly) != 0) File.SetAttributes(target, File.GetAttributes(target) | FileAttributes.ReadOnly);
            }
            else
            {
                File.Move(tmp, target);
            }
        }

        /// <summary>The names of the files matching <paramref name="pattern"/> directly in <paramref name="dir"/>; empty when it cannot be read.</summary>
        public static IReadOnlyList<string> FileNames(string dir, string pattern)
        {
            try { return Directory.Exists(dir) ? Directory.GetFiles(dir, pattern).Select(Path.GetFileName).ToList() : new List<string>(); }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException) { return new List<string>(); }
        }

        /// <summary>True when a file can be created and deleted in the folder (created if missing).</summary>
        public static bool IsWritableDirectory(string dir, out string error)
        {
            error = null;
            try
            {
                Directory.CreateDirectory(dir);
                var probe = Path.Combine(dir, ".write-probe-" + Guid.NewGuid().ToString("N"));
                File.WriteAllText(probe, "probe");
                File.Delete(probe);
                return true;
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException || e is NotSupportedException)
            {
                error = e.Message;
                return false;
            }
        }
    }
}
