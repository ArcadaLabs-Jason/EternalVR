namespace EternalVR.Launcher.Core.Settings
{
    /// <summary>How a pickup's animation camera is shown (the layer's <c>ETERNALVR_PICKUP_CAMERA</c>): the view stays with the
    /// head (still, the default) or the camera's motion plays on top of the head. The Play tab lists them in this order;
    /// launcher.ini stores the name, not the number.</summary>
    public enum PickupCameraView { Still, Play }

    /// <summary>The Pickup animations setting in launcher.ini: <c>pickup_camera = still|play</c>.</summary>
    public sealed partial class LauncherSettings
    {
        /// <summary>How a pickup's animation camera is shown; the view stays with the head by default.</summary>
        public PickupCameraView PickupCamera { get; set; } = PickupCameraView.Still;

        /// <summary>The settings file's and the layer's <c>ETERNALVR_PICKUP_CAMERA</c> value: still or play.</summary>
        public static string PickupCameraName(PickupCameraView p) => p == PickupCameraView.Play ? "play" : "still";

        /// <summary>A <c>pickup_camera</c> value (any case); still for anything else.</summary>
        private static PickupCameraView ParsePickupCamera(string text) =>
            Pick(text, PickupCameraView.Still, ("play", PickupCameraView.Play));
    }
}
