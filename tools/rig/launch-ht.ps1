<#
.SYNOPSIS
Launches a head-tracked run with a staged layer build into e1m1_intro and writes a launch timeline.

.DESCRIPTION
Starts the game through run.ps1 with the layer at -Layer (a staged copy of build\windows-msvc\src\vkcore:
the game locks the DLL), the settings from docs/VR_HEAD_TRACKED.md and the layer's in-process cinematic
skip. A background job waits for the game window, captures it every ~1.2 s into <layer>-logs\shot-NNN.png
and stops once two consecutive captures show the HUD (the cyan health block in the lower left) or after
-WatchSeconds. The layer log's milestones are then converted to wall-clock time and merged into
timeline.txt in the run folder, and collect.ps1 takes a screenshot. The game keeps running: end it with
stop.ps1.

  launch-ht.ps1 -Layer <workspace>\tmp-vr\ht4 -Label ht4 -DisplayWidth 3840 -DisplayHeight 2160
  launch-ht.ps1 ... -XrRuntimeJson <workspace>\tools\bin\openxr-simulator\openxr_simulator.json

.PARAMETER Layer
Folder holding EternalVR.dll, VK_LAYER_ETERNALVR.json and openxr_loader.dll. The layer log goes to
<Layer>-logs.

.PARAMETER DisplayWidth
With -DisplayWidth/-DisplayHeight the run uses the rig's virtual display at that mode, and the game window
is placed on it with ETERNALVR_WINDOW (-Window x,y,width,height; by default at the virtual display's
top-left corner as the live layout reports it, client 2560x2100). The layout goes into the timeline.
Without them the run uses no virtual display (-NoDisplay).

.PARAMETER WindowSize
WIDTHxHEIGHT for the automatic window (-Window auto on the virtual display), for example 2064x2100: with
-Stereo each eye renders at the window's size (S4 compares two sizes).

.PARAMETER XrRuntimeJson
An OpenXR runtime manifest for the game process only (XR_RUNTIME_JSON), for example OpenXR-Simulator
when no headset is connected. The system's active runtime is never changed.

.PARAMETER Stereo
Route S, synchronized sequential stereo (docs/VR_STEREO.md): ETERNALVR_MODE=stereo with per-eye temporal
history (the layer's default) and its cvars on the command line before -ExtraArgs: +r_TAASafeMode 0
+r_antialiasing 1 +rs_enable 0 +r_swapInterval 0 and 0 for the temporal effects whose history is still shared.
Each eye renders at the game window's size. With the virtual display the game also starts at the
ETERNALVR_WINDOW size (+r_windowWidth/+r_windowHeight before +map; a size in -ExtraArgs wins, for
example the render size), as with the launcher: the device context and eye R's TAA images are built at the
first window size, and every later resize rebuilds them.

.PARAMETER StereoV1
With -Stereo: Route S v1, no temporal accumulation (ETERNALVR_STEREO_TAA=0 and +r_TAASafeMode 1
+r_antialiasing 0 +r_jitter 0 +rs_enable 0 +r_swapInterval 0).

.PARAMETER StereoSameView
With -Stereo: both eyes render the game's own view (ETERNALVR_STEREO_SAME_VIEW=1, experiment S1).

.PARAMETER CaptureEyes
With -Stereo: '<folder>[,<every N pairs>]' writes every Nth eye pair as two PNG files
(ETERNALVR_CAPTURE_EYES); tools/stereo/eye_diff.py compares them.

.PARAMETER NoStereoCvars
With -Stereo: leave the v1 cvar set off the command line.

.PARAMETER StereoTaa
Same as -Stereo (per-eye temporal history is the default).

.PARAMETER StereoDlss
With -Stereo: DLSS per eye instead of TAA (ETERNALVR_STEREO_DLSS=1).

.PARAMETER StereoExperiment
left-eye | two-views: a live stereo experiment of docs/rig-findings/stereo-reentry.md (ETERNALVR_MODE=stereo
with ETERNALVR_STEREO_EXPERIMENT). two-views makes the automatic window up to 3840 wide and crashes build
25216728 (docs/VR_STEREO.md). Not for play.

.PARAMETER KeepFocus
Leave the game in the foreground (run.ps1 -KeepFocus). Not needed for the layer's cinematic skip; needed
with -ExternalSkip, which holds R through keys.ps1 instead.

.PARAMETER AcknowledgeSettingsChange
Passed to run.ps1: accept settings that differ from the last verified restore, only once the change is
known to be ours (for example a launcher session outside the rig).
#>
param(
    [Parameter(Mandatory)][string]$Layer,
    [Parameter(Mandatory)][string]$Label,
    [int]$DisplayWidth = 0,
    [int]$DisplayHeight = 0,
    [string]$Window = 'auto',
    [string]$WindowSize = '',
    [string]$XrRuntimeJson,
    [string]$Map = 'game/sp/e1m1_intro/e1m1_intro',
    [string[]]$ExtraArgs = @(),
    [string[]]$ExtraEnv = @(),
    [int]$WatchSeconds = 90,
    [switch]$Stereo,
    [switch]$StereoSameView,
    [string]$CaptureEyes = '',
    [switch]$NoStereoCvars,
    [switch]$StereoTaa,
    [switch]$StereoV1,
    [switch]$StereoDlss,
    [ValidateSet('', 'left-eye', 'two-views')][string]$StereoExperiment = '',
    [switch]$KeepFocus,
    [switch]$ExternalSkip,
    [switch]$AcknowledgeSettingsChange
)
$ErrorActionPreference = 'Stop'
$rig = $PSScriptRoot
if (-not (Test-Path (Join-Path $Layer 'EternalVR.dll'))) { throw "no EternalVR.dll in $Layer" }
$Layer = (Resolve-Path $Layer).Path
$logs = "$Layer-logs"
New-Item -ItemType Directory -Force $logs | Out-Null
$timeline = Join-Path $logs "timeline-$Label.txt"
Set-Content -Path $timeline -Value "$((Get-Date).ToString('HH:mm:ss.fff')) launch helper start" -Encoding ascii
$savedGames = if ($env:EVR_RIG_SAVED_GAMES) { $env:EVR_RIG_SAVED_GAMES } else { Join-Path $env:USERPROFILE 'Saved Games\id Software\DOOMEternal' }

$launched = Get-Date
$watch = Start-Job -ArgumentList $rig, $WatchSeconds, $timeline, $logs, $ExternalSkip.IsPresent, $savedGames, $launched -ScriptBlock {
    param($rig, $seconds, $timeline, $logs, $external, $savedGames, $launched)
    Add-Type -AssemblyName System.Drawing
    Add-Type -Name W -Namespace LaunchHt -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
[DllImport("user32.dll")] public static extern System.IntPtr GetForegroundWindow();
[DllImport("user32.dll")] public static extern bool GetWindowRect(System.IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool PrintWindow(System.IntPtr h, System.IntPtr hdc, uint flags);
[StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
'@
    [void][LaunchHt.W]::SetProcessDPIAware()
    function Note($text) { Add-Content -Path $timeline -Value "$((Get-Date).ToString('HH:mm:ss.fff')) $text" -Encoding ascii }
    $qlog = Join-Path $savedGames 'base\qconsole.log'
    $script:qseen = 0
    function PollQconsole {
        if (-not (Test-Path $qlog)) { return }
        try { $lines = [IO.File]::ReadAllLines($qlog) } catch { return }
        if ($lines.Count -lt $script:qseen) { $script:qseen = 0 }
        for ($i = $script:qseen; $i -lt $lines.Count; ++$i) {
            if ($lines[$i] -match 'map|level|spawn|load') { Note "qconsole: $($lines[$i])" }
        }
        $script:qseen = $lines.Count
    }
    $deadline = (Get-Date).AddSeconds(90)
    $proc = $null
    while ((Get-Date) -lt $deadline -and -not $proc) {
        $proc = Get-Process DOOMEternalx64vk -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 -and $_.StartTime -gt $launched } | Select-Object -First 1
        if (-not $proc) { Start-Sleep -Milliseconds 250 }
    }
    if (-not $proc) { Note 'no game window within 90 s'; return }
    Note "process start $($proc.StartTime.ToString('HH:mm:ss.fff')) (pid $($proc.Id)); game window 0x$('{0:X}' -f $proc.MainWindowHandle.ToInt64())"
    # qconsole.log is rewritten by this run; skip what an earlier run left.
    if (Test-Path $qlog) { try { $script:qseen = [IO.File]::ReadAllLines($qlog).Count } catch {} }
    $end = (Get-Date).AddSeconds($seconds)
    $hudHits = 0
    $shot = 0
    while ((Get-Date) -lt $end) {
        PollQconsole
        if ($proc.HasExited) { Note 'the game exited'; break }
        $hwnd = $proc.MainWindowHandle
        $fg = [LaunchHt.W]::GetForegroundWindow()
        $r = New-Object LaunchHt.W+RECT
        [void][LaunchHt.W]::GetWindowRect($hwnd, [ref]$r)
        $w = $r.Right - $r.Left; $h = $r.Bottom - $r.Top
        $hud = 0
        if ($w -gt 100 -and $h -gt 100) {
            $bmp = New-Object Drawing.Bitmap $w, $h
            $g = [Drawing.Graphics]::FromImage($bmp)
            try {
                # PrintWindow with PW_RENDERFULLCONTENT: the window's own image, even when other windows cover it.
                $hdc = $g.GetHdc()
                try { [void][LaunchHt.W]::PrintWindow($hwnd, $hdc, 2) } finally { $g.ReleaseHdc($hdc) }
                for ($x = [int]($w * 0.03); $x -lt [int]($w * 0.30); $x += 3) {
                    for ($y = [int]($h * 0.75); $y -lt [int]($h * 0.98); $y += 3) {
                        $c = $bmp.GetPixel($x, $y)
                        if ($c.B -gt 180 -and $c.G -gt 140 -and $c.R -lt 140) { ++$hud }
                    }
                }
                ++$shot
                $bmp.Save((Join-Path $logs ('shot-{0:D3}.png' -f $shot)), [Drawing.Imaging.ImageFormat]::Png)
            } catch {
                Note "capture failed: $($_.Exception.Message)"
            } finally {
                $g.Dispose(); $bmp.Dispose()
            }
        }
        if ($hud -gt 60) { ++$hudHits } else { $hudHits = 0 }
        Note ("capture {0:D3}: HUD pixels {1}, foreground is the game: {2}" -f $shot, $hud, ($fg -eq $hwnd))
        if ($hudHits -ge 2) { Note 'gameplay (HUD seen in two consecutive captures)'; break }
        if ($external) {
            & "$rig\keys.ps1" -Hold R -Ms 3000 -KeepFocus 2>&1 | Out-Null
            Note "R held 3 s from outside (foreground before the hold was the game: $($fg -eq $hwnd))"
        } else {
            Start-Sleep -Milliseconds 1200
        }
    }
    PollQconsole
}

if ($StereoSameView -or $CaptureEyes -or $StereoTaa -or $StereoV1 -or $StereoDlss) { $Stereo = [switch]$true }
if ($Stereo -and $StereoExperiment) { throw '-Stereo (Route S) and -StereoExperiment are separate modes' }
$stereoCvars = @()
if ($Stereo -and -not $NoStereoCvars) {
    $stereoCvars = if (-not $StereoV1) {
        @('+r_TAASafeMode 0', '+r_antialiasing 1', '+rs_enable 0', '+r_swapInterval 0', '+r_TAAAntiGhosting 0',
          '+r_SSDOTemporalAA 0', '+r_lightScatteringTAA 0', '+r_dofTAA 0', '+r_waterReflectionsTAA 0',
          '+r_waterGridTAA 0', '+r_refractionTAA 0', '+r_raytracedReflectionsTemporalUpscaleQuality 0') }
                   else { @('+r_TAASafeMode 1', '+r_antialiasing 0', '+r_jitter 0', '+rs_enable 0', '+r_swapInterval 0') }
}
$gameArgs = @('+logFile 1', '+com_skipKeyPressOnLoadScreens 1', '+com_skipIntroVideo 1', '+com_skipSignInManager 1',
    '+r_hdrDisplay 0', '+r_motionblur 0', '+r_dof 0', '+r_chromaticAberration 0', '+r_vignette 0') + $stereoCvars + $ExtraArgs
if ($Map) { $gameArgs += "+map $Map" } # -Map '' starts at the title screen
$gameEnv = @("VK_ADD_IMPLICIT_LAYER_PATH=$Layer", 'ETERNALVR_ENABLE_LAYER=1', "ETERNALVR_LOG_DIR=$logs")
if (-not $ExternalSkip) { $gameEnv += 'ETERNALVR_SKIP_CINEMATICS=1' }
if ($StereoExperiment) { $gameEnv += 'ETERNALVR_MODE=stereo'; $gameEnv += "ETERNALVR_STEREO_EXPERIMENT=$StereoExperiment" }
if ($Stereo) { $gameEnv += 'ETERNALVR_MODE=stereo' }
if ($StereoSameView) { $gameEnv += 'ETERNALVR_STEREO_SAME_VIEW=1' }
if ($StereoV1) { $gameEnv += 'ETERNALVR_STEREO_TAA=0' }
if ($StereoDlss) { $gameEnv += 'ETERNALVR_STEREO_DLSS=1' }
if ($CaptureEyes) { $gameEnv += "ETERNALVR_CAPTURE_EYES=$CaptureEyes" }
if ($XrRuntimeJson) {
    if (-not (Test-Path $XrRuntimeJson)) { throw "no runtime manifest at $XrRuntimeJson" }
    $gameEnv += "XR_RUNTIME_JSON=$((Resolve-Path $XrRuntimeJson).Path)"
}
if ($DisplayWidth -gt 0) {
    # The virtual display's place on the desktop depends on the other monitors (and can change when one is
    # switched off), so the window position is read from the live layout.
    & "$rig\display.ps1" add -Width $DisplayWidth -Height $DisplayHeight | Out-Null
    $state = & powershell -NoProfile -ExecutionPolicy Bypass -File "$rig\display.ps1" status -Json | Out-String | ConvertFrom-Json
    foreach ($t in @($state.topology)) {
        Add-Content -Path $timeline -Encoding ascii -Value ("{0} display: {1} {2},{3} {4}x{5}{6}{7}" -f (Get-Date).ToString('HH:mm:ss.fff'),
            $t.name, $t.x, $t.y, $t.width, $t.height, $(if ($t.primary) { ' primary' } else { '' }), $(if ($t.vdd) { ' [VDD]' } else { '' }))
    }
    if (-not $state.active -or -not $state.rect) { throw 'the virtual display is not active; use -DisplayWidth 0 for a run without it' }
    if ($Window -eq 'auto') {
        $w = [Math]::Min($(if ($StereoExperiment -eq 'two-views') { 3840 } else { 2560 }), [int]$state.rect.width)
        $h = [Math]::Min(2100, [int]$state.rect.height - 60)
        if ($WindowSize -match '^(\d+)x(\d+)$') {
            $w = [Math]::Min([int]$Matches[1], [int]$state.rect.width)
            $h = [Math]::Min([int]$Matches[2], [int]$state.rect.height - 60)
        } elseif ($WindowSize) { throw "-WindowSize is WIDTHxHEIGHT, not '$WindowSize'" }
        $Window = '{0},{1},{2},{3}' -f $state.rect.x, $state.rect.y, $w, $h
    }
    $gameEnv += "ETERNALVR_WINDOW=$Window"
    Add-Content -Path $timeline -Encoding ascii -Value "$((Get-Date).ToString('HH:mm:ss.fff')) ETERNALVR_WINDOW=$Window"
    $explicitSize = @($ExtraArgs | Where-Object { $_ -match '^\+(set |seta )?r_window(Width|Height)\b' }).Count -gt 0
    if ($Stereo -and -not $explicitSize -and $Window -match '^\s*-?\d+\s*,\s*-?\d+\s*,\s*(\d+)\s*,\s*(\d+)\s*$') {
        # The game starts at this size, as with the launcher: the device context and eye R's TAA images are
        # built at the first window size, and a later resize to ETERNALVR_WINDOW rebuilds them.
        $size = @("+r_windowWidth $($Matches[1])", "+r_windowHeight $($Matches[2])")
        $gameArgs = @($gameArgs | Where-Object { $_ -notlike '+map *' }) + $size + @($gameArgs | Where-Object { $_ -like '+map *' })
    }
}
$gameEnv += $ExtraEnv

$runArgs = @{ Exe = 'retail'; Label = $Label; Args = $gameArgs; GameEnv = $gameEnv }
if ($KeepFocus -or $ExternalSkip) { $runArgs.KeepFocus = $true }
if ($AcknowledgeSettingsChange) { $runArgs.AcknowledgeSettingsChange = $true }
if ($DisplayWidth -gt 0) { $runArgs.DisplayWidth = $DisplayWidth; $runArgs.DisplayHeight = $DisplayHeight } else { $runArgs.NoDisplay = $true }
& "$rig\run.ps1" @runArgs
$code = $LASTEXITCODE
if ($code -ne 0) {
    Stop-Job $watch; Remove-Job $watch -Force
    exit $code
}
Wait-Job $watch | Out-Null
Remove-Job $watch -Force

# Layer milestones, converted to wall-clock time from the log's anchor line.
$layerLog = Get-ChildItem "$logs\eternalvr-2*.log" -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1
if ($layerLog) {
    $lines = Get-Content $layerLog.FullName
    $anchorLine = $lines | Where-Object { $_ -match 'log start (\S+ \S+) local' } | Select-Object -First 1
    if ($anchorLine -match 'log start (\S+ \S+) local') {
        $anchor = [datetime]::ParseExact($Matches[1], 'yyyy-MM-dd HH:mm:ss.fff', $null)
        foreach ($pattern in 'first game present', 'first copy into the ring', 'first game frame on the screen',
                 'first head-tracked game view', 'first head-tracked frame', 'keys: key injection', 'cutscene starts',
                 'holding the skip key (hold 1)', 'the game read injected key', 'cutscene ends', 'aim: head aim on',
                 'aim: view angles = command', 'rebuilding', 'ring of 3 imported', 'seq: Route S on',
                 'seq: stereo tick for game frame', 'seq: render thread idle', 'eye tags out of sync',
                 'stereo off') {
            foreach ($l in @($lines | Where-Object { $_ -match [regex]::Escape($pattern) } | Select-Object -First 2)) {
                if ($l -match '^\[\s*([0-9.]+)\]') {
                    Add-Content -Path $timeline -Encoding ascii -Value "$($anchor.AddSeconds([double]$Matches[1]).ToString('HH:mm:ss.fff')) layer: $($l.Substring($l.IndexOf(']', $l.IndexOf(']') + 1) + 2))"
                }
            }
        }
    }
}
$runsRoot = if ($env:EVR_RIG_RUNS_ROOT) { $env:EVR_RIG_RUNS_ROOT } else { . (Join-Path $PSScriptRoot 'workspace.ps1'); Join-Path (Get-EvrWorkspace) 'runs' }
$run = Get-ChildItem $runsRoot -Directory | Where-Object Name -like "*-$Label" | Sort-Object Name | Select-Object -Last 1
if ($run) {
    Get-Content $timeline | Sort-Object | Set-Content (Join-Path $run.FullName 'timeline.txt') -Encoding ascii
    Write-Output "run: $($run.FullName)"
}
& "$rig\collect.ps1" 2>&1 | Select-String 'screenshot'
