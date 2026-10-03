using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Linq;
using EternalVR.Launcher.Core.Game;

namespace EternalVR.Launcher.Core.Update
{
    /// <summary>
    /// Installs a downloaded release zip over the running launcher's folder. <see cref="Stage"/> unpacks it into a folder
    /// of its own and checks every file against the zip's <c>SHA256SUMS.txt</c>; <see cref="Install"/> then moves each
    /// file it replaces aside (<c>*.evr-old</c>: Windows lets a running exe or a loaded DLL be renamed, not overwritten)
    /// and copies the new one in, putting everything back if a step fails. A file the previous release shipped (its
    /// <c>SHA256SUMS.txt</c> in the program folder) and the new one does not is moved aside the same way. The data folder
    /// is never touched.
    /// <see cref="RemoveOldFiles"/> deletes the moved-aside files once the new launcher runs.
    /// </summary>
    public static class UpdatePackage
    {
        public const string SumsFile = "SHA256SUMS.txt";
        public const string OldSuffix = ".evr-old";
        /// <summary>Files a release must hold (relative to its folder).</summary>
        public static readonly string[] Required = { "EternalVR.Launcher.exe", "EternalVR.Launcher.Core.dll", Path.Combine("layer", "EternalVR.dll") };

        /// <summary>
        /// Unpacks <paramref name="zipPath"/> into <paramref name="stagingDir"/> (emptied first), dropping the zip's one top
        /// folder, and checks it: every entry stays inside, every file is listed in <see cref="SumsFile"/> with its hash, the
        /// <see cref="Required"/> files are there and the launcher's file version is <paramref name="expected"/>. Throws
        /// <see cref="IOException"/> (or <see cref="InvalidDataException"/> for a broken zip) saying what is wrong.
        /// </summary>
        public static void Stage(string zipPath, string stagingDir, Version expected)
        {
            FileUtil.DeleteDirectory(stagingDir);
            Directory.CreateDirectory(stagingDir);
            var root = Path.GetFullPath(stagingDir).TrimEnd('\\', '/') + Path.DirectorySeparatorChar;
            using (var zip = ZipFile.OpenRead(zipPath))
            {
                var files = zip.Entries.Where(e => !e.FullName.EndsWith("/", StringComparison.Ordinal) && !e.FullName.EndsWith("\\", StringComparison.Ordinal)).ToList();
                if (files.Count == 0) throw new IOException("the update zip is empty");
                var top = TopFolder(files.Select(e => e.FullName.Replace('\\', '/')));
                foreach (var e in files)
                {
                    var rel = e.FullName.Replace('\\', '/').Substring(top.Length);
                    if (rel.Length == 0 || rel.Contains(":") || rel.StartsWith("/", StringComparison.Ordinal) || rel.Split('/').Any(p => p == ".." || p == "."))
                        throw new IOException("the update zip holds a path outside its folder: " + e.FullName);
                    // A name Windows cannot use (a character the path functions refuse) is a bad zip like any other: an IOException.
                    try
                    {
                        var target = Path.GetFullPath(Path.Combine(stagingDir, rel.Replace('/', Path.DirectorySeparatorChar)));
                        if (!target.StartsWith(root, StringComparison.OrdinalIgnoreCase))
                            throw new IOException("the update zip holds a path outside its folder: " + e.FullName);
                        Directory.CreateDirectory(Path.GetDirectoryName(target));
                        e.ExtractToFile(target, overwrite: false);
                    }
                    catch (Exception x) when (x is ArgumentException || x is NotSupportedException)
                    {
                        throw new IOException("the update zip holds a file name that cannot be used: " + e.FullName, x);
                    }
                }
            }
            Verify(stagingDir, expected);
        }

        /// <summary>The checks of <see cref="Stage"/> on an unpacked release folder.</summary>
        public static void Verify(string dir, Version expected)
        {
            var sumsPath = Path.Combine(dir, SumsFile);
            if (!File.Exists(sumsPath)) throw new IOException("the update has no " + SumsFile);
            var listed = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            foreach (var line in File.ReadAllLines(sumsPath))
            {
                var t = line.Trim();
                if (t.Length == 0) continue;
                int space = t.IndexOf(' ');
                if (space != 64) throw new IOException("the update's " + SumsFile + " has a bad line: " + t);
                listed[Normalise(t.Substring(space).Trim().TrimStart('*'))] = t.Substring(0, 64);
            }
            foreach (var file in Directory.GetFiles(dir, "*", SearchOption.AllDirectories))
            {
                var rel = Normalise(file.Substring(dir.TrimEnd('\\', '/').Length + 1));
                if (string.Equals(rel, SumsFile, StringComparison.OrdinalIgnoreCase)) continue;
                if (!listed.TryGetValue(rel, out var hash)) throw new IOException("the update holds a file its " + SumsFile + " does not list: " + rel);
                if (!string.Equals(KnownBuilds.Sha256OfFile(file), hash, StringComparison.OrdinalIgnoreCase))
                    throw new IOException("a file of the update does not match its " + SumsFile + ": " + rel);
                listed.Remove(rel);
            }
            if (listed.Count > 0) throw new IOException("the update is missing " + string.Join(", ", listed.Keys));
            foreach (var r in Required)
                if (!File.Exists(Path.Combine(dir, r))) throw new IOException("the update has no " + r);
            if (expected != null)
            {
                var info = FileVersionInfo.GetVersionInfo(Path.Combine(dir, Required[0]));
                var got = new Version(info.FileMajorPart, info.FileMinorPart, info.FileBuildPart);
                if (got != ReleaseFeed.Plain(expected)) throw new IOException($"the update's launcher is version {got}, not {ReleaseFeed.Plain(expected)}");
            }
        }

        /// <summary>
        /// Copies every file of <paramref name="stagingDir"/> into <paramref name="programDir"/>, moving each file it replaces
        /// aside first. On a failure every change is undone and the error passed on. Returns the files installed.
        /// </summary>
        public static IReadOnlyList<string> Install(string stagingDir, string programDir) => Install(stagingDir, programDir, out _);

        /// <summary>
        /// <see cref="Install(string, string)"/>, and the files the release installed before shipped (the program folder's
        /// own <see cref="SumsFile"/>) that the new release no longer has are moved aside like replaced ones, so a dropped
        /// controller map or data file does not stay behind; <paramref name="removed"/> names them. A file no release listed
        /// (the player's own) is never touched. On a failure these are put back too.
        /// </summary>
        public static IReadOnlyList<string> Install(string stagingDir, string programDir, out IReadOnlyList<string> removed)
        {
            var done = new List<KeyValuePair<string, string>>();
            var installed = new List<string>();
            // Read before the copy, which replaces it with the new release's list.
            var shippedBefore = ShippedFiles(programDir);
            try
            {
                foreach (var file in Directory.GetFiles(stagingDir, "*", SearchOption.AllDirectories).OrderBy(f => f, StringComparer.OrdinalIgnoreCase))
                {
                    var rel = file.Substring(stagingDir.TrimEnd('\\', '/').Length + 1);
                    var target = Path.Combine(programDir, rel);
                    Directory.CreateDirectory(Path.GetDirectoryName(target));
                    string old = null;
                    if (File.Exists(target))
                    {
                        old = FreeOldName(target);
                        File.Move(target, old);
                    }
                    done.Add(new KeyValuePair<string, string>(target, old));
                    File.Copy(file, target, overwrite: false);
                    installed.Add(rel);
                }

                var now = new HashSet<string>(installed.Select(Normalise), StringComparer.OrdinalIgnoreCase);
                var dropped = new List<string>();
                foreach (var rel in shippedBefore.Where(r => !now.Contains(r)))
                {
                    var target = Path.Combine(programDir, rel);
                    if (!File.Exists(target)) continue;
                    var old = FreeOldName(target);
                    File.Move(target, old);
                    done.Add(new KeyValuePair<string, string>(target, old));
                    dropped.Add(rel);
                }
                removed = dropped;
                return installed;
            }
            catch
            {
                for (int i = done.Count - 1; i >= 0; i--)
                {
                    var (target, old) = (done[i].Key, done[i].Value);
                    try
                    {
                        if (File.Exists(target) && (old == null || File.Exists(old))) File.Delete(target);
                        if (old != null && File.Exists(old)) File.Move(old, target);
                    }
                    catch (Exception e) when (e is IOException || e is UnauthorizedAccessException) { }
                }
                throw;
            }
        }

        /// <summary>
        /// The files the release in <paramref name="programDir"/> shipped: the paths its <see cref="SumsFile"/> lists, relative
        /// and inside the folder (a line that is not a sum or names a path outside is skipped). Empty without the file.
        /// </summary>
        public static IReadOnlyList<string> ShippedFiles(string programDir)
        {
            var list = new List<string>();
            var sums = Path.Combine(programDir, SumsFile);
            if (!File.Exists(sums)) return list;
            var root = Path.GetFullPath(programDir).TrimEnd('\\', '/') + Path.DirectorySeparatorChar;
            foreach (var line in File.ReadAllLines(sums))
            {
                var t = line.Trim();
                if (t.Length < 66 || t.IndexOf(' ') != 64 || !KnownBuilds.IsSha256Hex(t.Substring(0, 64))) continue;
                var raw = t.Substring(64).Trim().TrimStart('*');
                // A rooted path (\\server\share, \x, /x, C:\x) or a stream (a.txt:b) is never one of the release's files.
                if (raw.StartsWith("/", StringComparison.Ordinal) || raw.StartsWith("\\", StringComparison.Ordinal) || raw.Contains(":")) continue;
                var rel = Normalise(raw);
                if (rel.Length == 0 || string.Equals(rel, SumsFile, StringComparison.OrdinalIgnoreCase)
                    || rel.Split(Path.DirectorySeparatorChar).Any(p => p == ".." || p == "." || p.Length == 0))
                    continue;
                try
                {
                    if (!Path.GetFullPath(Path.Combine(programDir, rel)).StartsWith(root, StringComparison.OrdinalIgnoreCase)) continue;
                }
                catch (Exception e) when (e is ArgumentException || e is NotSupportedException || e is PathTooLongException) { continue; }
                list.Add(rel);
            }
            return list;
        }

        /// <summary>Deletes the files an update moved aside; returns how many are left (still in use).</summary>
        public static int RemoveOldFiles(string programDir)
        {
            int left = 0;
            if (!Directory.Exists(programDir)) return 0;
            foreach (var f in Directory.GetFiles(programDir, "*" + OldSuffix + "*", SearchOption.AllDirectories))
            {
                try
                {
                    File.SetAttributes(f, FileAttributes.Normal);
                    File.Delete(f);
                }
                catch (Exception e) when (e is IOException || e is UnauthorizedAccessException) { left++; }
            }
            return left;
        }

        /// <summary><c>&lt;file&gt;.evr-old</c>, or <c>.evr-old2</c> and on when an older one is still in use.</summary>
        private static string FreeOldName(string target)
        {
            for (int i = 1; ; i++)
            {
                var name = target + OldSuffix + (i == 1 ? string.Empty : i.ToString(System.Globalization.CultureInfo.InvariantCulture));
                if (!File.Exists(name)) return name;
                try
                {
                    File.SetAttributes(name, FileAttributes.Normal);
                    File.Delete(name);
                    return name;
                }
                catch (Exception e) when (e is IOException || e is UnauthorizedAccessException) { }
            }
        }

        /// <summary>The one folder every entry is in (<c>EternalVR-alpha-0.1.5-abc1234/</c>), or empty.</summary>
        private static string TopFolder(IEnumerable<string> names)
        {
            var list = names.ToList();
            int slash = list[0].IndexOf('/');
            if (slash <= 0) return string.Empty;
            var top = list[0].Substring(0, slash + 1);
            return list.All(n => n.StartsWith(top, StringComparison.Ordinal)) ? top : string.Empty;
        }

        private static string Normalise(string rel) =>
            rel.Replace('/', Path.DirectorySeparatorChar).Replace('\\', Path.DirectorySeparatorChar).TrimStart(Path.DirectorySeparatorChar);
    }
}
