namespace EternalVR.Launcher.Core.Settings
{
    /// <summary>How glory kills are shown (the layer's <c>ETERNALVR_GLORY_KILLS</c>): the view follows the kill's camera, keeps
    /// its heading (steady), fades out, or the kill plays on a flat screen.</summary>
    public enum GloryKillView { Follow, Steady, Fade, Screen }

    /// <summary>The Glory kills setting in launcher.ini: <c>glory_kills = follow|steady|fade|screen</c>.</summary>
    public sealed partial class LauncherSettings
    {
        /// <summary>How glory kills are shown; the view follows the kill's camera by default.</summary>
        public GloryKillView GloryKills { get; set; } = GloryKillView.Follow;

        /// <summary>The settings file's and the layer's <c>ETERNALVR_GLORY_KILLS</c> value: follow, steady, fade or screen.</summary>
        public static string GloryKillName(GloryKillView g) =>
            g == GloryKillView.Steady ? "steady" : g == GloryKillView.Fade ? "fade" : g == GloryKillView.Screen ? "screen" : "follow";

        /// <summary>A <c>glory_kills</c> value (any case); follow for anything else.</summary>
        private static GloryKillView ParseGloryKills(string text) =>
            Pick(text, GloryKillView.Follow, ("steady", GloryKillView.Steady), ("fade", GloryKillView.Fade), ("screen", GloryKillView.Screen));
    }
}
