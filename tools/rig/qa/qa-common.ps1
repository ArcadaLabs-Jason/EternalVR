# Shared helpers of the rig QA suite (qa-suite.ps1), dot-sourced: the rig paths, the STOP-RIG check,
# staging a layer build, the launch through launch-ht.ps1, bounded waits on the layer log, screenshots of the
# virtual display and the simulator window, the stop through stop.ps1 and the save restore after every run.

$script:QaRig = Split-Path -Parent $PSScriptRoot
$script:QaRepo = Split-Path -Parent (Split-Path -Parent $script:QaRig)
# The rig's workspace: the first folder above the repo (or worktree) that holds tmp-vr (EVR_WORKSPACE wins).
$script:QaWorkspace = $env:EVR_WORKSPACE
if (-not $script:QaWorkspace) {
    $d = $script:QaRepo
    while ($d -and -not (Test-Path (Join-Path $d 'tmp-vr'))) { $d = Split-Path -Parent $d }
    $script:QaWorkspace = $d
}
$script:QaStopFile = Join-Path $script:QaWorkspace 'tmp-vr\STOP-RIG'
$script:QaStageRoot = Join-Path $script:QaWorkspace 'tmp-vr\rs'
$script:QaSimJson = Join-Path $script:QaWorkspace 'tools\bin\openxr-simulator\openxr_simulator_rig.json'
$script:QaProcDump = Join-Path $script:QaWorkspace 'tools\bin\procdump\procdump64.exe'
$script:QaRunsRoot = if ($env:EVR_RIG_RUNS_ROOT) { $env:EVR_RIG_RUNS_ROOT } else { Join-Path $script:QaWorkspace 'runs' }
$script:QaSaveSource = Join-Path $script:QaWorkspace 'tmp-release\save100\resigned-jason'
# The game's first save slot the 100% save goes back to: EVR_QA_SAVE_TARGET, else the path in the rig's own
# tmp-vr\qa-save-target.txt (the save is signed for one Steam account), else the account that played last.
$script:QaSaveTarget = $env:EVR_QA_SAVE_TARGET
$targetFile = Join-Path $script:QaWorkspace 'tmp-vr\qa-save-target.txt'
if (-not $script:QaSaveTarget -and (Test-Path $targetFile)) { $script:QaSaveTarget = (Get-Content $targetFile -First 1).Trim() }
if (-not $script:QaSaveTarget) {
    $remote = Get-ChildItem 'C:\Program Files (x86)\Steam\userdata\*\782330\remote' -Directory -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($remote) { $script:QaSaveTarget = Join-Path $remote.FullName 'GAME-AUTOSAVE0' }
}
$script:QaSaveDurationBytes = 958137
$script:QaMap = 'game/sp/e1m2_battle/e1m2_battle'
# In the map: the schedule's line needs ETERNALVR_DEBUG_COMMANDS; head aim comes on in every run.
$script:QaPlayMarker = "the schedule's clock starts|aim: head aim on"
$script:QaGameProcess = 'DOOMEternalx64vk'
$script:QaExtraEnv = @() # NAME=VALUE pairs for every game scenario (qa-suite.ps1 -DlssDll)

if (-not ('QaNative' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class QaNative {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    public delegate bool EnumProc(IntPtr h, IntPtr p);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr p);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
    public static IntPtr FindByTitlePrefix(string prefix) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((h, p) => {
            if (!IsWindowVisible(h)) return true;
            var sb = new StringBuilder(512);
            GetWindowTextW(h, sb, 512);
            if (sb.ToString().StartsWith(prefix, StringComparison.OrdinalIgnoreCase)) { found = h; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
'@
}
Add-Type -AssemblyName System.Drawing
[void][QaNative]::SetProcessDPIAware()

function Write-QaLine([string]$Text) {
    $line = "$((Get-Date).ToString('HH:mm:ss')) $Text"
    Write-Host $line
    if ($script:QaLogFile) { Add-Content -LiteralPath $script:QaLogFile -Value $line -Encoding utf8 }
}

function Test-QaStopRequested { return (Test-Path -LiteralPath $script:QaStopFile) }

function Test-QaGameRunning { return [bool](Get-Process $script:QaGameProcess -ErrorAction SilentlyContinue) }

# Before every scenario: the owner's STOP-RIG file ends the suite, a game already running refuses it.
function Assert-QaCanStart {
    if (Test-QaStopRequested) { throw [OperationCanceledException]::new("STOP-RIG: $script:QaStopFile exists") }
    if (Test-QaGameRunning) { throw "$script:QaGameProcess.exe is already running; the suite starts only on an idle rig" }
}

# A new scenario context: what the scenario's timeline and assertions read and write.
function New-QaContext($Scenario, [string]$Out) {
    $dir = Join-Path $Out $Scenario.Name
    New-Item -ItemType Directory -Force $dir | Out-Null
    return @{
        Name = $Scenario.Name; Label = "qa-$($Scenario.Name)"; Out = $Out; Dir = $dir; Since = Get-Date
        Stage = $null; LogsDir = $null; RunDir = $null; InputPath = $null; Vdd = $null; DumpWatch = $null
        Shots = New-Object System.Collections.ArrayList; Results = New-Object System.Collections.ArrayList
        Notes = New-Object System.Collections.ArrayList; Stopped = $false; Abort = $false; Skipped = $false
    }
}

function Add-QaResult($Ctx, [string]$Name, [bool]$Ok, [string]$Detail = '') {
    [void]$Ctx.Results.Add([pscustomobject]@{ Name = $Name; Ok = $Ok; Detail = $Detail })
    Write-QaLine ("  {0} {1}{2}" -f $(if ($Ok) { 'ok  ' } else { 'FAIL' }), $Name, $(if ($Detail) { " ($Detail)" } else { '' }))
}

function Add-QaNote($Ctx, [string]$Text) { [void]$Ctx.Notes.Add($Text); Write-QaLine "  note: $Text" }

# Copies the layer build into a fresh stage folder (the game locks the DLL), as rsrun.ps1 does; the layer
# log goes to <stage>-logs, a scenario's eye captures to <stage>-eyes.
function New-QaStage($Ctx, [string]$LayerSrc) {
    $stage = Join-Path $script:QaStageRoot $Ctx.Label
    foreach ($d in @($stage, "$stage-logs", "$stage-dumps", "$stage-eyes")) {
        if (Test-Path -LiteralPath $d) { Remove-Item -LiteralPath $d -Recurse -Force }
    }
    New-Item -ItemType Directory -Force $stage, "$stage-dumps" | Out-Null
    foreach ($f in 'EternalVR.dll', 'VK_LAYER_ETERNALVR.json', 'openxr_loader.dll', 'EternalVR.pdb') {
        $src = Join-Path $LayerSrc $f
        if (Test-Path -LiteralPath $src) { Copy-Item -LiteralPath $src $stage }
    }
    $Ctx.Stage = $stage
    $Ctx.LogsDir = "$stage-logs"
    $Ctx.InputPath = "$stage-input.txt"
}

# Rewrites the scripted input file in one step (the layer re-reads it when its write time changes).
function Write-QaInput($Ctx, [string[]]$Lines) {
    $tmp = "$($Ctx.InputPath).tmp"
    [IO.File]::WriteAllText($tmp, (@($Lines) -join "`n") + "`n")
    Move-Item -LiteralPath $tmp -Destination $Ctx.InputPath -Force
}

# procdump waits for the game and writes a dump if it dies of an unhandled exception.
function Start-QaDumpWatch($Ctx) {
    if (-not (Test-Path -LiteralPath $script:QaProcDump)) { return }
    $Ctx.DumpWatch = Start-Process -FilePath $script:QaProcDump -PassThru -WindowStyle Hidden `
        -ArgumentList @('-accepteula', '-e', '-ma', '-w', "$script:QaGameProcess.exe", "$($Ctx.Stage)-dumps")
}

function Stop-QaDumpWatch($Ctx) {
    if ($Ctx.DumpWatch -and -not $Ctx.DumpWatch.HasExited) { Stop-Process -Id $Ctx.DumpWatch.Id -Force -ErrorAction SilentlyContinue }
}

# The virtual display's rect from display.ps1 status -Json (read-only), or $null.
function Get-QaVddRect {
    try {
        $state = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $script:QaRig 'display.ps1') status -Json |
            Out-String | ConvertFrom-Json
        if ($state.active -and $state.rect) { return $state.rect }
    } catch {}
    return $null
}

function Find-QaRunDir($Ctx) {
    if (-not (Test-Path -LiteralPath $script:QaRunsRoot)) { return $null }
    $dir = Get-ChildItem -LiteralPath $script:QaRunsRoot -Directory |
        Where-Object { $_.Name -like "*-$($Ctx.Label)" -and $_.CreationTime -ge $Ctx.Since.AddSeconds(-5) } |
        Sort-Object Name | Select-Object -Last 1
    if ($dir) { return $dir.FullName } else { return $null }
}

# Starts the game through launch-ht.ps1: Route S stereo on the simulator, the game window and the mirror on the
# virtual display (as armbisect.ps1 and rsrun.ps1 do). Returns once launch-ht.ps1 does (the game runs on).
function Start-QaGame($Ctx, $Scenario) {
    $gameEnv = @($Scenario.Env) + @($script:QaExtraEnv)
    if ($null -ne $Scenario.Input) { $gameEnv += "ETERNALVR_TEST_INPUT=$($Ctx.InputPath)" }
    if ($Scenario.EyeCaptures) { $gameEnv += "ETERNALVR_CAPTURE_EYES=$($Ctx.Stage)-eyes,$($Scenario.EyeCaptures)" }
    $Ctx.Vdd = Get-QaVddRect
    if ($Ctx.Vdd) { $gameEnv += "ETERNALVR_MIRROR_WINDOW=$($Ctx.Vdd.x),$($Ctx.Vdd.y),1280,720" }
    $map = if ($Scenario.Map) { $Scenario.Map } else { $script:QaMap }
    $gameArgs = if ($Scenario.Args) { @($Scenario.Args) } else { @() }
    $p = @{ Layer = $Ctx.Stage; Label = $Ctx.Label; DisplayWidth = 2560; DisplayHeight = 1440; XrRuntimeJson = $script:QaSimJson
            ExtraEnv = $gameEnv; ExtraArgs = $gameArgs; WatchSeconds = 20; WindowSize = '1280x720'; Map = $map; Stereo = $true
            AcknowledgeSettingsChange = $true }
    Write-QaLine "  launch: $map; $($gameEnv -join '; ')$(if ($gameArgs.Count) { '; ' + ($gameArgs -join ' ') })"
    $out = & (Join-Path $script:QaRig 'launch-ht.ps1') @p 2>&1 | ForEach-Object { "$_" }
    $code = $LASTEXITCODE
    Set-Content -LiteralPath (Join-Path $Ctx.Dir 'launch.txt') -Value $out -Encoding utf8
    $runLine = @($out | Where-Object { $_ -match '^run: (.+)$' }) | Select-Object -Last 1
    $Ctx.RunDir = if ($runLine -match '^run: (.+)$') { $Matches[1].Trim() } else { Find-QaRunDir $Ctx }
    if (-not $Ctx.Vdd) { $Ctx.Vdd = Get-QaVddRect }
    if (-not (Test-QaGameRunning)) { throw "the game is not running after launch-ht.ps1 (exit $code; see launch.txt)" }
}

# Reads a file the game may still be writing.
function Read-QaShared([string]$Path) {
    try {
        $fs = [IO.File]::Open($Path, 'Open', 'Read', 'ReadWrite, Delete')
        try { return (New-Object IO.StreamReader($fs)).ReadToEnd() } finally { $fs.Dispose() }
    } catch { return '' }
}

# The newest layer log of this scenario: <stage>-logs first, then the run folder's logs\ and the run folder.
function Get-QaLayerLog($Ctx) {
    $dirs = @($Ctx.LogsDir)
    if (-not $Ctx.RunDir) { $Ctx.RunDir = Find-QaRunDir $Ctx }
    if ($Ctx.RunDir) { $dirs += (Join-Path $Ctx.RunDir 'logs'); $dirs += $Ctx.RunDir }
    foreach ($d in $dirs) {
        if (-not $d -or -not (Test-Path -LiteralPath $d)) { continue }
        $f = Get-ChildItem -LiteralPath $d -Filter 'eternalvr-2*.log' -File -ErrorAction SilentlyContinue |
            Where-Object { $_.LastWriteTime -ge $Ctx.Since } | Sort-Object LastWriteTime | Select-Object -Last 1
        if ($f) { return $f.FullName }
    }
    return $null
}

function Get-QaLogText($Ctx) {
    $log = Get-QaLayerLog $Ctx
    if ($log) { return (Read-QaShared $log) } else { return '' }
}

# A bounded wait in half-second steps: STOP-RIG ends it (and the suite), a game that exits fails the scenario.
function Wait-QaSeconds($Ctx, [double]$Seconds) {
    $end = (Get-Date).AddSeconds($Seconds)
    while ((Get-Date) -lt $end) {
        if (Test-QaStopRequested) { throw [OperationCanceledException]::new('STOP-RIG appeared during the run') }
        if (-not (Test-QaGameRunning)) { throw 'the game exited during the run' }
        $left = ($end - (Get-Date)).TotalMilliseconds
        Start-Sleep -Milliseconds ([int][Math]::Max(1, [Math]::Min(500, $left)))
    }
}

# Waits until the layer log matches the regex; $true if it did within the timeout.
function Wait-QaLog($Ctx, [string]$Pattern, [int]$TimeoutSeconds) {
    $end = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $end) {
        if ((Get-QaLogText $Ctx) -match $Pattern) { return $true }
        Wait-QaSeconds $Ctx 0.5
    }
    return [bool]((Get-QaLogText $Ctx) -match $Pattern)
}

# Screenshots at a named checkpoint: the virtual display (what vddshot.ps1 takes) and the OpenXR Simulator's
# preview window (the headset view, what simshot.ps1 takes), both PNG in <Out>.
function Save-QaShot($Ctx, [string]$Checkpoint) {
    $base = Join-Path $Ctx.Out "$($Ctx.Name)-$Checkpoint"
    $r = $Ctx.Vdd
    $x = 3840; $y = 0; $w = 2560; $h = 1440
    if ($r) { $x = [int]$r.x; $y = [int]$r.y; $w = [int]$r.width; $h = [int]$r.height }
    try {
        $bmp = New-Object Drawing.Bitmap $w, $h
        $g = [Drawing.Graphics]::FromImage($bmp)
        try { $g.CopyFromScreen($x, $y, 0, 0, $bmp.Size); $bmp.Save("$base.png", [Drawing.Imaging.ImageFormat]::Png) }
        finally { $g.Dispose(); $bmp.Dispose() }
        [void]$Ctx.Shots.Add("$base.png")
    } catch { Add-QaNote $Ctx "virtual display shot '$Checkpoint' failed: $($_.Exception.Message)" }
    try {
        $hwnd = [QaNative]::FindByTitlePrefix('OpenXR Simulator')
        if ($hwnd -eq [IntPtr]::Zero) { Add-QaNote $Ctx "no simulator window for '$Checkpoint'"; return }
        $rc = New-Object QaNative+RECT
        [void][QaNative]::GetWindowRect($hwnd, [ref]$rc)
        $sw = $rc.Right - $rc.Left; $sh = $rc.Bottom - $rc.Top
        if ($sw -lt 16 -or $sh -lt 16) { return }
        $bmp = New-Object Drawing.Bitmap $sw, $sh
        $g = [Drawing.Graphics]::FromImage($bmp)
        try {
            $hdc = $g.GetHdc()
            try { [void][QaNative]::PrintWindow($hwnd, $hdc, 2) } finally { $g.ReleaseHdc($hdc) }
            $bmp.Save("$base-sim.png", [Drawing.Imaging.ImageFormat]::Png)
        } finally { $g.Dispose(); $bmp.Dispose() }
        [void]$Ctx.Shots.Add("$base-sim.png")
    } catch { Add-QaNote $Ctx "simulator shot '$Checkpoint' failed: $($_.Exception.Message)" }
    Write-QaLine "  shot: $Checkpoint"
}

# Ends the run through stop.ps1 (the only script that stops a live run) with the Steam-Cloud files restored.
function Stop-QaGame($Ctx) {
    if (-not $Ctx.RunDir) { $Ctx.RunDir = Find-QaRunDir $Ctx }
    $run = if ($Ctx.RunDir) { Split-Path -Leaf $Ctx.RunDir } elseif (Test-QaGameRunning) { 'latest' } else { $null }
    if (-not $run) { return }
    $out = & (Join-Path $script:QaRig 'stop.ps1') -Run $run -RestoreCloudFiles 2>&1 | ForEach-Object { "$_" }
    $code = $LASTEXITCODE
    Set-Content -LiteralPath (Join-Path $Ctx.Dir 'stop.txt') -Value $out -Encoding utf8
    $end = (Get-Date).AddSeconds(30)
    while ((Test-QaGameRunning) -and (Get-Date) -lt $end) { Start-Sleep -Milliseconds 500 }
    $done = [bool]($out | Where-Object { $_ -match 'cleanup done' })
    Add-QaResult $Ctx 'stopped and cleaned up' ((-not (Test-QaGameRunning)) -and $code -eq 0) "stop.ps1 exit $code$(if (-not $done) { ', no cleanup done line' })"
}

# The 100% save back into the autosave slot after every game run (armbisect.ps1), checked by size.
function Restore-QaSave {
    try {
        Copy-Item -Path (Join-Path $script:QaSaveSource '*') -Destination $script:QaSaveTarget -Force -ErrorAction Stop
        Get-ChildItem -LiteralPath $script:QaSaveTarget -File | ForEach-Object { $_.LastWriteTime = Get-Date }
        $len = (Get-Item -LiteralPath (Join-Path $script:QaSaveTarget 'game_duration.dat') -ErrorAction Stop).Length
        return [pscustomobject]@{ Ok = ($len -eq $script:QaSaveDurationBytes); Detail = "game_duration.dat $len bytes" }
    } catch {
        return [pscustomobject]@{ Ok = $false; Detail = $_.Exception.Message }
    }
}

# Log assertions. Plain text by default; -Regex for a regular expression, -CaseSensitive with it.
function Get-QaMatches($Ctx, [string]$Pattern, [switch]$Regex, [switch]$CaseSensitive) {
    $lines = (Get-QaLogText $Ctx) -split "`r?`n"
    if (-not $Regex) { return @($lines | Where-Object { $_.Contains($Pattern) }) }
    if ($CaseSensitive) { return @($lines | Where-Object { $_ -cmatch $Pattern }) }
    return @($lines | Where-Object { $_ -match $Pattern })
}

function Test-QaPresent($Ctx, [string]$Pattern, [switch]$Regex, [switch]$CaseSensitive) {
    $m = Get-QaMatches $Ctx $Pattern -Regex:$Regex -CaseSensitive:$CaseSensitive
    Add-QaResult $Ctx "present: $Pattern" ($m.Count -gt 0) "$($m.Count) line(s)"
}

function Test-QaAbsent($Ctx, [string]$Pattern, [switch]$Regex, [switch]$CaseSensitive) {
    $m = Get-QaMatches $Ctx $Pattern -Regex:$Regex -CaseSensitive:$CaseSensitive
    $first = if ($m.Count -gt 0) { ': ' + ($m[0].Trim() -replace '^(.{0,160}).*$', '$1') } else { '' }
    Add-QaResult $Ctx "absent: $Pattern" ($m.Count -eq 0) "$($m.Count) line(s)$first"
}

function Test-QaCount($Ctx, [string]$Pattern, [int]$Expected) {
    $m = Get-QaMatches $Ctx $Pattern
    Add-QaResult $Ctx "exactly $($Expected)x: $Pattern" ($m.Count -eq $Expected) "$($m.Count) line(s)"
}
