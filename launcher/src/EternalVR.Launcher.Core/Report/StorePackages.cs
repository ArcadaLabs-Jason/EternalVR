using System;
using System.Collections.Generic;
using System.Linq;
using EternalVR.Launcher.Core.Game;

namespace EternalVR.Launcher.Core.Report
{
    /// <summary>
    /// The Microsoft Store packages a report describes for a Game Pass or Microsoft Store game: the game's own package and the
    /// Xbox pieces it starts through (Gaming Services, the Xbox app, the Xbox sign-in). The launcher asks Windows for them
    /// (<c>Get-AppxPackage</c>, one line per package in <see cref="Query"/>'s format); the lines are read and written here.
    /// </summary>
    public static class StorePackages
    {
        public static readonly IReadOnlyList<KeyValuePair<string, string>> Names = new[]
        {
            new KeyValuePair<string, string>(GamePassInstall.PackageName, "store package"),
            new KeyValuePair<string, string>("Microsoft.GamingServices", "gaming services"),
            new KeyValuePair<string, string>("Microsoft.GamingApp", "xbox app"),
            new KeyValuePair<string, string>("Microsoft.XboxIdentityProvider", "xbox identity provider"),
        };

        /// <summary>The PowerShell command: <c>Name|Version|Status|SignatureKind|InstallLocation|PackageFullName</c> per package.</summary>
        public static string Query() =>
            "$n = @(" + string.Join(",", Names.Select(n => "'" + n.Key + "'")) + "); "
            + "Get-AppxPackage | Where-Object { $n -contains $_.Name } | ForEach-Object { "
            + "\"$($_.Name)|$($_.Version)|$($_.Status)|$($_.SignatureKind)|$($_.InstallLocation)|$($_.PackageFullName)\" }";

        /// <summary>
        /// The report lines (<c>store package: ...</c>, <c>gaming services: ...</c> ...) of the query's output: each package by its
        /// version and Windows' status of it (<c>Ok</c>, or the problem: <c>LicenseIssue</c>, <c>Modified</c>, <c>Tampered</c>,
        /// <c>Disabled</c> ...), the game's full name and folder too; "not installed" for one that is not. <paramref name="error"/>
        /// (the query failed) is reported once instead.
        /// </summary>
        public static IReadOnlyList<KeyValuePair<string, string>> Describe(string output, string error)
        {
            var lines = new List<KeyValuePair<string, string>>();
            if (error != null)
            {
                lines.Add(new KeyValuePair<string, string>("store packages", "could not be read (" + error + ")"));
                return lines;
            }
            var rows = (output ?? string.Empty).Split('\n')
                .Select(l => l.Trim().Split('|'))
                .Where(f => f.Length >= 6)
                .ToList();
            foreach (var n in Names)
            {
                var found = rows.Where(f => string.Equals(f[0], n.Key, StringComparison.OrdinalIgnoreCase)).ToList();
                if (found.Count == 0) { lines.Add(new KeyValuePair<string, string>(n.Value, "not installed")); continue; }
                foreach (var f in found)
                {
                    var value = f[1] + ", status " + Field(f[2]) + ", signature " + Field(f[3]);
                    if (n.Key == GamePassInstall.PackageName) value += ", " + f[5] + " in " + Field(f[4]);
                    lines.Add(new KeyValuePair<string, string>(n.Value, value));
                }
            }
            return lines;
        }

        private static string Field(string s) => string.IsNullOrWhiteSpace(s) ? "unknown" : s.Trim();
    }
}
