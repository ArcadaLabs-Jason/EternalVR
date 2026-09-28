using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using EternalVR.Launcher.Core.Preflight;

namespace EternalVR.Launcher.Core.Report
{
    /// <summary>What the Windows side hands over for a report: folders, machine facts, checks and who the user is.</summary>
    public sealed class ReportInputs
    {
        public string DataRoot { get; set; }
        public string ProgramDir { get; set; }
        public string LayerDir { get; set; }
        /// <summary>The lines of <see cref="ReportManifest.SystemFile"/>, in order.</summary>
        public IReadOnlyList<KeyValuePair<string, string>> System { get; set; } = new KeyValuePair<string, string>[0];
        public IReadOnlyList<Check> Preflight { get; set; } = new Check[0];
        /// <summary>For redaction: the profile folder, the user name and the Steam account IDs known.</summary>
        public string UserProfile { get; set; }
        public string UserName { get; set; }
        public IReadOnlyList<string> SteamAccountIds { get; set; } = new string[0];
        public DateTime Now { get; set; } = DateTime.Now;
    }

    public sealed class ReportFile
    {
        public ReportFile(string zipPath, string text, long originalBytes, bool truncated)
        {
            ZipPath = zipPath;
            Text = text;
            OriginalBytes = originalBytes;
            Truncated = truncated;
        }

        public string ZipPath { get; }
        /// <summary>The redacted text that goes into the zip.</summary>
        public string Text { get; internal set; }
        /// <summary>The size of the source file (for generated files, of the text).</summary>
        public long OriginalBytes { get; }
        public bool Truncated { get; }
        public long Bytes => Binary != null ? Binary.LongLength : Encoding.UTF8.GetByteCount(Text);

        /// <summary>A capture image (<see cref="ReportSource.SessionCaptures"/>): zipped as it is, never redacted; <see cref="Text"/> is empty.</summary>
        public ReportFile(string zipPath, byte[] binary) : this(zipPath, string.Empty, binary.LongLength, false) => Binary = binary;

        public byte[] Binary { get; }
    }

    /// <summary>A report ready to be saved: its files, what was left out, and the zip itself.</summary>
    public sealed class ReportResult
    {
        internal ReportResult(IReadOnlyList<ReportFile> files, IReadOnlyList<string> dropped, byte[] zip)
        {
            Files = files;
            Dropped = dropped;
            Zip = zip;
        }

        public IReadOnlyList<ReportFile> Files { get; }
        /// <summary>One line per file or group left out, with the reason.</summary>
        public IReadOnlyList<string> Dropped { get; }
        public byte[] Zip { get; }
        public long UncompressedBytes => Files.Sum(f => f.Bytes);

        /// <summary>The file list and sizes, for the confirmation dialog.</summary>
        public string Describe()
        {
            var sb = new StringBuilder();
            foreach (var f in Files) sb.AppendLine($"{f.ZipPath}  ({ReportBuilder.Size(f.Bytes)}{(f.Truncated ? ", shortened" : string.Empty)})");
            sb.AppendLine();
            sb.AppendLine($"{Files.Count} files, {ReportBuilder.Size(UncompressedBytes)} of text; the zip is {ReportBuilder.Size(Zip.LongLength)}.");
            foreach (var d in Dropped) sb.AppendLine("Left out: " + d);
            return sb.ToString();
        }
    }

    /// <summary>Collects, redacts and zips a report (ROADMAP M4.5 "Export report", T-095) from <see cref="ReportManifest"/>.</summary>
    public static class ReportBuilder
    {
        private static readonly Regex SessionFolderName = new Regex(@"^\d{8}-\d{6}(-\d+)?$", RegexOptions.CultureInvariant);
        private static readonly UTF8Encoding Utf8 = new UTF8Encoding(false);

        public static string DefaultFileName(DateTime now) => "EternalVR-report-" + now.ToString("yyyy-MM-dd", CultureInfo.InvariantCulture) + ".zip";

        public static ReportResult Build(ReportInputs inputs)
        {
            if (inputs == null) throw new ArgumentNullException(nameof(inputs));
            var files = new List<ReportFile>();
            var dropped = new List<string>();
            var missing = new List<string>();
            var logs = inputs.DataRoot == null ? null : Path.Combine(inputs.DataRoot, "logs");
            var sessions = NewestSessions(logs, int.MaxValue);

            foreach (var item in ReportManifest.Items)
            {
                switch (item.Source)
                {
                    case ReportSource.Generated:
                        var text = item.ZipPath == ReportManifest.SystemFile ? SystemText(inputs) : PreflightText(inputs);
                        files.Add(new ReportFile(item.ZipPath, text, Utf8.GetByteCount(text), false));
                        break;
                    case ReportSource.SessionCaptures:
                        AddCaptures(files, dropped, logs, sessions.Take(item.Sessions), item);
                        break;
                    case ReportSource.SessionFolder:
                        foreach (var s in sessions.Take(item.Sessions))
                        {
                            var matches = SafeFiles(Path.Combine(logs, s), item.Pattern);
                            foreach (var path in matches)
                                AddFile(files, dropped, path, item, item.ZipPath.Replace("{session}", s).Replace("{name}", Path.GetFileName(path)));
                        }
                        break;
                    default:
                        var root = item.Source == ReportSource.DataFolder ? inputs.DataRoot
                            : item.Source == ReportSource.ProgramFolder ? inputs.ProgramDir : inputs.LayerDir;
                        var full = root == null ? null
                            : Path.Combine(root, item.Pattern.Replace('\\', Path.DirectorySeparatorChar)); // manifest paths use '\'
                        if (full == null || !File.Exists(full)) { missing.Add(item.ZipPath); break; }
                        AddFile(files, dropped, full, item, item.ZipPath);
                        break;
                }
            }

            if (sessions.Count > ReportManifest.SessionsKept)
                dropped.Add($"{sessions.Count - ReportManifest.SessionsKept} older session folder(s) (only the newest {ReportManifest.SessionsKept} are taken)");
            int dumps = sessions.Sum(s => SafeFiles(Path.Combine(logs, s), "*.dmp").Count);
            if (dumps > 0) dropped.Add($"{dumps} memory dump(s) (*.dmp): never included");

            // Redaction first (an account ID found in one file is removed from all), then the size cap.
            var ids = (inputs.SteamAccountIds ?? new string[0]).Concat(Redactor.FindSteamAccountIds(files.Select(f => f.Text)));
            var redactor = new Redactor(inputs.UserProfile, inputs.UserName, ids);
            foreach (var f in files) f.Text = redactor.Apply(f.Text);

            var kept = new List<ReportFile>();
            long total = 0;
            foreach (var f in files)
            {
                if (f.Binary == null && total + f.Bytes > ReportManifest.TotalCapBytes) // captures have their own cap
                {
                    dropped.Add($"{f.ZipPath} ({Size(f.Bytes)}): the report would pass {Size(ReportManifest.TotalCapBytes)}");
                    continue;
                }
                total += f.Binary == null ? f.Bytes : 0;
                kept.Add(f);
            }

            var contents = new ReportFile(ReportManifest.ContentsFile, redactor.Apply(ContentsText(inputs, kept, dropped, missing)), 0, false);
            kept.Insert(0, contents);
            return new ReportResult(kept, dropped, Zip(kept, inputs.Now));
        }

        /// <summary>The session log folders (<c>yyyyMMdd-HHmmss[-n]</c>), newest first.</summary>
        public static IReadOnlyList<string> NewestSessions(string logsDir, int count)
        {
            if (logsDir == null || !Directory.Exists(logsDir)) return new string[0];
            return Directory.GetDirectories(logsDir)
                .Select(Path.GetFileName)
                .Where(n => SessionFolderName.IsMatch(n))
                .OrderByDescending(n => n.Substring(0, 15), StringComparer.Ordinal)
                .ThenByDescending(n => n.Length > 16 ? int.Parse(n.Substring(16), CultureInfo.InvariantCulture) : 1)
                .Take(count)
                .ToList();
        }

        /// <summary>
        /// Reads a text file that may still be open for writing. A file longer than <paramref name="headBytes"/> +
        /// <paramref name="tailBytes"/> (both nonzero or both zero for all) keeps its first and last whole lines within them,
        /// with a marker line where the middle was left out.
        /// </summary>
        public static string ReadSlice(string path, long headBytes, long tailBytes, out long length, out bool truncated)
        {
            using (var s = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
            {
                length = s.Length;
                truncated = (headBytes > 0 || tailBytes > 0) && length > headBytes + tailBytes;
                if (!truncated) return Utf8.GetString(ReadBytes(s, 0, length));

                var head = Utf8.GetString(ReadBytes(s, 0, headBytes));
                int lastNl = head.LastIndexOf('\n');
                head = lastNl >= 0 ? head.Substring(0, lastNl + 1) : string.Empty;
                var tail = Utf8.GetString(ReadBytes(s, length - tailBytes, tailBytes));
                int firstNl = tail.IndexOf('\n');
                tail = firstNl >= 0 ? tail.Substring(firstNl + 1) : tail;
                long omitted = length - Utf8.GetByteCount(head) - Utf8.GetByteCount(tail);
                return head + $"[... {omitted.ToString(CultureInfo.InvariantCulture)} bytes left out of the report here ...]\n" + tail;
            }
        }

        internal static string Size(long bytes) =>
            bytes >= 1024 * 1024 ? (bytes / 1048576.0).ToString("0.0", CultureInfo.InvariantCulture) + " MB"
            : bytes >= 1024 ? (bytes / 1024.0).ToString("0", CultureInfo.InvariantCulture) + " KB"
            : bytes.ToString(CultureInfo.InvariantCulture) + " bytes";

        private static byte[] ReadBytes(Stream s, long offset, long count)
        {
            var buffer = new byte[count];
            s.Seek(offset, SeekOrigin.Begin);
            int read = 0;
            while (read < count)
            {
                int n = s.Read(buffer, read, (int)(count - read));
                if (n == 0) break;
                read += n;
            }
            if (read < count) Array.Resize(ref buffer, read);
            return buffer;
        }

        private static void AddFile(List<ReportFile> files, List<string> dropped, string path, ReportItem item, string zipPath)
        {
            try
            {
                var text = ReadSlice(path, item.HeadBytes, item.TailBytes, out var length, out var truncated);
                files.Add(new ReportFile(zipPath, text, length, truncated));
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException)
            {
                dropped.Add($"{zipPath}: could not be read ({e.GetType().Name})");
            }
        }

        /// <summary>
        /// The captures of <paramref name="sessions"/> (newest first): each capture's files (one name stem: -L.png, -R.png,
        /// -UI.png or -mono.png, and .txt) are taken whole while they fit in <see cref="ReportManifest.CapturesCapBytes"/>.
        /// </summary>
        private static void AddCaptures(List<ReportFile> files, List<string> dropped, string logs, IEnumerable<string> sessions, ReportItem item)
        {
            long total = 0;
            int left = 0;
            foreach (var s in sessions)
            {
                var dir = Path.Combine(logs, s, "captures");
                var groups = SafeFiles(dir, item.Pattern)
                    .GroupBy(p => CaptureStem(Path.GetFileName(p)), StringComparer.OrdinalIgnoreCase)
                    .OrderByDescending(g => g.Key, StringComparer.Ordinal); // the names start with the time
                foreach (var g in groups)
                {
                    long bytes = g.Sum(p => { try { return new FileInfo(p).Length; } catch (IOException) { return 0L; } });
                    if (total + bytes > ReportManifest.CapturesCapBytes) { left++; continue; }
                    total += bytes;
                    foreach (var path in g)
                    {
                        var zipPath = item.ZipPath.Replace("{session}", s).Replace("{name}", Path.GetFileName(path));
                        if (path.EndsWith(".txt", StringComparison.OrdinalIgnoreCase)) { AddFile(files, dropped, path, item, zipPath); continue; }
                        try { files.Add(new ReportFile(zipPath, File.ReadAllBytes(path))); }
                        catch (Exception e) when (e is IOException || e is UnauthorizedAccessException) { dropped.Add($"{zipPath}: could not be read ({e.GetType().Name})"); }
                    }
                }
            }
            if (left > 0) dropped.Add($"{left} older capture(s): the captures would pass {Size(ReportManifest.CapturesCapBytes)}");
        }

        /// <summary><c>capture-20260927-153012-p000123-t4567-L.png</c> to <c>capture-20260927-153012-p000123-t4567</c>.</summary>
        internal static string CaptureStem(string name)
        {
            var stem = Path.GetFileNameWithoutExtension(name);
            foreach (var suffix in new[] { "-L", "-R", "-UI", "-mono" })
                if (stem.EndsWith(suffix, StringComparison.OrdinalIgnoreCase)) return stem.Substring(0, stem.Length - suffix.Length);
            return stem;
        }

        private static IReadOnlyList<string> SafeFiles(string dir, string pattern)
        {
            try { return Directory.Exists(dir) ? Directory.GetFiles(dir, pattern).OrderBy(p => p, StringComparer.OrdinalIgnoreCase).ToList() : new List<string>(); }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException) { return new List<string>(); }
        }

        private static string SystemText(ReportInputs inputs)
        {
            var sb = new StringBuilder();
            sb.Append("EternalVR report, created ").Append(inputs.Now.ToString("yyyy-MM-dd HH:mm:ss", CultureInfo.InvariantCulture)).Append('\n');
            foreach (var kv in inputs.System ?? new KeyValuePair<string, string>[0])
                sb.Append(kv.Key).Append(": ").Append(string.IsNullOrWhiteSpace(kv.Value) ? "unknown" : kv.Value.Trim()).Append('\n');
            return sb.ToString();
        }

        private static string PreflightText(ReportInputs inputs)
        {
            var sb = new StringBuilder();
            foreach (var c in inputs.Preflight ?? new Check[0]) sb.Append(c).Append('\n');
            return sb.Length == 0 ? "no checks were run\n" : sb.ToString();
        }

        private static string ContentsText(ReportInputs inputs, IReadOnlyList<ReportFile> kept, IReadOnlyList<string> dropped, IReadOnlyList<string> missing)
        {
            var sb = new StringBuilder();
            sb.Append("EternalVR report, created ").Append(inputs.Now.ToString("yyyy-MM-dd HH:mm:ss", CultureInfo.InvariantCulture)).Append('\n');
            sb.Append("Every file is redacted: your user folder reads ").Append(Redactor.ProfileToken).Append(", your user name ").Append(Redactor.UserToken)
              .Append(", Steam account IDs ").Append(Redactor.SteamIdToken).Append(" or ").Append(Redactor.SteamId64Token).Append(".\n\n");
            sb.Append("Files:\n");
            foreach (var f in kept)
            {
                sb.Append("  ").Append(f.ZipPath).Append("  ").Append(Size(f.Bytes));
                if (f.Truncated) sb.Append(" (shortened from ").Append(Size(f.OriginalBytes)).Append(')');
                sb.Append('\n');
            }
            if (missing.Count > 0) sb.Append("Not found: ").Append(string.Join(", ", missing)).Append('\n');
            foreach (var d in dropped) sb.Append("Left out: ").Append(d).Append('\n');
            sb.Append(ReportManifest.NeverIncluded).Append('\n');
            return sb.ToString();
        }

        private static byte[] Zip(IReadOnlyList<ReportFile> files, DateTime now)
        {
            using (var ms = new MemoryStream())
            {
                using (var zip = new ZipArchive(ms, ZipArchiveMode.Create, leaveOpen: true))
                {
                    foreach (var f in files)
                    {
                        var entry = zip.CreateEntry(f.ZipPath, CompressionLevel.Optimal);
                        entry.LastWriteTime = new DateTimeOffset(now);
                        using (var s = entry.Open())
                        {
                            var bytes = f.Binary ?? Utf8.GetBytes(f.Text);
                            s.Write(bytes, 0, bytes.Length);
                        }
                    }
                }
                return ms.ToArray();
            }
        }
    }
}
