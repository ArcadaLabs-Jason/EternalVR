using System;
using System.Text.RegularExpressions;

namespace EternalVR.Launcher.Core.Preflight
{
    public enum VersionVerdict
    {
        /// <summary>Both sides carry the same release version.</summary>
        Match,
        /// <summary>Both are release versions and they differ: the launch is refused.</summary>
        Mismatch,
        /// <summary>A development build or an unreadable version on either side: logged as a warning only.</summary>
        Unverified,
    }

    /// <summary>
    /// The launcher and layer version handshake (ROADMAP M4.5 "User data and updates", ARCHITECTURE section 13).
    /// Both sides carry <c>MAJOR.MINOR.PATCH[-prerelease][+metadata]</c>: the launcher its informational
    /// version (<c>launcher/Directory.Build.props</c>, plus the commit), the layer the product version of
    /// <c>EternalVR.dll</c>'s version resource (<c>CMakeLists.txt</c>'s project version). Build metadata after
    /// <c>+</c> is ignored. A Debug build carries the <c>-dev</c> prerelease on either side, and a DLL without a
    /// version resource (built before the handshake) reads as unknown; both only warn, so development builds
    /// are never blocked.
    /// </summary>
    public static class VersionCheck
    {
        public const string DevMarker = "dev";

        private static readonly Regex Shape = new Regex(@"^(\d+)\.(\d+)\.(\d+)(?:-([0-9A-Za-z.-]+))?$", RegexOptions.CultureInvariant);

        /// <summary>The version without build metadata and whitespace; null when empty or not a version.</summary>
        public static string Normalize(string version)
        {
            if (string.IsNullOrWhiteSpace(version)) return null;
            var v = version.Trim();
            int plus = v.IndexOf('+');
            if (plus >= 0) v = v.Substring(0, plus);
            return Shape.IsMatch(v) ? v : null;
        }

        /// <summary>Whether the version is a development build (a prerelease part that names <c>dev</c>).</summary>
        public static bool IsDev(string version)
        {
            var v = Normalize(version);
            if (v == null) return false;
            var pre = Shape.Match(v).Groups[4].Value;
            foreach (var part in pre.Split('.', '-'))
                if (part.Equals(DevMarker, StringComparison.OrdinalIgnoreCase)) return true;
            return false;
        }

        public static VersionVerdict Compare(string launcherVersion, string layerVersion)
        {
            var a = Normalize(launcherVersion);
            var b = Normalize(layerVersion);
            if (a == null || b == null || IsDev(a) || IsDev(b)) return VersionVerdict.Unverified;
            return string.Equals(a, b, StringComparison.OrdinalIgnoreCase) ? VersionVerdict.Match : VersionVerdict.Mismatch;
        }

        /// <summary>The preflight check for the pair (id <c>version</c>).</summary>
        public static Check Evaluate(string launcherVersion, string layerVersion, string layerDir)
        {
            var shownLauncher = Show(launcherVersion);
            var shownLayer = Show(layerVersion);
            switch (Compare(launcherVersion, layerVersion))
            {
                case VersionVerdict.Match:
                    return new Check("version", Severity.Pass, $"Launcher and layer are both version {Normalize(launcherVersion)}");
                case VersionVerdict.Mismatch:
                    return new Check("version", Severity.Fail,
                        $"Version mismatch: the launcher is {shownLauncher} but the layer in {layerDir} is {shownLayer}. "
                        + "They must come from the same release: unzip the whole release again into an empty folder (keeping its folders) and start the launcher from there.");
                default:
                    var why = Normalize(launcherVersion) == null || Normalize(layerVersion) == null ? "a version could not be read" : "a development build";
                    return new Check("version", Severity.Warn,
                        $"Launcher {shownLauncher}, layer {shownLayer}: not compared ({why}). A release zip always carries a matching pair.");
            }
        }

        private static string Show(string version) =>
            Normalize(version) ?? (string.IsNullOrWhiteSpace(version) ? "unknown" : "'" + version.Trim() + "'");
    }
}
