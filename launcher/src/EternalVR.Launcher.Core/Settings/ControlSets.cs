using System;
using System.IO;

namespace EternalVR.Launcher.Core.Settings
{
    /// <summary>
    /// The controls of each VR settings profile (docs/release/CONTROLS.md). The controls folder itself,
    /// <c>&lt;data root&gt;\controls\</c>, holds the controls used with no profile ("(none)"), as it did before profiles had
    /// controls; each profile keeps its own set in <c>controls\profiles\&lt;name&gt;\</c>, a <see cref="ControlsFolder"/> of the
    /// same shape. A profile without a folder of its own (made by an older launcher, or copied in by hand) uses the no-profile
    /// set until <see cref="Adopt"/> gives it a copy of that set.
    /// </summary>
    public sealed class ControlSets
    {
        public const string ProfilesName = "profiles";
        /// <summary>A copy in progress: a leading dot, which no profile name can have (<see cref="ProfileStore.NormaliseName"/>).</summary>
        private const string TempPrefix = ".evr-tmp-";

        public ControlSets(string root)
        {
            if (string.IsNullOrWhiteSpace(root)) throw new ArgumentException("controls folder is empty", nameof(root));
            Root = Path.GetFullPath(root);
        }

        /// <summary>The controls folder: the no-profile set, with the profiles' sets under <see cref="ProfilesDir"/>.</summary>
        public string Root { get; }
        public string ProfilesDir => Path.Combine(Root, ProfilesName);

        /// <summary>The controls used with no VR settings profile.</summary>
        public ControlsFolder Shared => new ControlsFolder(Root);

        /// <summary>
        /// The profile's own set, whether or not its folder exists yet; the no-profile set for no profile, or for a name
        /// that cannot be a profile.
        /// </summary>
        public ControlsFolder Of(string profile) =>
            ProfileStore.NormaliseName(profile) is string n ? new ControlsFolder(Path.Combine(ProfilesDir, n), n) : Shared;

        /// <summary>True when the profile has a controls folder of its own.</summary>
        public bool HasOwn(string profile) => ProfileStore.NormaliseName(profile) != null && Directory.Exists(Of(profile).Dir);

        /// <summary>
        /// The set the launch uses and the editor shows for <paramref name="profile"/>: its own when it has one, else the
        /// no-profile set.
        /// </summary>
        public ControlsFolder InUse(string profile) => HasOwn(profile) ? Of(profile) : Shared;

        /// <summary>
        /// Gives a profile without a controls folder of its own a copy of the no-profile set's maps, so from now on it keeps
        /// its own. True when a folder was made; false for no profile, or one that already has its folder.
        /// </summary>
        public bool Adopt(string profile)
        {
            if (ProfileStore.NormaliseName(profile) == null || HasOwn(profile)) return false;
            CopyInto(profile, Shared);
            return true;
        }

        /// <summary>
        /// A new profile's controls: its folder becomes a copy of the maps in <paramref name="from"/>'s set in use (the
        /// no-profile set for none), replacing any folder of that name left behind.
        /// </summary>
        public void StartFrom(string profile, string from)
        {
            var n = ProfileStore.NormaliseName(profile) ?? throw new ArgumentException("not a profile name: " + profile, nameof(profile));
            var source = InUse(from);
            if (string.Equals(source.Dir, Of(n).Dir, StringComparison.OrdinalIgnoreCase)) return;
            CopyInto(n, source);
        }

        /// <summary>Removes the profile's controls folder and everything in it; nothing for no profile or no folder.</summary>
        public void Delete(string profile)
        {
            if (ProfileStore.NormaliseName(profile) == null) return;
            FileUtil.DeleteDirectory(Of(profile).Dir);
        }

        /// <summary>
        /// Makes <paramref name="profile"/>'s folder hold a copy of <paramref name="source"/>'s maps (not its README or
        /// defaults, which <see cref="ControlsFolder.Prepare"/> writes). The copy is made beside it and then moved into place,
        /// so a failed copy leaves no half-made set.
        /// </summary>
        private void CopyInto(string profile, ControlsFolder source)
        {
            var n = ProfileStore.NormaliseName(profile);
            var target = Of(n).Dir;
            var temp = Path.Combine(ProfilesDir, TempPrefix + n);
            FileUtil.DeleteDirectory(temp);
            Directory.CreateDirectory(temp);
            try
            {
                foreach (var map in source.PlayerMaps())
                {
                    var copy = Path.Combine(temp, Path.GetFileName(map));
                    File.Copy(map, copy);
                    File.SetAttributes(copy, FileAttributes.Normal);
                }
                FileUtil.DeleteDirectory(target);
                Directory.Move(temp, target);
            }
            catch
            {
                try { FileUtil.DeleteDirectory(temp); }
                catch (Exception e) when (e is IOException || e is UnauthorizedAccessException) { }
                throw;
            }
        }
    }
}
