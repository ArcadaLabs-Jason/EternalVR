using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using EternalVR.Launcher.Core.Game;
using EternalVR.Launcher.Core.Text;

namespace EternalVR.Launcher.Core.Steam
{
    /// <summary>Steam's record of one cloud file: the size and SHA-1 it last synced.</summary>
    public sealed class CloudRecordEntry
    {
        public CloudRecordEntry(long? size, string sha1, string syncState)
        {
            Size = size;
            Sha1 = sha1;
            SyncState = syncState;
        }

        public long? Size { get; }
        /// <summary>Lower-case hex; null when the record has none.</summary>
        public string Sha1 { get; }
        public string SyncState { get; }
    }

    /// <summary>
    /// Steam's record of one app's cloud folder (<c>userdata\&lt;id&gt;\782330\remotecache.vdf</c>).
    /// <see cref="Files"/> maps the path relative to <c>remote\</c> (with the platform's separator) to
    /// its entry; only Auto-Cloud root 0 (the remote folder) is read.
    /// </summary>
    public sealed class CloudRecord
    {
        public CloudRecord(string path) { Path = path; }

        public string Path { get; }
        /// <summary>The file exists and parses.</summary>
        public bool Present { get; internal set; }
        /// <summary>Why a record that exists could not be read; null otherwise.</summary>
        public string Error { get; internal set; }
        public Dictionary<string, CloudRecordEntry> Files { get; } = new Dictionary<string, CloudRecordEntry>(StringComparer.OrdinalIgnoreCase);
    }

    /// <summary>A cloud file on disk whose size or SHA-1 differs from Steam's record.</summary>
    public sealed class StaleCloudFile
    {
        public StaleCloudFile(string key, long fileSize, string fileSha1, long? recordSize, string recordSha1)
        {
            Key = key;
            FileSize = fileSize;
            FileSha1 = fileSha1;
            RecordSize = recordSize;
            RecordSha1 = recordSha1;
        }

        /// <summary><c>&lt;location&gt;/&lt;relative path with /&gt;</c>.</summary>
        public string Key { get; }
        public long FileSize { get; }
        public string FileSha1 { get; }
        public long? RecordSize { get; }
        public string RecordSha1 { get; }

        public override string ToString() =>
            $"{Key} (file {FileSize} bytes, SHA-1 {FileSha1}; Steam's record {RecordSize?.ToString() ?? "?"} bytes, SHA-1 {RecordSha1 ?? "?"})";
    }

    /// <summary>How the cloud files on disk compare with Steam's record.</summary>
    public sealed class CloudConsistency
    {
        /// <summary>Tracked with another size or SHA-1: the game's next start may reset its profile.</summary>
        public List<StaleCloudFile> Stale { get; } = new List<StaleCloudFile>();
        /// <summary>On disk but not in the record: Steam takes them as new at the next app session.</summary>
        public List<string> Untracked { get; } = new List<string>();
        /// <summary>Records that exist but do not parse (a missing record is not listed: nothing to compare).</summary>
        public List<string> Unreadable { get; } = new List<string>();
        /// <summary>Locations whose record was read.</summary>
        public int Checked { get; internal set; }

        public bool Consistent => Stale.Count == 0;
    }

    /// <summary>
    /// Reads Steam's cloud record (T-115). Every file under <c>782330\remote\</c> (the profile and the
    /// save slots) is Steam-Cloud synced, and Steam records each one's size and SHA-1 in
    /// <c>remotecache.vdf</c>; a file put back behind Steam's back leaves that record stale and the game
    /// then reports "Profile corrupt, creating a new one". This class only ever reads Steam's files.
    /// </summary>
    public static class SteamCloudCache
    {
        public const string FileName = "remotecache.vdf";

        /// <summary>The record next to a <c>remote</c> folder: <c>&lt;remote&gt;\..\remotecache.vdf</c>.</summary>
        public static string RecordPathFor(string remoteDir) =>
            System.IO.Path.Combine(System.IO.Path.GetDirectoryName(System.IO.Path.GetFullPath(remoteDir).TrimEnd('\\', '/')), FileName);

        public static CloudRecord Read(string remoteDir)
        {
            var record = new CloudRecord(RecordPathFor(remoteDir));
            if (!File.Exists(record.Path)) return record;
            try { Parse(File.ReadAllText(record.Path), record); }
            catch (Exception e) when (e is VdfFormatException || e is IOException || e is FormatException || e is OverflowException)
            {
                record.Files.Clear();
                record.Error = e.Message;
            }
            return record;
        }

        /// <summary>Parses the text of a record into <paramref name="record"/> and marks it present.</summary>
        public static void Parse(string text, CloudRecord record)
        {
            var root = VdfParser.Parse(text);
            // The top-level block is named after the app ID; its children are the files, with a few
            // plain values (ChangeNumber, OSType) mixed in.
            var app = root.Children.Select(c => c.Value).FirstOrDefault(v => v.Value == null);
            if (app != null)
            {
                foreach (var child in app.Children)
                {
                    var e = child.Value;
                    if (e.Value != null) continue;
                    var rootId = e.GetString("root");
                    if (rootId != null && rootId != "0") continue;
                    var sizeText = e.GetString("size");
                    long? size = sizeText == null ? (long?)null : long.Parse(sizeText, System.Globalization.CultureInfo.InvariantCulture);
                    var sha = e.GetString("sha")?.ToLowerInvariant();
                    record.Files[child.Key.Replace('/', System.IO.Path.DirectorySeparatorChar).Replace('\\', System.IO.Path.DirectorySeparatorChar)] =
                        new CloudRecordEntry(size, sha, e.GetString("syncstate"));
                }
            }
            record.Present = true;
        }

        /// <summary>Compares every file under each Steam location's <c>remote\</c> folder with Steam's record.</summary>
        public static CloudConsistency Check(IEnumerable<SettingsLocation> locations)
        {
            var result = new CloudConsistency();
            foreach (var loc in locations.Where(l => l.Kind == SettingsLocationKind.SteamRemote))
            {
                if (!Directory.Exists(loc.Path)) continue;
                var record = Read(loc.Path);
                if (!record.Present)
                {
                    if (record.Error != null) result.Unreadable.Add($"{record.Path}: {record.Error}");
                    continue;
                }
                result.Checked++;
                var root = System.IO.Path.GetFullPath(loc.Path).TrimEnd('\\', '/');
                foreach (var file in Directory.GetFiles(root, "*", SearchOption.AllDirectories).OrderBy(f => f, StringComparer.OrdinalIgnoreCase))
                {
                    var rel = file.Substring(root.Length + 1);
                    var key = loc.Name + "/" + rel.Replace('\\', '/');
                    if (!record.Files.TryGetValue(rel, out var entry))
                    {
                        result.Untracked.Add(key);
                        continue;
                    }
                    var size = new FileInfo(file).Length;
                    var sha1 = Sha1OfFile(file);
                    if ((entry.Size.HasValue && entry.Size.Value != size) || (entry.Sha1 != null && entry.Sha1 != sha1))
                        result.Stale.Add(new StaleCloudFile(key, size, sha1, entry.Size, entry.Sha1));
                }
            }
            return result;
        }

        /// <summary>
        /// A signature of every file in the app folders holding the given Steam locations (the remote
        /// folders and remotecache.vdf): path, size and write time. Two equal signatures some seconds
        /// apart mean Steam is not syncing the app's cloud files.
        /// </summary>
        public static string SyncSignature(IEnumerable<SettingsLocation> locations)
        {
            var sb = new StringBuilder();
            foreach (var loc in locations.Where(l => l.Kind == SettingsLocationKind.SteamRemote))
            {
                var appDir = System.IO.Path.GetDirectoryName(System.IO.Path.GetFullPath(loc.Path).TrimEnd('\\', '/'));
                if (!Directory.Exists(appDir)) continue;
                foreach (var f in Directory.GetFiles(appDir, "*", SearchOption.AllDirectories).OrderBy(f => f, StringComparer.Ordinal))
                {
                    var info = new FileInfo(f);
                    sb.Append(f).Append('|').Append(info.Length).Append('|').Append(info.LastWriteTimeUtc.Ticks).Append('\n');
                }
            }
            return sb.ToString();
        }

        public static string Sha1OfFile(string path)
        {
            using (var sha = SHA1.Create())
            using (var fs = File.OpenRead(path))
            {
                var hash = sha.ComputeHash(fs);
                var sb = new StringBuilder(hash.Length * 2);
                foreach (var b in hash) sb.Append(b.ToString("x2"));
                return sb.ToString();
            }
        }
    }
}
