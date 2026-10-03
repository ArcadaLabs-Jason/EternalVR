using System.IO;

namespace EternalVR.Launcher.Core.Update
{
    public enum InstallBlockKind
    {
        /// <summary>The launcher's folder is not an unpacked release (no <c>BUILD-INFO.txt</c>): a build or test folder.</summary>
        NotRelease,
        /// <summary>The launcher's folder cannot be written.</summary>
        NotWritable,
        /// <summary>The game runs: the update would replace the layer it has loaded.</summary>
        GameRunning,
    }

    /// <summary>
    /// Why a newer release cannot be installed over this launcher now, in the words the update dialog shows. A folder that
    /// is not a release, or cannot be written, stays that way (<see cref="Lasting"/>): the dialog offers the release page
    /// instead. A running game only waits for it to quit.
    /// </summary>
    public sealed class InstallBlock
    {
        public const string BuildInfoFile = "BUILD-INFO.txt";

        private InstallBlock(InstallBlockKind kind, string text)
        {
            Kind = kind;
            Text = text;
        }

        public InstallBlockKind Kind { get; }
        public string Text { get; }
        /// <summary>True when installing stays impossible however long the dialog is open.</summary>
        public bool Lasting => Kind != InstallBlockKind.GameRunning;

        /// <summary>True when <paramref name="programDir"/> is an unpacked release (it holds <see cref="BuildInfoFile"/>).</summary>
        public static bool IsRelease(string programDir) => File.Exists(Path.Combine(programDir, BuildInfoFile));

        /// <summary>
        /// The reason that applies first (not a release, then a folder that cannot be written, then the running game); null
        /// when the release can be installed. <paramref name="writeError"/> is null when the folder can be written.
        /// </summary>
        public static InstallBlock For(bool isRelease, string writeError, bool gameRunning)
        {
            if (!isRelease)
                return new InstallBlock(InstallBlockKind.NotRelease,
                    "This is not a release build (it has no " + BuildInfoFile + "), so it cannot update itself. Get the new release from its page.");
            if (writeError != null)
                return new InstallBlock(InstallBlockKind.NotWritable,
                    "The launcher's folder cannot be written (" + writeError.TrimEnd('.') + "). Get the new release from its page, or move EternalVR to a folder of your own.");
            if (gameRunning)
                return new InstallBlock(InstallBlockKind.GameRunning, "Quit DOOM Eternal to install: the update replaces the layer the game loads.");
            return null;
        }
    }
}
