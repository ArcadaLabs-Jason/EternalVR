using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

namespace EternalVR.Launcher.Core.Launch
{
    /// <summary>What <see cref="OpenXrProbe"/> found: the runtime's view limits, or why there are none.</summary>
    public sealed class OpenXrProbeResult
    {
        /// <summary>The views' limits (<see cref="ViewLimits.EyesSideBySide"/> 1); null when the probe failed.</summary>
        public ViewLimits Limits { get; set; }
        public string RuntimeName { get; set; }
        public string SystemName { get; set; }
        /// <summary>Why there are no limits (for the log); null on success.</summary>
        public string Error { get; set; }
        /// <summary>The runtime reported no headset (<c>XR_ERROR_FORM_FACTOR_UNAVAILABLE</c>): usually off or not connected.</summary>
        public bool HeadsetUnavailable { get; set; }

        public bool Ok => Limits != null;

        public static OpenXrProbeResult Failed(string why, bool headsetUnavailable = false) =>
            new OpenXrProbeResult { Error = why, HeadsetUnavailable = headsetUnavailable };

        public override string ToString() => Ok
            ? $"runtime '{RuntimeName}', system '{SystemName}': recommended {Limits.Recommended} per eye, max image {Limits.MaxImageRect}, max swapchain {Limits.MaxSwapchain}"
            : "failed: " + Error;
    }

    /// <summary>
    /// Asks the OpenXR runtime the game will use for its recommended eye size before the game starts, so the game can
    /// start at its final render size (docs/rig-findings/render-size.md, section 7). It only creates an instance
    /// (no graphics extension, no session), reads the system and the stereo views, and destroys it. Windows only;
    /// the loader is the layer's own <c>openxr_loader.dll</c>, loaded by full path.
    /// Struct layouts follow openxr.h of OpenXR SDK 1.1.63 (the layer build's
    /// <c>_deps/openxr-loader-1.1.63/include/openxr/openxr.h</c>, lines cited per struct); x64 only.
    /// </summary>
    public static class OpenXrProbe
    {
        /// <summary>SteamVR and Windows Mixed Reality can take this long to answer from a cold start.</summary>
        public const int DefaultTimeoutMs = 30000;

        /// <summary>
        /// Runs the probe on a worker thread with <paramref name="environment"/> (XR_RUNTIME_JSON, API layer switches)
        /// set in this process for the probe only, and restored afterwards. Never throws.
        /// </summary>
        public static OpenXrProbeResult Run(string loaderPath, IEnumerable<KeyValuePair<string, string>> environment, int timeoutMs = DefaultTimeoutMs)
        {
            if (Environment.OSVersion.Platform != PlatformID.Win32NT || IntPtr.Size != 8)
                return OpenXrProbeResult.Failed("the probe needs 64-bit Windows");
            if (string.IsNullOrEmpty(loaderPath) || !System.IO.File.Exists(loaderPath))
                return OpenXrProbeResult.Failed("the OpenXR loader is missing: " + loaderPath);

            var saved = new List<KeyValuePair<string, string>>();
            OpenXrProbeResult result = null;
            try
            {
                foreach (var kv in environment ?? new KeyValuePair<string, string>[0])
                {
                    saved.Add(new KeyValuePair<string, string>(kv.Key, Environment.GetEnvironmentVariable(kv.Key)));
                    Environment.SetEnvironmentVariable(kv.Key, kv.Value);
                }
                var worker = new Thread(() =>
                {
                    OpenXrProbeResult r;
                    try { r = Probe(loaderPath); }
                    catch (Exception e) { r = OpenXrProbeResult.Failed(e.GetType().Name + ": " + e.Message); }
                    Volatile.Write(ref result, r);
                }) { IsBackground = true, Name = "OpenXR probe" };
                worker.Start();
                // A runtime that hangs is left behind on its background thread; the launch goes on without it.
                if (!worker.Join(timeoutMs))
                    return OpenXrProbeResult.Failed($"the runtime did not answer within {timeoutMs / 1000.0:0.#} s");
                return Volatile.Read(ref result);
            }
            finally
            {
                for (int i = saved.Count - 1; i >= 0; i--) Environment.SetEnvironmentVariable(saved[i].Key, saved[i].Value);
            }
        }

        private static OpenXrProbeResult Probe(string loaderPath)
        {
            // The loader stays loaded: a runtime may keep threads alive after xrDestroyInstance.
            IntPtr module = Native.LoadLibraryExW(loaderPath, IntPtr.Zero, Native.LoadWithAlteredSearchPath);
            if (module == IntPtr.Zero)
                return OpenXrProbeResult.Failed($"loading {loaderPath} failed (Windows error {Marshal.GetLastWin32Error()})");
            var createInstance = Export<XrCreateInstance>(module, "xrCreateInstance");
            var destroyInstance = Export<XrDestroyInstance>(module, "xrDestroyInstance");
            var getInstanceProperties = Export<XrGetInstanceProperties>(module, "xrGetInstanceProperties");
            var getSystem = Export<XrGetSystem>(module, "xrGetSystem");
            var getSystemProperties = Export<XrGetSystemProperties>(module, "xrGetSystemProperties");
            var enumerateViews = Export<XrEnumerateViewConfigurationViews>(module, "xrEnumerateViewConfigurationViews");
            if (createInstance == null || destroyInstance == null || getInstanceProperties == null || getSystem == null
                || getSystemProperties == null || enumerateViews == null)
                return OpenXrProbeResult.Failed("the OpenXR loader lacks a core function");

            var info = new XrInstanceCreateInfo
            {
                type = XrTypeInstanceCreateInfo,
                applicationInfo = new XrApplicationInfo
                {
                    applicationName = Chars("EternalVR Launcher", 128),
                    applicationVersion = 1,
                    engineName = Chars("EternalVR", 128),
                    engineVersion = 1,
                    apiVersion = ApiVersion1_1,
                },
            };
            // As the layer does: OpenXR 1.1, then 1.0 when the runtime has only 1.0.
            int r = createInstance(ref info, out ulong instance);
            if (r == XrErrorApiVersionUnsupported)
            {
                info.applicationInfo.apiVersion = ApiVersion1_0;
                r = createInstance(ref info, out instance);
            }
            if (r < 0) return OpenXrProbeResult.Failed("xrCreateInstance: " + ResultName(r));
            try
            {
                var result = new OpenXrProbeResult();
                var ip = new XrInstanceProperties { type = XrTypeInstanceProperties };
                if (getInstanceProperties(instance, ref ip) >= 0) result.RuntimeName = Text(ip.runtimeName);

                var getInfo = new XrSystemGetInfo { type = XrTypeSystemGetInfo, formFactor = XrFormFactorHeadMountedDisplay };
                r = getSystem(instance, ref getInfo, out ulong systemId);
                if (r < 0)
                    return OpenXrProbeResult.Failed("xrGetSystem: " + ResultName(r), r == XrErrorFormFactorUnavailable);

                var sp = new XrSystemProperties { type = XrTypeSystemProperties };
                r = getSystemProperties(instance, systemId, ref sp);
                if (r < 0) return OpenXrProbeResult.Failed("xrGetSystemProperties: " + ResultName(r));
                result.SystemName = Text(sp.systemName);

                r = enumerateViews(instance, systemId, XrViewConfigurationTypePrimaryStereo, 0, out uint count, IntPtr.Zero);
                if (r < 0 || count == 0 || count > 16)
                    return OpenXrProbeResult.Failed("xrEnumerateViewConfigurationViews: " + (r < 0 ? ResultName(r) : count + " view(s)"));
                int size = Marshal.SizeOf<XrViewConfigurationView>();
                IntPtr views = Marshal.AllocHGlobal(size * (int)count);
                try
                {
                    for (int i = 0; i < count; i++)
                        Marshal.StructureToPtr(new XrViewConfigurationView { type = XrTypeViewConfigurationView }, views + i * size, false);
                    r = enumerateViews(instance, systemId, XrViewConfigurationTypePrimaryStereo, count, out count, views);
                    if (r < 0) return OpenXrProbeResult.Failed("xrEnumerateViewConfigurationViews: " + ResultName(r));
                    // As the layer (presenter_xr.cpp, reportViewLimits): the largest recommendation, the smallest maximum.
                    uint recW = 0, recH = 0, maxW = uint.MaxValue, maxH = uint.MaxValue;
                    for (int i = 0; i < count; i++)
                    {
                        var v = Marshal.PtrToStructure<XrViewConfigurationView>(views + i * size);
                        recW = Math.Max(recW, v.recommendedImageRectWidth);
                        recH = Math.Max(recH, v.recommendedImageRectHeight);
                        maxW = Math.Min(maxW, v.maxImageRectWidth);
                        maxH = Math.Min(maxH, v.maxImageRectHeight);
                    }
                    result.Limits = new ViewLimits
                    {
                        Recommended = new Extent(recW, recH),
                        MaxImageRect = new Extent(maxW, maxH),
                        MaxSwapchain = new Extent(sp.graphicsProperties.maxSwapchainImageWidth, sp.graphicsProperties.maxSwapchainImageHeight),
                    };
                }
                finally { Marshal.FreeHGlobal(views); }
                if (result.Limits.Recommended.IsEmpty) return OpenXrProbeResult.Failed("the runtime recommends an empty view");
                return result;
            }
            finally { destroyInstance(instance); }
        }

        private static T Export<T>(IntPtr module, string name) where T : class
        {
            IntPtr p = Native.GetProcAddress(module, name);
            return p == IntPtr.Zero ? null : Marshal.GetDelegateForFunctionPointer<T>(p);
        }

        private static byte[] Chars(string s, int size)
        {
            var b = new byte[size];
            Encoding.UTF8.GetBytes(s, 0, s.Length, b, 0);
            return b;
        }

        private static string Text(byte[] chars)
        {
            if (chars == null) return string.Empty;
            int n = Array.IndexOf(chars, (byte)0);
            return Encoding.UTF8.GetString(chars, 0, n < 0 ? chars.Length : n);
        }

        public static string ResultName(int r)
        {
            switch (r)
            {
                case -1: return "XR_ERROR_VALIDATION_FAILURE";
                case -2: return "XR_ERROR_RUNTIME_FAILURE";
                case -3: return "XR_ERROR_OUT_OF_MEMORY";
                case XrErrorApiVersionUnsupported: return "XR_ERROR_API_VERSION_UNSUPPORTED";
                case -6: return "XR_ERROR_INITIALIZATION_FAILED";
                case -13: return "XR_ERROR_INSTANCE_LOST";
                case -34: return "XR_ERROR_FORM_FACTOR_UNSUPPORTED";
                case XrErrorFormFactorUnavailable: return "XR_ERROR_FORM_FACTOR_UNAVAILABLE";
                case -41: return "XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED";
                case -51: return "XR_ERROR_RUNTIME_UNAVAILABLE";
                default: return "XrResult " + r;
            }
        }

        // ---- openxr.h (SDK 1.1.63): constants and structs. XRAPI_CALL is __stdcall (openxr_platform_defines.h:31).

        // XR_MAKE_VERSION(major, minor, patch) = major << 48 | minor << 32 | patch (openxr.h:25); patch 63.
        internal const ulong ApiVersion1_0 = (1UL << 48) | 63UL;               // XR_API_VERSION_1_0, openxr.h:32
        internal const ulong ApiVersion1_1 = (1UL << 48) | (1UL << 32) | 63UL; // XR_API_VERSION_1_1, openxr.h:2163
        private const int XrTypeInstanceCreateInfo = 3;         // openxr.h:354
        private const int XrTypeSystemGetInfo = 4;              // openxr.h:355
        private const int XrTypeSystemProperties = 5;           // openxr.h:356
        private const int XrTypeInstanceProperties = 32;        // openxr.h:374
        private const int XrTypeViewConfigurationView = 41;     // openxr.h:381
        private const int XrFormFactorHeadMountedDisplay = 1;   // openxr.h:1115
        private const int XrViewConfigurationTypePrimaryStereo = 2; // openxr.h:1129
        private const int XrErrorApiVersionUnsupported = -4;    // openxr.h:158
        private const int XrErrorFormFactorUnavailable = -35;   // openxr.h:187

        // openxr.h:1332. 272 bytes: name [128], uint32 version, name [128], uint32 version, uint64 apiVersion at 264.
        [StructLayout(LayoutKind.Sequential)]
        internal struct XrApplicationInfo
        {
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 128)] public byte[] applicationName;
            public uint applicationVersion;
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 128)] public byte[] engineName;
            public uint engineVersion;
            public ulong apiVersion;
        }

        // openxr.h:1340. 328 bytes; no API layers or extensions are enabled (counts 0, null lists).
        [StructLayout(LayoutKind.Sequential)]
        internal struct XrInstanceCreateInfo
        {
            public int type;
            public IntPtr next;
            public ulong createFlags;
            public XrApplicationInfo applicationInfo;
            public uint enabledApiLayerCount;
            public IntPtr enabledApiLayerNames;
            public uint enabledExtensionCount;
            public IntPtr enabledExtensionNames;
        }

        // openxr.h:1351. 152 bytes.
        [StructLayout(LayoutKind.Sequential)]
        internal struct XrInstanceProperties
        {
            public int type;
            public IntPtr next;
            public ulong runtimeVersion;
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 128)] public byte[] runtimeName;
        }

        // openxr.h:1364. 24 bytes.
        [StructLayout(LayoutKind.Sequential)]
        internal struct XrSystemGetInfo
        {
            public int type;
            public IntPtr next;
            public int formFactor;
        }

        // openxr.h:1370. Height first, then width.
        [StructLayout(LayoutKind.Sequential)]
        internal struct XrSystemGraphicsProperties
        {
            public uint maxSwapchainImageHeight;
            public uint maxSwapchainImageWidth;
            public uint maxLayerCount;
        }

        // openxr.h:1376 (XrBool32 is uint32).
        [StructLayout(LayoutKind.Sequential)]
        internal struct XrSystemTrackingProperties
        {
            public uint orientationTracking;
            public uint positionTracking;
        }

        // openxr.h:1381. 304 bytes: systemId at 16, vendorId at 24, name [256] at 28, graphics at 284, tracking at 296.
        [StructLayout(LayoutKind.Sequential)]
        internal struct XrSystemProperties
        {
            public int type;
            public IntPtr next;
            public ulong systemId;
            public uint vendorId;
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 256)] public byte[] systemName;
            public XrSystemGraphicsProperties graphicsProperties;
            public XrSystemTrackingProperties trackingProperties;
        }

        // openxr.h:1459. 40 bytes: width, max width, height, max height (interleaved), sample counts.
        [StructLayout(LayoutKind.Sequential)]
        internal struct XrViewConfigurationView
        {
            public int type;
            public IntPtr next;
            public uint recommendedImageRectWidth;
            public uint maxImageRectWidth;
            public uint recommendedImageRectHeight;
            public uint maxImageRectHeight;
            public uint recommendedSwapchainSampleCount;
            public uint maxSwapchainSampleCount;
        }

        // XrInstance and XrSystemId are 64-bit handles (XR_DEFINE_HANDLE, XR_DEFINE_ATOM) on 64-bit Windows.
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int XrCreateInstance(ref XrInstanceCreateInfo info, out ulong instance);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int XrDestroyInstance(ulong instance);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int XrGetInstanceProperties(ulong instance, ref XrInstanceProperties properties);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int XrGetSystem(ulong instance, ref XrSystemGetInfo info, out ulong systemId);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int XrGetSystemProperties(ulong instance, ulong systemId, ref XrSystemProperties properties);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int XrEnumerateViewConfigurationViews(ulong instance, ulong systemId, int viewConfigurationType,
            uint capacity, out uint count, IntPtr views);

        private static class Native
        {
            public const uint LoadWithAlteredSearchPath = 0x00000008;

            [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
            public static extern IntPtr LoadLibraryExW(string path, IntPtr file, uint flags);

            [DllImport("kernel32.dll", CharSet = CharSet.Ansi, ExactSpelling = true, SetLastError = true)]
            public static extern IntPtr GetProcAddress(IntPtr module, string name);
        }
    }
}
