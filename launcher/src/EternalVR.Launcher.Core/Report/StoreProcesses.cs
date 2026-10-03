using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;

namespace EternalVR.Launcher.Core.Report
{
    /// <summary>One running process, as the launcher saw it.</summary>
    public sealed class SeenProcess
    {
        public SeenProcess(string name, int id, DateTime? started)
        {
            Name = name;
            Id = id;
            Started = started;
        }

        public string Name { get; }
        public int Id { get; }
        /// <summary>Local time; null when Windows would not say.</summary>
        public DateTime? Started { get; }
    }

    /// <summary>
    /// The Xbox and Gaming Services processes alive when a Game Pass or Microsoft Store game is started, and after it closes
    /// early: logged by the launcher, so a report shows what a flat session from the Xbox app left running (a Game Pass
    /// player's game crashed at start only after a flat session, 2026-10-03).
    /// </summary>
    public static class StoreProcesses
    {
        public static readonly IReadOnlyList<string> Names = new[]
        {
            "gamingservices", "gamingservicesnet", "XboxPcApp", "XboxPcAppFT", "XboxPcTray", "GameLaunchHelper", "GameBar",
            "GameBarFTServer", "XboxGameBarWidgets", "idTechLauncher", "DOOMEternalx64vk", "XboxIdp", "WinStore.App",
        };

        /// <summary><c>store processes at launch: gamingservices 4120 (since 09:12:30), XboxPcApp 8812 (since 13:20:05)</c>, or "none".</summary>
        public static string Describe(string when, IEnumerable<SeenProcess> seen)
        {
            var list = (seen ?? Enumerable.Empty<SeenProcess>())
                .OrderBy(p => p.Name, StringComparer.OrdinalIgnoreCase).ThenBy(p => p.Id)
                .Select(p => p.Name + " " + p.Id.ToString(CultureInfo.InvariantCulture)
                    + (p.Started.HasValue ? " (since " + p.Started.Value.ToString("HH:mm:ss", CultureInfo.InvariantCulture) + ")" : string.Empty))
                .ToList();
            return "store processes " + when + ": " + (list.Count == 0 ? "none" : string.Join(", ", list));
        }
    }
}
