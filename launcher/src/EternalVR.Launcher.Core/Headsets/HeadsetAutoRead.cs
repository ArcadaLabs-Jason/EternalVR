using System;

namespace EternalVR.Launcher.Core.Headsets
{
    /// <summary>
    /// Whether the launcher reads the headset by itself when it opens (the runtime probe of Detect again, in the background).
    /// Only when the runtime the game will use is already up, so opening the launcher never starts one: Virtual Desktop's own
    /// runtime (VDXR) when its Streamer runs, Meta's runtime (Quest Link) when its server runs, SteamVR when its server runs
    /// (Virtual Desktop in SteamVR mode included). Any other runtime is read at Launch VR, as before.
    /// </summary>
    public static class HeadsetAutoRead
    {
        /// <summary>Virtual Desktop's Streamer (<c>VirtualDesktop.Streamer.exe</c>), which VDXR talks to.</summary>
        public const string VirtualDesktopStreamer = "VirtualDesktop.Streamer";
        /// <summary>Meta's runtime server (<c>OVRServer_x64.exe</c>), started by its service for Quest Link and Air Link.</summary>
        public const string MetaServer = "OVRServer_x64";
        /// <summary>SteamVR's server (<c>vrserver.exe</c>): runs only while SteamVR does.</summary>
        public const string SteamVrServer = "vrserver";

        /// <summary>The process that shows the route's runtime is up; null for a route the launcher does not read by itself.</summary>
        public static string ProcessOf(RouteKind route)
        {
            switch (route)
            {
                case RouteKind.VirtualDesktop: return VirtualDesktopStreamer;
                case RouteKind.MetaLink: return MetaServer;
                case RouteKind.SteamVr: return SteamVrServer;
                default: return null;
            }
        }

        /// <summary>
        /// Null when the headset is read now; else why not, in the window's words ("SteamVR is not running"). For the runtime
        /// named <paramref name="runtimeName"/> (the manifest's <c>runtime.name</c> of the runtime the game will use; null when
        /// none is set or it cannot be read), with <paramref name="isRunning"/> telling whether a process runs, and
        /// <paramref name="loaderPresent"/> whether the layer's OpenXR loader is there to ask with.
        /// </summary>
        public static string WhyNot(string runtimeName, Func<string, bool> isRunning, bool loaderPresent)
        {
            var route = HeadsetIdentity.RouteOf(runtimeName);
            if (route == RouteKind.Unknown) return "no OpenXR runtime is set";
            if (!loaderPresent) return "the EternalVR layer's OpenXR loader is missing";
            var process = ProcessOf(route);
            if (process == null) return HeadsetIdentity.RouteNameOf(route, runtimeName) + " is read only when you launch";
            if (isRunning(process)) return null;
            switch (route)
            {
                case RouteKind.VirtualDesktop: return "the Virtual Desktop Streamer is not running";
                case RouteKind.MetaLink: return "Meta's Link software is not running";
                default: return "SteamVR is not running";
            }
        }
    }
}
