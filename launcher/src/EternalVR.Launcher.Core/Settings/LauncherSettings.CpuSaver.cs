using System;
using System.Collections.Generic;
using System.Linq;

namespace EternalVR.Launcher.Core.Settings
{
    /// <summary>The processor saver's choices in launcher.ini: <c>cpu_saver_&lt;id&gt; = on|off</c> per item of
    /// <c>data\cpu-saver.txt</c> (<see cref="Launch.CpuSaver"/>), and an older launcher's single <c>cpu_saver</c> switch.</summary>
    public sealed partial class LauncherSettings
    {
        /// <summary>The key prefix of a processor saver item's choice: <c>cpu_saver_&lt;id&gt; = on|off</c> (<see cref="Launch.CpuSaver"/>).</summary>
        public const string CpuSaverKeyPrefix = "cpu_saver_";
        /// <summary>The processor saver items the player chose, by id, in file order (an item without a choice takes its
        /// default, <see cref="Launch.CpuSaver.IsOn"/>). Replaced as a whole, never changed in place, so a clone may share it.</summary>
        public IReadOnlyList<KeyValuePair<string, bool>> CpuSaverChoices { get; private set; } = new KeyValuePair<string, bool>[0];
        /// <summary>An older launcher's single switch, <c>cpu_saver = on</c>: every item without a choice of its own is on.
        /// Written back only until the window has saved a choice for each item.</summary>
        public bool CpuSaverAllOn { get; set; } = false;

        /// <summary>The player's choice for processor saver item <paramref name="id"/>; null when there is none.</summary>
        public bool? CpuSaverChoice(string id)
        {
            foreach (var kv in CpuSaverChoices)
                if (string.Equals(kv.Key, id, StringComparison.OrdinalIgnoreCase)) return kv.Value;
            return null;
        }

        /// <summary>Sets the choices of the processor saver items (the window sets every item it shows): an item not
        /// named keeps its choice. Once chosen, the older <c>cpu_saver</c> switch no longer counts.</summary>
        public void SetCpuSaverChoices(IEnumerable<KeyValuePair<string, bool>> choices)
        {
            var list = CpuSaverChoices.ToList();
            foreach (var c in choices)
            {
                var id = c.Key.ToLowerInvariant();
                int i = list.FindIndex(kv => kv.Key == id);
                if (i >= 0) list[i] = new KeyValuePair<string, bool>(id, c.Value);
                else list.Add(new KeyValuePair<string, bool>(id, c.Value));
            }
            CpuSaverChoices = list;
            CpuSaverAllOn = false;
        }

        /// <summary>A processor saver item's key: <c>cpu_saver_</c> and an id (<see cref="Launch.CpuSaver.IsId"/>).</summary>
        private static bool IsCpuSaverKey(string key) =>
            key.StartsWith(CpuSaverKeyPrefix, StringComparison.OrdinalIgnoreCase)
            && Launch.CpuSaver.IsId(key.Substring(CpuSaverKeyPrefix.Length).ToLowerInvariant());

        /// <summary>on, 1 or true: on; off, 0 or false: off; anything else: no choice.</summary>
        private static bool? Switch(string text)
        {
            var t = text.Trim();
            if (t == "1" || string.Equals(t, "on", StringComparison.OrdinalIgnoreCase) || string.Equals(t, "true", StringComparison.OrdinalIgnoreCase)) return true;
            if (t == "0" || string.Equals(t, "off", StringComparison.OrdinalIgnoreCase) || string.Equals(t, "false", StringComparison.OrdinalIgnoreCase)) return false;
            return null;
        }
    }
}
