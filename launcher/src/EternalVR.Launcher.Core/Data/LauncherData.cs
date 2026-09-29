using System.IO;
using EternalVR.Launcher.Core.Game;
using EternalVR.Launcher.Core.Launch;
using EternalVR.Launcher.Core.Preflight;

namespace EternalVR.Launcher.Core.Data
{
    /// <summary>The launcher's data files, loaded from the <c>data</c> folder next to the launcher.</summary>
    public sealed class LauncherData
    {
        public KnownBuilds Builds { get; private set; }
        public ForcedCvars ForcedCvars { get; private set; }
        public SessionKeys SessionKeys { get; private set; }
        public ArgumentPolicy ArgumentPolicy { get; private set; }
        public KnownLayers KnownLayers { get; private set; }
        public AntiCheat AntiCheat { get; private set; }
        /// <summary>The processor saver's items (<see cref="Launch.CpuSaver"/>).</summary>
        public CpuSaver CpuSaver { get; private set; }

        /// <summary>
        /// The keys the settings restore puts back after a session: the forced cvars, the session keys and the processor
        /// saver's cvars, of every item (whether it was on or not: a key the session did not change is left alone).
        /// </summary>
        public System.Collections.Generic.IReadOnlyList<string> RestoredKeys => SessionKeys.RestoredKeys(ForcedCvars, SessionKeys, CpuSaver?.Names);

        public static LauncherData Load(string dataDir)
        {
            string Read(string name) => File.ReadAllText(Path.Combine(dataDir, name));
            return new LauncherData
            {
                Builds = KnownBuilds.Parse(Read("known-builds.txt")),
                ForcedCvars = ForcedCvars.Parse(Read("forced-cvars.txt")),
                SessionKeys = SessionKeys.Parse(Read("session-keys.txt")),
                ArgumentPolicy = ArgumentPolicy.Parse(Read("refused-args.txt")),
                KnownLayers = KnownLayers.Parse(Read("known-layers.txt")),
                AntiCheat = AntiCheat.Parse(Read("anti-cheat.txt")),
                CpuSaver = CpuSaver.Parse(Read("cpu-saver.txt")),
            };
        }
    }
}
