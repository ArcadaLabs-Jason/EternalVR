using System;

namespace EternalVR.Launcher.Core.Report
{
    /// <summary>
    /// How the launcher asks for a report: one short sentence in every status line and dialog that wants one, and while a
    /// warning or a problem on the status line holds it the window marks its Export report button, so a player who has
    /// never exported finds it. The most common help players needed on Discord was how to export.
    /// </summary>
    public static class ReportHint
    {
        public const string ButtonName = "Export report";

        public const string Ask = "Click " + ButtonName + " and post the zip with your bug report.";

        /// <summary>The text asks for a report (it names the button).</summary>
        public static bool In(string text) => text != null && text.IndexOf(ButtonName, StringComparison.Ordinal) >= 0;
    }
}
