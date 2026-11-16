using System;

namespace EternalVR.Launcher.Core.Report
{
    /// <summary>
    /// The report's power plan line (system.txt, near the CPU line): the held game's pacing depends on whether Windows lets
    /// the CPU's clock fall (Balanced) or not (High performance), so the active scheme goes into the export.
    /// </summary>
    public static class PowerPlan
    {
        /// <summary>What the line says when the query failed.</summary>
        public const string Unknown = "unknown";

        /// <summary>
        /// "Balanced {381b4222-f694-41f0-9685-ff5bb260df2e}": the scheme's name and GUID. With no friendly name the three built-in
        /// schemes are named from their GUIDs, else the GUID stands alone; with neither, "unknown". Never throws.
        /// </summary>
        public static string Describe(string friendlyName, Guid? scheme)
        {
            var name = string.IsNullOrWhiteSpace(friendlyName) ? null : string.Join(" ", friendlyName.Split((char[])null, StringSplitOptions.RemoveEmptyEntries));
            if (name == null && scheme.HasValue) name = BuiltInName(scheme.Value);
            if (!scheme.HasValue) return name == null ? Unknown : name;
            var guid = scheme.Value.ToString("B");
            return name == null ? guid : name + " " + guid;
        }

        private static string BuiltInName(Guid scheme)
        {
            switch (scheme.ToString("D"))
            {
                case "381b4222-f694-41f0-9685-ff5bb260df2e": return "Balanced";
                case "8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c": return "High performance";
                case "a1841308-3541-4fab-bc81-f71556f20b4a": return "Power saver";
                case "e9a42b02-d5df-448d-aa00-03f14749eb61": return "Ultimate Performance";
                default: return null;
            }
        }
    }
}
