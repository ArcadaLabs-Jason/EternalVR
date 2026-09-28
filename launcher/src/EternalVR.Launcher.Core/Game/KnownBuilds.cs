using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using EternalVR.Launcher.Core.Data;

namespace EternalVR.Launcher.Core.Game
{
    public sealed class KnownBuild
    {
        public KnownBuild(string sha256, string buildId, string description)
        {
            Sha256 = sha256.ToLowerInvariant();
            BuildId = buildId;
            Description = description;
        }

        public string Sha256 { get; }
        public string BuildId { get; }
        public string Description { get; }
    }

    public enum BuildStatus { Known, Unknown, Missing }

    public sealed class BuildCheck
    {
        public BuildCheck(BuildStatus status, string sha256, KnownBuild build)
        {
            Status = status;
            Sha256 = sha256;
            Build = build;
        }

        public BuildStatus Status { get; }
        public string Sha256 { get; }
        public KnownBuild Build { get; }
    }

    /// <summary>
    /// The exe hashes the layer supports (T-094), kept as data in <c>data\known-builds.txt</c>:
    /// <c>sha256 | steam build id | description</c>.
    /// </summary>
    public sealed class KnownBuilds
    {
        private readonly List<KnownBuild> builds;

        public KnownBuilds(IEnumerable<KnownBuild> builds) { this.builds = builds.ToList(); }

        public IReadOnlyList<KnownBuild> All => builds;

        public static KnownBuilds Parse(string text)
        {
            var list = new List<KnownBuild>();
            foreach (var r in DataFile.ParseRecords(text))
            {
                var hash = DataFile.Field(r, 0);
                if (!IsSha256Hex(hash)) throw new FormatException("known-builds: not a SHA-256: " + hash);
                list.Add(new KnownBuild(hash, DataFile.Field(r, 1), DataFile.Field(r, 2)));
            }
            return new KnownBuilds(list);
        }

        public KnownBuild Find(string sha256) =>
            builds.FirstOrDefault(b => string.Equals(b.Sha256, sha256, StringComparison.OrdinalIgnoreCase));

        public BuildCheck Check(string exePath)
        {
            if (!File.Exists(exePath)) return new BuildCheck(BuildStatus.Missing, null, null);
            var hash = Sha256OfFile(exePath);
            var build = Find(hash);
            return new BuildCheck(build != null ? BuildStatus.Known : BuildStatus.Unknown, hash, build);
        }

        public static string Sha256OfFile(string path)
        {
            using (var sha = SHA256.Create())
            using (var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete, 1 << 20))
                return ToHex(sha.ComputeHash(stream));
        }

        public static string ToHex(byte[] bytes)
        {
            var sb = new StringBuilder(bytes.Length * 2);
            foreach (var b in bytes) sb.Append(b.ToString("x2"));
            return sb.ToString();
        }

        public static bool IsSha256Hex(string s) =>
            s != null && s.Length == 64 && s.All(c => (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'));
    }
}
