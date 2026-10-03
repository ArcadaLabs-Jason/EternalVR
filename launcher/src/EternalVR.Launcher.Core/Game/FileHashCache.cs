using System;
using System.Collections.Generic;
using System.IO;

namespace EternalVR.Launcher.Core.Game
{
    /// <summary>
    /// Hashes of files kept by full path, size and last write time: a file that has not changed is not read again. The
    /// build check hashes the game's 77 MB exe at every preflight, which runs on the window's thread after each change.
    /// Safe to use from several threads.
    /// </summary>
    public sealed class FileHashCache
    {
        private readonly Func<string, string> hash;
        private readonly object sync = new object();
        private readonly Dictionary<string, Entry> entries = new Dictionary<string, Entry>(StringComparer.OrdinalIgnoreCase);

        public FileHashCache(Func<string, string> hash) { this.hash = hash ?? throw new ArgumentNullException(nameof(hash)); }

        /// <summary>The file's hash, read again only when its size or last write time changed since the last read.</summary>
        public string Get(string path)
        {
            var info = new FileInfo(path);
            var key = info.FullName;
            // Taken before the read: a file changed while it is read gets a new stamp, so the next call reads it again.
            long length = info.Length;
            var written = info.LastWriteTimeUtc;
            lock (sync)
            {
                if (entries.TryGetValue(key, out var e) && e.Length == length && e.WrittenUtc == written) return e.Hash;
            }
            var h = hash(path);
            lock (sync) entries[key] = new Entry(length, written, h);
            return h;
        }

        private sealed class Entry
        {
            public Entry(long length, DateTime writtenUtc, string hash)
            {
                Length = length;
                WrittenUtc = writtenUtc;
                Hash = hash;
            }

            public long Length { get; }
            public DateTime WrittenUtc { get; }
            public string Hash { get; }
        }
    }
}
