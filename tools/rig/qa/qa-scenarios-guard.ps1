# The multiplayer guard's positive controls (REQ-16, T-109, T-114, docs/rig-findings/mp-guard.md), offline:
# nothing here opens a BATTLEMODE screen or contacts a server. Dot-sourced by qa-suite.ps1 after
# qa-scenarios-controls.ps1: added to $script:QaScenarios before the launcher's end-to-end suite. The negative
# side (armed, never tripped or refused in the campaign) is checked on every game scenario by qa-suite.ps1.
#
#   mp-guard-trip     a game run with the guard's test trip (ETERNALVR_GUARD_TEST_TRIP_MS) after the map is in play
#   mp-guard-standin  no game: a stand-in Vulkan program named DOOMEternalx64vk.exe loads the layer with
#                     multiplayer arguments, as the game's instance in an unknown exe, and under another name

# Milliseconds from the guard arming (the game's instance, before the main menu) to the test trip: past the map
# load and head aim (about 19 s on the rig), so the trip lands in play.
$script:QaGuardTripMs = 45000

# The stand-in: creates one Vulkan instance, as the game's (app 'DOOMEternal', engine 'idTech', API 1.1) when
# EVR_STANDIN_AS_GAME=1, else with no application info; prints the vkCreateInstance result. Compiled by Add-Type,
# so it needs no build; its command line is what the layer screens (the program path is skipped). The suite runs under
# Windows PowerShell 5.1 (powershell.exe), where Add-Type builds a .NET Framework exe; under PowerShell 7 it would build
# one against .NET that does not run on its own.
$script:QaStandInSource = @'
using System;
using System.Runtime.InteropServices;
public static class EvrGuardStandIn {
    [StructLayout(LayoutKind.Sequential)]
    struct VkApplicationInfo {
        public int sType; public IntPtr pNext; public IntPtr pApplicationName; public uint applicationVersion;
        public IntPtr pEngineName; public uint engineVersion; public uint apiVersion;
    }
    [StructLayout(LayoutKind.Sequential)]
    struct VkInstanceCreateInfo {
        public int sType; public IntPtr pNext; public uint flags; public IntPtr pApplicationInfo;
        public uint enabledLayerCount; public IntPtr ppEnabledLayerNames;
        public uint enabledExtensionCount; public IntPtr ppEnabledExtensionNames;
    }
    [DllImport("vulkan-1.dll")] static extern int vkCreateInstance(ref VkInstanceCreateInfo info, IntPtr allocator, out IntPtr instance);
    [DllImport("vulkan-1.dll")] static extern void vkDestroyInstance(IntPtr instance, IntPtr allocator);
    public static int Main(string[] args) {
        var info = new VkInstanceCreateInfo();
        info.sType = 1;
        if (Environment.GetEnvironmentVariable("EVR_STANDIN_AS_GAME") == "1") {
            var app = new VkApplicationInfo();
            app.pApplicationName = Marshal.StringToHGlobalAnsi("DOOMEternal");
            app.pEngineName = Marshal.StringToHGlobalAnsi("idTech");
            app.apiVersion = (1u << 22) | (1u << 12);
            info.pApplicationInfo = Marshal.AllocHGlobal(Marshal.SizeOf(typeof(VkApplicationInfo)));
            Marshal.StructureToPtr(app, info.pApplicationInfo, false);
        }
        IntPtr instance;
        int result = vkCreateInstance(ref info, IntPtr.Zero, out instance);
        Console.WriteLine("vkCreateInstance " + result);
        if (result == 0) vkDestroyInstance(instance, IntPtr.Zero);
        return result == 0 ? 0 : 1;
    }
}
'@

# The stand-in's cases: a name for the report, the program's file name, its arguments, whether it poses as the
# game's instance, and the checks on its layer log ($null: no layer log may be written at all).
$script:QaStandInCases = @(
    @{ Name = 'connect-lobby'; Exe = 'DOOMEternalx64vk.exe'; Args = '+connect_lobby 1'; AsGame = $true
       Present = @('mp guard: TRIPPED by command line (argument "+connect" requests a multiplayer connection)',
                   'mp guard: the layer does not load; EternalVR is single-player only')
       Absent = @('EternalVR layer loaded', 'mp guard: armed') },
    @{ Name = 'pvp-map'; Exe = 'DOOMEternalx64vk.exe'; Args = '+map game/pvp/pvp_laser/pvp_laser'; AsGame = $true
       Present = @('mp guard: TRIPPED by command line (argument "game/pvp/" requests a BATTLEMODE map)',
                   'mp guard: the layer does not load; EternalVR is single-player only')
       Absent = @('EternalVR layer loaded', 'mp guard: armed') },
    @{ Name = 'unknown-exe'; Exe = 'DOOMEternalx64vk.exe'; Args = '+com_skipIntroVideo 1'; AsGame = $true
       Present = @('EternalVR layer loaded', "vkCreateInstance: app 'DOOMEternal' engine 'idTech'",
                   'mp guard: map load MISSING', 'mp guard: REFUSED: a detection point is missing')
       Absent = @('mp guard: armed', 'mp guard: TRIPPED') },
    @{ Name = 'not-the-game'; Exe = 'DOOMEternalx64vk.exe'; Args = '+com_skipIntroVideo 1'; AsGame = $false
       Present = @('EternalVR layer loaded', "(not the game's; pass-through)")
       Absent = @('mp guard:') },
    @{ Name = 'other-process'; Exe = 'evr-guard-standin.exe'; Args = '+com_skipIntroVideo 1'; AsGame = $true
       Present = $null; Absent = $null }
)

# Runs every stand-in case against the layer build in -LayerSrc; results go to the scenario's report row.
function Invoke-QaStandInScenario($Scenario, $Ctx, [string]$LayerSrc) {
    $principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
    if ($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        # The Vulkan loader ignores VK_ADD_IMPLICIT_LAYER_PATH in an elevated process: nothing would be tested.
        Add-QaResult $Ctx 'run unelevated' $false 'the suite runs elevated; the loader ignores the layer path there'
        return
    }
    New-QaStage $Ctx $LayerSrc
    $bin = Join-Path $Ctx.Dir 'standin'
    if (Test-Path -LiteralPath $bin) { Remove-Item -LiteralPath $bin -Recurse -Force }
    New-Item -ItemType Directory -Force $bin | Out-Null
    $built = Join-Path $bin 'standin.exe'
    try { Add-Type -TypeDefinition $script:QaStandInSource -OutputAssembly $built -OutputType ConsoleApplication -ErrorAction Stop }
    catch { Add-QaResult $Ctx 'stand-in built' $false $_.Exception.Message; return }
    foreach ($case in $script:QaStandInCases) {
        $exe = Join-Path $bin $case.Exe
        if (-not (Test-Path -LiteralPath $exe)) { Copy-Item -LiteralPath $built $exe }
        $logs = Join-Path $Ctx.Dir "logs-$($case.Name)"
        New-Item -ItemType Directory -Force $logs | Out-Null
        $psi = New-Object Diagnostics.ProcessStartInfo $exe, $case.Args
        $psi.UseShellExecute = $false
        $psi.RedirectStandardOutput = $true
        $psi.EnvironmentVariables['VK_ADD_IMPLICIT_LAYER_PATH'] = $Ctx.Stage
        $psi.EnvironmentVariables['ETERNALVR_ENABLE_LAYER'] = '1'
        $psi.EnvironmentVariables['ETERNALVR_LOG_DIR'] = $logs
        $psi.EnvironmentVariables['EVR_STANDIN_AS_GAME'] = $(if ($case.AsGame) { '1' } else { '0' })
        $p = [Diagnostics.Process]::Start($psi)
        # Read without blocking, so a stand-in that hangs is killed after 30 s instead of holding the suite.
        $read = $p.StandardOutput.ReadToEndAsync()
        $killed = -not $p.WaitForExit(30000)
        if ($killed) { $p.Kill(); [void]$p.WaitForExit(5000) }
        $out = if ($read.Wait(5000)) { $read.Result.Trim() } else { '' }
        if ($killed) { $out += ' (killed after 30 s)' }
        Set-Content -LiteralPath (Join-Path $logs 'standin.txt') -Value "$($case.Exe) $($case.Args): $out" -Encoding utf8
        # Refused or not, the program's own instance is made: the layer only steps aside.
        Add-QaResult $Ctx "$($case.Name): the program's vkCreateInstance succeeds" ($out -eq 'vkCreateInstance 0') $out
        $log = Get-ChildItem -LiteralPath $logs -Filter 'eternalvr-2*.log' -File -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($null -eq $case.Present) {
            Add-QaResult $Ctx "$($case.Name): no layer log ($($case.Exe) is not the game; the layer is inert)" ($null -eq $log) $logs
            continue
        }
        if (-not $log) { Add-QaResult $Ctx "$($case.Name): layer log written" $false "no eternalvr-2*.log in $logs"; continue }
        Copy-Item -LiteralPath $log.FullName (Join-Path $Ctx.Dir "eternalvr-$($case.Name).log")
        $text = Read-QaShared $log.FullName
        foreach ($s in $case.Present) { Add-QaResult $Ctx "$($case.Name): present: $s" $text.Contains($s) }
        foreach ($s in $case.Absent) { Add-QaResult $Ctx "$($case.Name): absent: $s" (-not $text.Contains($s)) }
    }
}

# The index of the first log line containing $Text, or -1.
function Get-QaLineIndex($Ctx, [string]$Text) {
    $lines = @((Get-QaLogText $Ctx) -split "`r?`n")
    for ($i = 0; $i -lt $lines.Count; ++$i) { if ($lines[$i].Contains($Text)) { return $i } }
    return -1
}

$guardScenarios = @(
    @{
        Name = 'mp-guard-trip'; Kind = 'game'; GuardTrips = $true
        Proves = "The guard's positive control in the game: armed, the campaign map loads as single-player, then ETERNALVR_GUARD_TEST_TRIP_MS trips it in play; the game keeps running 25 s on, the headset falls back to the flat cinema screen (no new head-tracked frame between the last two 10 s xr lines after the trip, more on the screen), the head stops being driven and the cvars the layer wrote are set back; the trip is the only one"
        Env = @("ETERNALVR_GUARD_TEST_TRIP_MS=$script:QaGuardTripMs")
        Timeline = {
            param($c)
            $tripped = Wait-QaLog $c 'mp guard: TRIPPED by test' 90
            Add-QaResult $c 'the test trip landed within 90 s of being in the map' $tripped
            # 25 s: two of the presenter's 10 s summaries after the trip.
            if ($tripped) { Wait-QaSeconds $c 25; Save-QaShot $c 'after-trip'; Save-QaXrShot $c 'after-trip' }
        }
        Asserts = {
            param($c)
            Test-QaPresent $c "mp guard: test trip in $script:QaGuardTripMs ms (ETERNALVR_GUARD_TEST_TRIP_MS)"
            Test-QaCount $c 'mp guard: TRIPPED by' 1
            Test-QaPresent $c 'mp guard: TRIPPED by test (ETERNALVR_GUARD_TEST_TRIP_MS)'
            $aim = Get-QaLineIndex $c 'aim: head aim on'
            $trip = Get-QaLineIndex $c 'mp guard: TRIPPED by test'
            Add-QaResult $c 'the trip came after head aim was on (in play)' ($aim -ge 0 -and $trip -gt $aim) "line $aim, then line $trip"
            Test-QaPresent $c 'xr: multiplayer guard tripped: the game is shown on the flat cinema screen'
            Test-QaPresent $c "head: the multiplayer guard is tripped; the game's view and aim are no longer changed"
            Test-QaPresent $c 'cvar restore: the multiplayer guard tripped:'
            # The xr summary every 10 s counts headset frames since the start: after the trip the head-tracked count
            # stands still and the cinema screen's grows.
            $lines = @((Get-QaLogText $c) -split "`r?`n")
            $after = @()
            if ($trip -ge 0) {
                $after = @($lines[$trip..($lines.Count - 1)] | Where-Object { $_ -match 'xr: \d+ frame\(s\), .* (\d+) head-tracked, (\d+) on the screen' } |
                    ForEach-Object { $null = $_ -match '(\d+) head-tracked, (\d+) on the screen'; , @([int64]$Matches[1], [int64]$Matches[2]) })
            }
            $detail = ($after | ForEach-Object { "$($_[0]) head-tracked / $($_[1]) on the screen" }) -join ', '
            $still = $after.Count -ge 2 -and $after[-1][0] -eq $after[-2][0] -and $after[-1][1] -gt $after[-2][1]
            Add-QaResult $c 'after the trip the headset gets only the cinema screen (last two xr summaries)' $still $(if ($detail) { $detail } else { 'fewer than two xr summaries after the trip' })
            Test-QaAbsent $c 'mp guard: REFUSED'
        }
    },
    @{
        Name = 'mp-guard-standin'; Kind = 'standin'
        Proves = "The guard's offline refusals in the layer build, without the game: a stand-in Vulkan program named DOOMEternalx64vk.exe gets +connect_lobby 1, then a game/pvp/ map: the command line trips the guard and the layer does not load; as the game's instance in an exe that is not a known build: every detection point MISSING and REFUSED (fail closed); not the game's instance: pass-through, no guard; under another file name: no layer log at all. The program's own instance is made every time"
    }
)

# Before the launcher's end-to-end suite, which stays last.
$e2e = @($script:QaScenarios | Where-Object { $_.Kind -eq 'e2e' })
$script:QaScenarios = @($script:QaScenarios | Where-Object { $_.Kind -ne 'e2e' }) + $guardScenarios + $e2e
