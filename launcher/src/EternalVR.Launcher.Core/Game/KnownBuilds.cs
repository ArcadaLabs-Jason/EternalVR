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
            Platform = GamePlatform.Steam;
        }

        private KnownBuild(string storeName, string version, string description, GamePlatform platform)
        {
            StoreName = storeName;
            BuildId = version;
            Description = description;
            Platform = platform;
        }

        /// <summary>A Game Pass build: its package identity name and version (<c>MicrosoftGame.Config</c>).</summary>
        public static KnownBuild GamePass(string identityName, string version, string description) =>
            new KnownBuild(identityName, version, description, GamePlatform.GamePass);

        /// <summary>The exe's SHA-256; null for a Game Pass build (its exe cannot be read).</summary>
        public string Sha256 { get; }
        /// <summary>Steam's build ID, or the Game Pass package version.</summary>
        public string BuildId { get; }
        public string Description { get; }
        public GamePlatform Platform { get; }
        /// <summary>The package identity name of a Game Pass build; null for a Steam build.</summary>
        public string StoreName { get; }
    }

    public enum BuildStatus { Known, Unknown, Missing }

    public sealed class BuildCheck
    {
        public BuildCheck(BuildStatus status, string sha256, KnownBuild build, string version = null)
        {
            Status = status;
            Sha256 = sha256;
            Build = build;
            Version = version;
        }

        public BuildStatus Status { get; }
        /// <summary>The exe's SHA-256 (Steam); null for Game Pass.</summary>
        public string Sha256 { get; }
        public KnownBuild Build { get; }
        /// <summary>The installed package version (Game Pass); null for Steam.</summary>
        public string Version { get; }

        /// <summary>
        /// The Steam build Parallel Eye Rendering runs on: the one the layer knows by its exe's timestamp
        /// (src/vkcore/view_install.cpp). On any other build, Game Pass among them, the layer would refuse it ("not available
        /// for this game version") and run the standard renderer, so the launcher does not ask for it there.
        /// </summary>
        public const string ParallelEyesSteamBuild = "25216728";

        /// <summary>The game is the build Parallel Eye Rendering runs on (<see cref="ParallelEyesSteamBuild"/>).</summary>
        public bool RunsParallelEyes =>
            Status == BuildStatus.Known && Build != null && Build.Platform == GamePlatform.Steam && Build.BuildId == ParallelEyesSteamBuild;
    }

    /// <summary>
    /// The builds the layer supports (T-094), kept as data in <c>data\known-builds.txt</c>: Steam builds by exe
    /// hash, <c>sha256 | steam build id | description</c>, and Game Pass builds by package identity,
    /// <c>gamepass | identity name | version | description</c>.
    /// </summary>
    public sealed class KnownBuilds
    {
        private readonly List<KnownBuild> builds;
        /// <summary>The exe's hash is read again only when the file changed (<see cref="Check"/> runs at every preflight).</summary>
        private readonly FileHashCache exeHashes;

        public KnownBuilds(IEnumerable<KnownBuild> builds) : this(builds, Sha256OfFile) { }

        /// <summary>With the function that hashes the exe (the tests count its reads).</summary>
        internal KnownBuilds(IEnumerable<KnownBuild> builds, Func<string, string> hash)
        {
            this.builds = builds.ToList();
            exeHashes = new FileHashCache(hash);
        }

        public IReadOnlyList<KnownBuild> All => builds;

        /// <summary>The first field of a Game Pass record.</summary>
        public const string GamePassRecord = "gamepass";

        public static KnownBuilds Parse(string text)
        {
            var list = new List<KnownBuild>();
            foreach (var r in DataFile.ParseRecords(text))
            {
                var hash = DataFile.Field(r, 0);
                if (string.Equals(hash, GamePassRecord, StringComparison.OrdinalIgnoreCase))
                {
                    var name = DataFile.Field(r, 1);
                    var version = DataFile.Field(r, 2);
                    if (name.Length == 0 || !System.Version.TryParse(version, out _))
                        throw new FormatException("known-builds: a Game Pass record needs an identity name and a version: " + string.Join(" | ", r));
                    list.Add(KnownBuild.GamePass(name, version, DataFile.Field(r, 3)));
                    continue;
                }
                if (!IsSha256Hex(hash)) throw new FormatException("known-builds: not a SHA-256: " + hash);
                list.Add(new KnownBuild(hash, DataFile.Field(r, 1), DataFile.Field(r, 2)));
            }
            return new KnownBuilds(list);
        }

        public KnownBuild Find(string sha256) =>
            builds.FirstOrDefault(b => b.Sha256 != null && string.Equals(b.Sha256, sha256, StringComparison.OrdinalIgnoreCase));

        /// <summary>The build IDs (Steam) or package versions (Game Pass) of one platform, for messages.</summary>
        public IReadOnlyList<string> IdsFor(GamePlatform platform) =>
            builds.Where(b => b.Platform == platform).Select(b => b.BuildId).Distinct().ToList();

        /// <summary>A Game Pass install by its package identity: known when name and version match a record; missing without one.</summary>
        public BuildCheck CheckStore(StoreIdentity identity)
        {
            if (identity == null) return new BuildCheck(BuildStatus.Missing, null, null);
            var build = builds.FirstOrDefault(b => b.Platform == GamePlatform.GamePass
                && string.Equals(b.StoreName, identity.Name, StringComparison.OrdinalIgnoreCase)
                && string.Equals(b.BuildId, identity.Version, StringComparison.Ordinal));
            return new BuildCheck(build != null ? BuildStatus.Known : BuildStatus.Unknown, null, build, identity.Version);
        }

        public BuildCheck Check(string exePath)
        {
            if (!File.Exists(exePath)) return new BuildCheck(BuildStatus.Missing, null, null);
            var hash = exeHashes.Get(exePath);
            var build = Find(hash);
            return new BuildCheck(build != null ? BuildStatus.Known : BuildStatus.Unknown, hash, build);
        }

        public static string Sha256OfFile(string path)
        {
            using (var sha = SHA256.Create())
            using (var stream = new FileStream(FileUtil.Long(path), FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete, 1 << 20))
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
