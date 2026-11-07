using System;
using System.Globalization;
using System.IO;
using System.Linq;

namespace EternalVR.Launcher.Core.Update
{
    /// <summary>
    /// The build this launcher is, as far as a test build is compared with it (<see cref="TestBuildFeed.IsNewer"/>): its
    /// version and, for an unpacked release or test build, the commit and commit time its <c>BUILD-INFO.txt</c> records
    /// (<c>commit: &lt;sha&gt;</c>, <c>commit time: yyyy-MM-dd HH:mm:ss zzz</c>, as tools/release/make-release.ps1 writes them).
    /// </summary>
    public sealed class RunningBuild
    {
        public RunningBuild(Version version, string commit, DateTimeOffset? commitTime)
        {
            Version = ReleaseFeed.Plain(version);
            Commit = commit;
            CommitTime = commitTime;
        }

        /// <summary>Major.minor.build of the launcher.</summary>
        public Version Version { get; }
        /// <summary>The commit it was built from, lower-case hex; null when not known (no <c>BUILD-INFO.txt</c>).</summary>
        public string Commit { get; }
        /// <summary>When that commit was made; null when not known.</summary>
        public DateTimeOffset? CommitTime { get; }

        /// <summary>True when <paramref name="sha"/> (7 or more hex digits) names this build's commit.</summary>
        public bool IsCommit(string sha) =>
            Commit != null && !string.IsNullOrEmpty(sha) && sha.Length >= 7
            && (Commit.StartsWith(sha, StringComparison.OrdinalIgnoreCase) || sha.StartsWith(Commit, StringComparison.OrdinalIgnoreCase));

        /// <summary>The build of <paramref name="version"/> that <paramref name="buildInfo"/> (a <c>BUILD-INFO.txt</c>; null for none) describes.</summary>
        public static RunningBuild FromBuildInfo(Version version, string buildInfo)
        {
            string commit = null;
            DateTimeOffset? time = null;
            foreach (var raw in (buildInfo ?? string.Empty).Replace("\r\n", "\n").Split('\n'))
            {
                var line = raw.Trim();
                if (line.StartsWith("commit time:", StringComparison.OrdinalIgnoreCase))
                {
                    if (DateTimeOffset.TryParseExact(line.Substring(12).Trim(), "yyyy-MM-dd HH:mm:ss zzz", CultureInfo.InvariantCulture,
                        DateTimeStyles.None, out var t))
                        time = t;
                }
                else if (line.StartsWith("commit:", StringComparison.OrdinalIgnoreCase))
                {
                    // "commit: <sha>", with " (with uncommitted changes: ...)" after it for a build of a changed tree.
                    var sha = line.Substring(7).Trim().Split(' ').FirstOrDefault() ?? string.Empty;
                    if (sha.Length >= 7 && sha.Length <= 40 && sha.All(Uri.IsHexDigit)) commit = sha.ToLowerInvariant();
                }
            }
            return new RunningBuild(version, commit, time);
        }

        /// <summary>The build in <paramref name="programDir"/>: its <c>BUILD-INFO.txt</c> when it has one and it can be read.</summary>
        public static RunningBuild Read(string programDir, Version version)
        {
            string text = null;
            try
            {
                var path = Path.Combine(programDir, InstallBlock.BuildInfoFile);
                if (File.Exists(path)) text = File.ReadAllText(path);
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException)
            {
                text = null;
            }
            return FromBuildInfo(version, text);
        }
    }
}
