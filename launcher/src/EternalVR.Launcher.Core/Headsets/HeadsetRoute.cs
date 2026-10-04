using System;
using System.IO;
using System.Linq;
using EternalVR.Launcher.Core.Report;
using EternalVR.Launcher.Core.Text;

namespace EternalVR.Launcher.Core.Headsets
{
    /// <summary>The way the game reaches the headset: the OpenXR runtime in use.</summary>
    public enum RouteKind
    {
        /// <summary>No runtime known.</summary>
        Unknown,
        /// <summary>Virtual Desktop's own runtime, VDXR (<c>VirtualDesktopXR</c>).</summary>
        VirtualDesktop,
        /// <summary>Meta Horizon Link and Air Link (<c>Oculus</c>).</summary>
        MetaLink,
        /// <summary>SteamVR (<c>SteamVR/OpenXR</c>): every headset SteamVR drives, Steam Link and Virtual Desktop in SteamVR mode included.</summary>
        SteamVr,
        /// <summary>Pimax Play's runtime (<c>Pimax OpenXR</c>, the older PimaxXR).</summary>
        PimaxPlay,
        /// <summary>Varjo Base (<c>Varjo OpenXR Runtime</c>).</summary>
        VarjoBase,
        /// <summary>Any other runtime, named as it names itself.</summary>
        Other,
    }

    /// <summary>
    /// Which headset and route the launcher found, in the window's words, from the runtime probe's names and SteamVR's last
    /// seen headset. A vendor's runtime names the model as its system name; SteamVR names only its tracking system
    /// (<c>SteamVR/OpenXR : lighthouse</c>), so the model then comes from SteamVR's settings file, said to be last seen by
    /// SteamVR, or from a tracking system one headset alone uses (<c>playstation_vr2</c>), or is not named at all.
    /// </summary>
    public sealed class HeadsetIdentity
    {
        /// <summary>The prefix of SteamVR's system name before its tracking system.</summary>
        public const string SteamVrSystemPrefix = "SteamVR/OpenXR";

        private HeadsetIdentity() { }

        public RouteKind Route { get; private set; }
        /// <summary>The route as the window names it: "Virtual Desktop (VDXR)", "Meta Link", "SteamVR", "Pimax Play", "Varjo
        /// Base", or the runtime's own name.</summary>
        public string RouteName { get; private set; }
        /// <summary>Who sets the size the runtime asks for: "VD", "Meta Link", "SteamVR" or "the runtime".</summary>
        public string Asker { get; private set; }
        /// <summary>The headset as the window names it: "Meta Quest 3", "Valve Index (last seen by SteamVR)", "a SteamVR headset
        /// (lighthouse)"; null when nothing names it.</summary>
        public string Model { get; private set; }
        /// <summary>True when <see cref="Model"/> names a model (not only "a SteamVR headset").</summary>
        public bool ModelKnown { get; private set; }
        /// <summary>The headset in <c>data\headsets.txt</c>, with its native panel; null when it is not in the list.</summary>
        public HeadsetModel Known { get; private set; }
        /// <summary>SteamVR's tracking system (<c>lighthouse</c>, <c>oculus</c>, <c>playstation_vr2</c>...); null on other routes.</summary>
        public string TrackingSystem { get; private set; }

        /// <summary>"VD asks for", "Meta Link asks for", "SteamVR asks for" or "The runtime asks for".</summary>
        public string AsksLabel => Capitalised(Asker) + " asks for";

        /// <summary>The Headset line: "Meta Quest 3 via Virtual Desktop (VDXR)", "A SteamVR headset (lighthouse)".</summary>
        public string Describe()
        {
            if (Model == null) return "A headset via " + RouteName;
            if (Route == RouteKind.SteamVr && !ModelKnown) return Capitalised(Model);
            return Capitalised(Model) + " via " + RouteName;
        }

        /// <summary>The route of a runtime's name: the probe's (<c>VirtualDesktopXR</c>) or its manifest's (<c>VirtualDesktopXR (Bundled)</c>).</summary>
        public static RouteKind RouteOf(string runtimeName)
        {
            var n = (runtimeName ?? string.Empty).Trim();
            if (n.Length == 0) return RouteKind.Unknown;
            if (n.StartsWith("VirtualDesktopXR", StringComparison.OrdinalIgnoreCase)) return RouteKind.VirtualDesktop;
            if (n.StartsWith("Oculus", StringComparison.OrdinalIgnoreCase)) return RouteKind.MetaLink;
            if (n.StartsWith("SteamVR", StringComparison.OrdinalIgnoreCase)) return RouteKind.SteamVr;
            if (n.IndexOf("Pimax", StringComparison.OrdinalIgnoreCase) >= 0) return RouteKind.PimaxPlay;
            if (n.IndexOf("Varjo", StringComparison.OrdinalIgnoreCase) >= 0) return RouteKind.VarjoBase;
            return RouteKind.Other;
        }

        /// <summary>The route's name in the window; <paramref name="runtimeName"/> itself for any other runtime.</summary>
        public static string RouteNameOf(RouteKind route, string runtimeName)
        {
            switch (route)
            {
                case RouteKind.VirtualDesktop: return "Virtual Desktop (VDXR)";
                case RouteKind.MetaLink: return "Meta Link";
                case RouteKind.SteamVr: return "SteamVR";
                case RouteKind.PimaxPlay: return "Pimax Play";
                case RouteKind.VarjoBase: return "Varjo Base";
                case RouteKind.Other: return runtimeName.Trim();
                default: return "an unknown runtime";
            }
        }

        /// <summary>
        /// The <c>runtime.name</c> of an OpenXR runtime manifest (<c>VirtualDesktopXR (Bundled)</c>, <c>SteamVR</c>): the route the
        /// next launch takes, before any probe. Null when the path is empty or the file is missing or unreadable.
        /// </summary>
        public static string ManifestRuntimeName(string manifestPath)
        {
            if (string.IsNullOrWhiteSpace(manifestPath)) return null;
            try
            {
                if (!File.Exists(manifestPath)) return null;
                var name = MiniJson.Get(MiniJson.Parse(File.ReadAllText(manifestPath)), "runtime", "name") as string;
                return string.IsNullOrWhiteSpace(name) ? null : name.Trim();
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException || e is FormatException
                || e is OverflowException || e is ArgumentException || e is NotSupportedException)
            {
                return null;
            }
        }

        /// <summary>SteamVR's tracking system in its system name (<c>SteamVR/OpenXR : lighthouse</c>); null for another name.</summary>
        public static string TrackingSystemOf(string systemName)
        {
            var n = (systemName ?? string.Empty).Trim();
            int colon = n.IndexOf(':');
            if (!n.StartsWith(SteamVrSystemPrefix, StringComparison.OrdinalIgnoreCase) || colon < 0) return null;
            var token = n.Substring(colon + 1).Trim();
            return token.Length == 0 ? null : token;
        }

        /// <summary>
        /// The headset and route for the probe's <paramref name="runtimeName"/> and <paramref name="systemName"/> (either may be
        /// null: not read yet), SteamVR's last seen headset <paramref name="lastSeen"/> (null: none) and the table of headsets.
        /// </summary>
        public static HeadsetIdentity Identify(string runtimeName, string systemName, SteamVrHeadset lastSeen, HeadsetTable table)
        {
            table = table ?? HeadsetTable.Empty;
            var id = new HeadsetIdentity { Route = RouteOf(runtimeName) };
            id.RouteName = RouteNameOf(id.Route, runtimeName);
            id.Asker = id.Route == RouteKind.VirtualDesktop ? "VD" : id.Route == RouteKind.MetaLink ? "Meta Link"
                : id.Route == RouteKind.SteamVr ? "SteamVR" : "the runtime";
            var system = string.IsNullOrWhiteSpace(systemName) ? null : systemName.Trim();
            if (id.Route == RouteKind.SteamVr)
            {
                id.TrackingSystem = TrackingSystemOf(system);
                var seen = SameHeadset(lastSeen, id.TrackingSystem) ? lastSeen : null;
                // Virtual Desktop's SteamVR mode reaches SteamVR through its oculus driver.
                if (seen?.Driver != null && seen.Driver.StartsWith("oculus_virtualdesktop", StringComparison.OrdinalIgnoreCase))
                    id.RouteName = "Virtual Desktop through SteamVR";
                // The model SteamVR last saw wins when it is in the list; else a tracking system one headset alone uses.
                var bySeen = seen == null ? null : table.BySteamVrModel(seen.Model);
                var byTracking = table.ByTrackingSystem(id.TrackingSystem);
                id.Known = bySeen ?? byTracking;
                if (seen != null)
                {
                    id.Model = (bySeen?.Name ?? MakerAndModel(seen)) + " (last seen by SteamVR)";
                    id.ModelKnown = true;
                }
                else if (byTracking != null)
                {
                    id.Model = byTracking.Name;
                    id.ModelKnown = true;
                }
                else
                {
                    id.Model = "a SteamVR headset" + (id.TrackingSystem == null ? string.Empty : " (" + id.TrackingSystem + ")");
                }
                return id;
            }
            if (system != null)
            {
                id.Model = system;
                id.ModelKnown = true;
                id.Known = table.BySystemName(system);
            }
            return id;
        }

        /// <summary>
        /// Drivers whose headsets name another tracking system: the Steam Frame's driver is <c>vrlink</c> and its system name
        /// <c>SteamVR/OpenXR : cv</c> (a player's report, 2026-10-04).
        /// </summary>
        private static readonly string[][] DriverTrackingPairs = { new[] { "vrlink", "cv" } };

        /// <summary>
        /// Whether SteamVR's last seen headset can be the one on <paramref name="trackingSystem"/>: its driver is the tracking
        /// system or contains it (<c>oculus_virtualdesktop</c> on <c>oculus</c>), or is paired with it in
        /// <see cref="DriverTrackingPairs"/>; either unknown counts as the same.
        /// </summary>
        private static bool SameHeadset(SteamVrHeadset seen, string trackingSystem)
        {
            if (seen == null || string.IsNullOrWhiteSpace(seen.Model)) return false;
            if (string.IsNullOrWhiteSpace(seen.Driver) || trackingSystem == null) return true;
            var driver = seen.Driver.Trim();
            return driver.IndexOf(trackingSystem, StringComparison.OrdinalIgnoreCase) >= 0
                || trackingSystem.IndexOf(driver, StringComparison.OrdinalIgnoreCase) >= 0
                || DriverTrackingPairs.Any(p => string.Equals(p[0], driver, StringComparison.OrdinalIgnoreCase)
                    && string.Equals(p[1], trackingSystem, StringComparison.OrdinalIgnoreCase));
        }

        /// <summary>"Valve Corporation Index"; the model alone when it already starts with its maker ("Oculus Quest2").</summary>
        private static string MakerAndModel(SteamVrHeadset seen)
        {
            var model = seen.Model.Trim();
            var maker = (seen.Manufacturer ?? string.Empty).Trim();
            if (maker.Length == 0 || model.StartsWith(maker, StringComparison.OrdinalIgnoreCase)) return model;
            return maker + " " + model;
        }

        internal static string Capitalised(string s) =>
            string.IsNullOrEmpty(s) ? s : char.ToUpperInvariant(s[0]) + s.Substring(1);
    }
}
