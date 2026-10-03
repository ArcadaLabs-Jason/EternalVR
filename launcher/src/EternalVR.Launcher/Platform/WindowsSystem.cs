using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Security.Principal;
using EternalVR.Launcher.Core.Preflight;
using EternalVR.Launcher.Core.Report;
using Microsoft.Win32;

namespace EternalVR.Launcher.Platform
{
    /// <summary>
    /// Read-only queries of the machine: registry values, processes, elevation and known folders. Nothing
    /// here writes anything.
    /// </summary>
    public static class WindowsSystem
    {
        /// <summary>
        /// The desktop's displays (their whole areas, physical pixels: the launcher is per-monitor DPI aware
        /// through its manifest), for the stereo window.
        /// </summary>
        public static IReadOnlyList<Core.Launch.DisplayArea> Displays()
        {
            try
            {
                return System.Windows.Forms.Screen.AllScreens
                    .Select(d => new Core.Launch.DisplayArea(d.DeviceName, d.Bounds.X, d.Bounds.Y, d.Bounds.Width, d.Bounds.Height, d.Primary,
                        IsVirtualDisplay(d.DeviceName)))
                    .ToList();
            }
            catch (Exception e) when (e is Win32Exception || e is InvalidOperationException)
            {
                return new Core.Launch.DisplayArea[0];
            }
        }

        /// <summary>Whether a display (<c>\\.\DISPLAYn</c>) is a virtual one: its adapter's name or its monitor's id (read only).</summary>
        private static bool IsVirtualDisplay(string deviceName)
        {
            string adapter = null, monitor = null;
            for (uint i = 0; ; i++)
            {
                var dd = new DisplayDevice { cb = Marshal.SizeOf(typeof(DisplayDevice)) };
                if (!EnumDisplayDevices(null, i, ref dd, 0)) break;
                if (string.Equals(dd.DeviceName, deviceName, StringComparison.OrdinalIgnoreCase))
                {
                    adapter = dd.DeviceString;
                    break;
                }
            }
            var md = new DisplayDevice { cb = Marshal.SizeOf(typeof(DisplayDevice)) };
            if (EnumDisplayDevices(deviceName, 0, ref md, 0)) monitor = md.DeviceID;
            return Core.Launch.VirtualDisplays.IsVirtual(adapter, monitor);
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct DisplayDevice
        {
            public int cb;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string DeviceName;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string DeviceString;
            public int StateFlags;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string DeviceID;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string DeviceKey;
        }

        [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "EnumDisplayDevicesW")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool EnumDisplayDevices(string device, uint index, ref DisplayDevice displayDevice, uint flags);

        public static bool IsElevated()
        {
            using (var identity = WindowsIdentity.GetCurrent())
                return new WindowsPrincipal(identity).IsInRole(WindowsBuiltInRole.Administrator);
        }

        public static bool IsProcessRunning(string name) => RunningProcessIds(name).Count > 0;

        /// <summary>The running processes of these names, with their start times when Windows gives them.</summary>
        public static IReadOnlyList<SeenProcess> RunningProcesses(IEnumerable<string> names)
        {
            var list = new List<SeenProcess>();
            foreach (var name in names)
                foreach (var p in Process.GetProcessesByName(name))
                {
                    DateTime? started = null;
                    try { started = p.StartTime; }
                    catch (Exception e) when (e is System.ComponentModel.Win32Exception || e is InvalidOperationException || e is NotSupportedException) { }
                    list.Add(new SeenProcess(p.ProcessName, p.Id, started));
                    p.Dispose();
                }
            return list;
        }

        public static IReadOnlyList<int> RunningProcessIds(string name)
        {
            var list = new List<int>();
            foreach (var p in Process.GetProcessesByName(name))
            {
                list.Add(p.Id);
                p.Dispose();
            }
            return list;
        }

        /// <summary>Steam's folder from <c>HKCU\Software\Valve\Steam\SteamPath</c>, else the default.</summary>
        public static string SteamRoot()
        {
            var path = ReadString(RegistryHive.CurrentUser, @"Software\Valve\Steam", "SteamPath");
            if (!string.IsNullOrEmpty(path)) return Path.GetFullPath(path.Replace('/', '\\'));
            var fallback = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "Steam");
            return Directory.Exists(fallback) ? fallback : null;
        }

        /// <summary>
        /// The logged-in Steam account (<c>HKCU\Software\Valve\Steam\ActiveProcess\ActiveUser</c>):
        /// its account ID, "0" when Steam reports nobody logged in, null when the value is absent.
        /// </summary>
        public static string SteamActiveUser()
        {
            var value = ReadValue(RegistryHive.CurrentUser, @"Software\Valve\Steam\ActiveProcess", "ActiveUser");
            if (value is int i) return unchecked((uint)i).ToString();
            return null;
        }

        /// <summary>The roots of the ready fixed drives (where Gaming Services can install games).</summary>
        public static IReadOnlyList<string> FixedDriveRoots()
        {
            var list = new List<string>();
            foreach (var d in DriveInfo.GetDrives())
            {
                try { if (d.DriveType == DriveType.Fixed && d.IsReady) list.Add(d.RootDirectory.FullName); }
                catch (Exception e) when (e is IOException || e is UnauthorizedAccessException) { }
            }
            return list;
        }

        public static string ActiveOpenXrRuntime() =>
            ReadString(RegistryHive.LocalMachine, @"SOFTWARE\Khronos\OpenXR\1", "ActiveRuntime");

        public static IReadOnlyList<string> AvailableOpenXrRuntimes() =>
            ValueNames(RegistryHive.LocalMachine, @"SOFTWARE\Khronos\OpenXR\1\AvailableRuntimes");

        /// <summary>HwSchMode (2 = hardware-accelerated GPU scheduling on); null when absent.</summary>
        public static int? HagsMode() =>
            ReadValue(RegistryHive.LocalMachine, @"SYSTEM\CurrentControlSet\Control\GraphicsDrivers", "HwSchMode") as int?;

        /// <summary>The product version in a DLL's version resource, read as data (the DLL is not loaded); null when it has none.</summary>
        public static string ProductVersion(string path)
        {
            try { return File.Exists(path) ? FileVersionInfo.GetVersionInfo(path).ProductVersion : null; }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException) { return null; }
        }

        /// <summary>The CPU's name (the first processor's ProcessorNameString) and the logical processors Windows
        /// sees, e.g. "AMD Ryzen 7 7800X3D 8-Core Processor, 16 logical processors".</summary>
        public static string CpuDescription()
        {
            var name = ReadString(RegistryHive.LocalMachine, @"HARDWARE\DESCRIPTION\System\CentralProcessor\0", "ProcessorNameString");
            name = string.IsNullOrWhiteSpace(name) ? "unknown CPU" : string.Join(" ", name.Split((char[])null, StringSplitOptions.RemoveEmptyEntries));
            return $"{name}, {Environment.ProcessorCount} logical processors";
        }

        /// <summary>The installed memory in GB (GlobalMemoryStatusEx's total physical memory); null when it fails.</summary>
        public static string MemoryDescription()
        {
            var status = new MemoryStatusEx { Length = (uint)Marshal.SizeOf(typeof(MemoryStatusEx)) };
            if (!GlobalMemoryStatusEx(ref status)) return null;
            return $"{status.TotalPhys / (1024.0 * 1024 * 1024):0.0} GB";
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct MemoryStatusEx
        {
            public uint Length;
            public uint MemoryLoad;
            public ulong TotalPhys;
            public ulong AvailPhys;
            public ulong TotalPageFile;
            public ulong AvailPageFile;
            public ulong TotalVirtual;
            public ulong AvailVirtual;
            public ulong AvailExtendedVirtual;
        }

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool GlobalMemoryStatusEx(ref MemoryStatusEx buffer);

        /// <summary>The Windows edition, release and build, e.g. "Windows 10 Pro 24H2 (build 26100.4061)".</summary>
        public static string OsDescription()
        {
            const string key = @"SOFTWARE\Microsoft\Windows NT\CurrentVersion";
            var name = ReadString(RegistryHive.LocalMachine, key, "ProductName") ?? "Windows";
            var release = ReadString(RegistryHive.LocalMachine, key, "DisplayVersion");
            var build = ReadString(RegistryHive.LocalMachine, key, "CurrentBuildNumber");
            var ubr = ReadValue(RegistryHive.LocalMachine, key, "UBR") as int?;
            // ProductName still says "Windows 10" on Windows 11; the build number tells them apart (22000 and up is 11).
            if (int.TryParse(build, out var number) && number >= 22000) name = name.Replace("Windows 10", "Windows 11");
            return $"{name}{(release == null ? string.Empty : " " + release)} (build {build ?? "?"}{(ubr.HasValue ? "." + ubr.Value : string.Empty)})"
                + $", {(Environment.Is64BitOperatingSystem ? "64" : "32")}-bit";
        }

        /// <summary>Display adapters from the display class key (read only): name, driver version and date.</summary>
        public static IReadOnlyList<string> DisplayAdapters()
        {
            const string classKey = @"SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}";
            var list = new List<string>();
            try
            {
                using (var baseKey = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry64))
                using (var k = baseKey.OpenSubKey(classKey, writable: false))
                {
                    if (k == null) return list;
                    foreach (var sub in k.GetSubKeyNames().Where(n => n.Length == 4 && n.All(char.IsDigit)))
                    {
                        try
                        {
                            using (var a = k.OpenSubKey(sub, writable: false))
                            {
                                var desc = a?.GetValue("DriverDesc") as string;
                                if (string.IsNullOrEmpty(desc)) continue;
                                list.Add($"{desc}, driver {a.GetValue("DriverVersion") as string ?? "?"} ({a.GetValue("DriverDate") as string ?? "?"})");
                            }
                        }
                        catch (System.Security.SecurityException) { }
                        catch (UnauthorizedAccessException) { }
                    }
                }
            }
            catch (System.Security.SecurityException) { }
            catch (UnauthorizedAccessException) { }
            return list.Distinct().ToList();
        }

        /// <summary>Display adapters with their dedicated memory; hardware ones match a PCI device ID.</summary>
        public static IReadOnlyList<Core.Preflight.GpuAdapter> GpuAdapters()
        {
            const string classKey = @"SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}";
            var list = new List<Core.Preflight.GpuAdapter>();
            try
            {
                using (var baseKey = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry64))
                using (var k = baseKey.OpenSubKey(classKey, writable: false))
                {
                    if (k == null) return list;
                    foreach (var sub in k.GetSubKeyNames().Where(n => n.Length == 4 && n.All(char.IsDigit)))
                    {
                        try
                        {
                            using (var a = k.OpenSubKey(sub, writable: false))
                            {
                                var desc = a?.GetValue("DriverDesc") as string;
                                if (string.IsNullOrEmpty(desc)) continue;
                                var device = a.GetValue("MatchingDeviceId") as string ?? string.Empty;
                                long? memory = null;
                                var qw = a.GetValue("HardwareInformation.qwMemorySize");
                                if (qw is long l) memory = l;
                                else if (qw is byte[] b && b.Length >= 8) memory = BitConverter.ToInt64(b, 0);
                                else if (a.GetValue("HardwareInformation.MemorySize") is int i) memory = unchecked((uint)i);
                                else if (a.GetValue("HardwareInformation.MemorySize") is byte[] m && m.Length >= 4) memory = BitConverter.ToUInt32(m, 0);
                                list.Add(new Core.Preflight.GpuAdapter(desc, memory, device.StartsWith("pci\\", StringComparison.OrdinalIgnoreCase)));
                            }
                        }
                        catch (System.Security.SecurityException) { }
                        catch (UnauthorizedAccessException) { }
                    }
                }
            }
            catch (System.Security.SecurityException) { }
            catch (UnauthorizedAccessException) { }
            return list;
        }

        /// <summary>The file version of the system's Vulkan loader (System32\vulkan-1.dll), read as data; null when absent.</summary>
        public static string SystemVulkanLoaderVersion()
        {
            var path = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), "vulkan-1.dll");
            try { return File.Exists(path) ? FileVersionInfo.GetVersionInfo(path).FileVersion : null; }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException) { return null; }
        }

        /// <summary>Implicit Vulkan and OpenXR layers registered in HKLM and HKCU, with their manifests read.</summary>
        public static IReadOnlyList<InstalledLayer> ImplicitLayers()
        {
            var result = new List<InstalledLayer>();
            void Collect(LayerApi api, RegistryHive hive, string key)
            {
                // A key this account may not read lists nothing (as ReadValue does), never a crash of the checks.
                try { CollectFrom(api, hive, key); }
                catch (System.Security.SecurityException) { }
                catch (UnauthorizedAccessException) { }
            }

            void CollectFrom(LayerApi api, RegistryHive hive, string key)
            {
                using (var baseKey = RegistryKey.OpenBaseKey(hive, RegistryView.Registry64))
                using (var k = baseKey.OpenSubKey(key, writable: false))
                {
                    if (k == null) return;
                    foreach (var manifest in k.GetValueNames())
                    {
                        bool enabled = k.GetValue(manifest) is int v && v == 0;
                        string text = null;
                        try { if (File.Exists(manifest)) text = File.ReadAllText(manifest); }
                        catch (IOException) { }
                        catch (UnauthorizedAccessException) { }
                        var scope = hive == RegistryHive.CurrentUser ? "HKCU" : "HKLM";
                        // A manifest that cannot be read cannot be loaded either, so it counts as disabled.
                        result.AddRange(text == null
                            ? new[] { new InstalledLayer(api, manifest, manifest, scope, false, null, null) }
                            : InstalledLayer.FromManifest(api, manifest, text, scope, enabled));
                    }
                }
            }

            Collect(LayerApi.Vulkan, RegistryHive.LocalMachine, @"SOFTWARE\Khronos\Vulkan\ImplicitLayers");
            Collect(LayerApi.Vulkan, RegistryHive.CurrentUser, @"SOFTWARE\Khronos\Vulkan\ImplicitLayers");
            Collect(LayerApi.OpenXR, RegistryHive.LocalMachine, @"SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit");
            Collect(LayerApi.OpenXR, RegistryHive.CurrentUser, @"SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit");
            return result;
        }

        /// <summary>The user's Saved Games folder (FOLDERID_SavedGames), else %USERPROFILE%\Saved Games.</summary>
        public static string SavedGamesFolder()
        {
            var id = new Guid("4C5C32FF-BB9D-43b0-B5B4-2D72E54EAAA4");
            if (SHGetKnownFolderPath(ref id, 0, IntPtr.Zero, out var ptr) == 0)
            {
                try { return Marshal.PtrToStringUni(ptr); }
                finally { Marshal.FreeCoTaskMem(ptr); }
            }
            return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "Saved Games");
        }

        public static bool IsUnderProgramFiles(string path)
        {
            var full = Path.GetFullPath(path).TrimEnd('\\') + "\\";
            foreach (var pf in new[] { Environment.SpecialFolder.ProgramFiles, Environment.SpecialFolder.ProgramFilesX86 })
            {
                var root = Environment.GetFolderPath(pf);
                if (!string.IsNullOrEmpty(root) && full.StartsWith(root.TrimEnd('\\') + "\\", StringComparison.OrdinalIgnoreCase)) return true;
            }
            return false;
        }

        private static string ReadString(RegistryHive hive, string key, string name) => ReadValue(hive, key, name) as string;

        private static object ReadValue(RegistryHive hive, string key, string name)
        {
            try
            {
                using (var baseKey = RegistryKey.OpenBaseKey(hive, RegistryView.Registry64))
                using (var k = baseKey.OpenSubKey(key, writable: false))
                    return k?.GetValue(name);
            }
            catch (System.Security.SecurityException) { return null; }
            catch (UnauthorizedAccessException) { return null; }
        }

        private static IReadOnlyList<string> ValueNames(RegistryHive hive, string key)
        {
            try
            {
                using (var baseKey = RegistryKey.OpenBaseKey(hive, RegistryView.Registry64))
                using (var k = baseKey.OpenSubKey(key, writable: false))
                    return k?.GetValueNames().ToList() ?? new List<string>();
            }
            catch (System.Security.SecurityException) { return new List<string>(); }
        }

        [DllImport("shell32.dll")]
        private static extern int SHGetKnownFolderPath(ref Guid rfid, uint dwFlags, IntPtr hToken, out IntPtr ppszPath);
    }
}
