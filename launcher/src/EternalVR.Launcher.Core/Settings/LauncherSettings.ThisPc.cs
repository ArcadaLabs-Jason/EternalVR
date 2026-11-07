using System.Collections.Generic;
using System.Text;

namespace EternalVR.Launcher.Core.Settings
{
    /// <summary>
    /// This PC's switches in launcher.ini: <c>adapter_steer</c> ("Use the headset's graphics card") and <c>test_builds</c>
    /// ("Get test builds"), both 0 or 1 and off by default. Like the folders and the runtime they are never part of a profile
    /// (<see cref="SettingKeys.Machine"/>), and Reset and switching profiles keep them.
    /// </summary>
    public sealed partial class LauncherSettings
    {
        /// <summary>
        /// The game renders on the headset's graphics card on a PC with two or more (the layer's <c>ETERNALVR_ADAPTER_STEER</c>,
        /// docs/rig-findings/adapter-steer.md); off by default. This machine's: never in a profile.
        /// </summary>
        public bool AdapterSteer { get; set; } = false;

        /// <summary>
        /// The update check also looks for a newer test build on the test repository (<see cref="Update.TestBuildFeed"/>,
        /// DECISIONS T-198); off by default, and then the check asks only for releases, as before. This machine's: never in a profile.
        /// </summary>
        public bool TestBuilds { get; set; } = false;

        private void ReadThisPc(IDictionary<string, string> map)
        {
            if (map.TryGetValue("adapter_steer", out var st)) AdapterSteer = On(st);
            if (map.TryGetValue("test_builds", out var tb)) TestBuilds = On(tb);
        }

        private void WriteThisPc(StringBuilder sb)
        {
            sb.AppendLine("adapter_steer = " + (AdapterSteer ? "1" : "0"));
            sb.AppendLine("test_builds = " + (TestBuilds ? "1" : "0"));
        }
    }
}
