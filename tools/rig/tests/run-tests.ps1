<#
.SYNOPSIS
Automated tests for the rig scripts. They are the specification of the scripts' behaviour.

.DESCRIPTION
No game, no real Steam folder, no real settings, no display changes: every test gets its own folder
under the test root with fake settings trees (Saved Games and Steam userdata\<id>\782330), the test
application (tools/rig/testapp) as the game, a JSON stub as the display controller, and the rig scripts'
test seams (elevation probe, Steam process name, game process names, timeouts, kill points) set through
EVR_RIG_* environment variables. In test mode the scripts refuse any path outside the test root.

Exit code 0 when every test passes, 1 otherwise.

.PARAMETER TestApp
Path to evr_rig_testapp.exe. Default: the newest build under <repo>\build\*\tools\rig\testapp\.

.PARAMETER TestRoot
Folder for the test runs. Default: <workspace>\tmp-rigtests when EVR_WORKSPACE is set, otherwise
%RUNNER_TEMP% on CI, otherwise tmp-rigtests in the workspace that tools\rig\workspace.ps1 derives.

.PARAMETER Only
Run only the named tests.
#>
param(
    [string]$TestApp,
    [string]$TestRoot,
    [string[]]$Only
)

$ErrorActionPreference = 'Stop'
$RigDir = Split-Path -Parent $PSScriptRoot
$RepoRoot = Split-Path -Parent (Split-Path -Parent $RigDir)
. (Join-Path $RigDir 'common.ps1')

if (-not $TestApp) {
    $found = @(Get-ChildItem -Path (Join-Path $RepoRoot 'build') -Recurse -Filter 'evr_rig_testapp.exe' -ErrorAction SilentlyContinue | Sort-Object LastWriteTime)
    if ($found.Count -eq 0) { Write-Host 'evr_rig_testapp.exe not found; build the project first (target evr_rig_testapp) or pass -TestApp'; exit 1 }
    $TestApp = $found[-1].FullName
}
if (-not $TestRoot) {
    if ($env:EVR_WORKSPACE) { $TestRoot = Join-Path $env:EVR_WORKSPACE 'tmp-rigtests' }
    elseif ($env:RUNNER_TEMP) { $TestRoot = Join-Path $env:RUNNER_TEMP 'evr-rigtests' }
    else { $TestRoot = Join-Path (Get-EvrWorkspace) 'tmp-rigtests' }
}
$TestRoot = [IO.Path]::GetFullPath($TestRoot)
if (-not (Test-Path -LiteralPath $TestRoot)) { [void](New-Item -ItemType Directory -Path $TestRoot) }
# Earlier suites are ours to remove (only folders named like a suite stamp).
Get-ChildItem -LiteralPath $TestRoot -Directory | Where-Object { $_.Name -match '^suite-\d{8}-\d{6}$' } |
    Sort-Object Name | Select-Object -SkipLast 2 | ForEach-Object { Remove-Item -LiteralPath $_.FullName -Recurse -Force }
$SuiteRoot = Join-Path $TestRoot ('suite-{0}' -f (Get-Date).ToString('yyyyMMdd-HHmmss'))
[void](New-Item -ItemType Directory -Path $SuiteRoot)
Write-Host "test app:   $TestApp"
Write-Host "suite root: $SuiteRoot"

# ---------------------------------------------------------------------------------------------------
# Harness

$script:Results = @()
$script:Failures = $null
$script:SkipReason = $null
$script:T = $null

function Check([bool]$Condition, [string]$Message) {
    if (-not $Condition) { [void]$script:Failures.Add($Message) }
}

function Skip([string]$Reason) { $script:SkipReason = $Reason }

function Stop-TestApps {
    foreach ($p in @(Get-Process -Name 'evr_rig_testapp' -ErrorAction SilentlyContinue)) {
        $path = $null; try { $path = $p.Path } catch { }
        if ($path -and (Test-RigPathUnder $path $SuiteRoot)) { try { $p.Kill(); [void]$p.WaitForExit(5000) } catch { } }
    }
}

function Test-Case([string]$Name, [scriptblock]$Body) {
    if ($Only -and ($Only -notcontains $Name)) { return }
    $script:Failures = New-Object System.Collections.ArrayList
    $script:SkipReason = $null
    Write-Host ''
    Write-Host "=== $Name"
    $sw = [Diagnostics.Stopwatch]::StartNew()
    try { & $Body } catch {
        [void]$script:Failures.Add("exception: $($_.Exception.Message) $($_.InvocationInfo.PositionMessage)")
    } finally { Stop-TestApps }
    $status = 'PASS'
    if ($script:Failures.Count -gt 0) { $status = 'FAIL' } elseif ($script:SkipReason) { $status = 'SKIP' }
    foreach ($f in $script:Failures) { Write-Host "    FAIL: $f" }
    if ($status -eq 'SKIP') { Write-Host "    SKIP: $($script:SkipReason)" }
    Write-Host ("--- {0} {1} ({2:n1} s)" -f $status, $Name, $sw.Elapsed.TotalSeconds)
    $script:Results += [pscustomobject]@{ Name = $Name; Status = $status; Seconds = $sw.Elapsed.TotalSeconds; Detail = (@($script:Failures) + @($script:SkipReason) | Where-Object { $_ }) -join '; ' }
}

function Write-TestFile([string]$Path, [string]$Text) {
    $dir = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $dir)) { [void][IO.Directory]::CreateDirectory($dir) }
    [IO.File]::WriteAllText($Path, $Text)
}

function Write-TestBytes([string]$Path, [int]$Count, [int]$Seed) {
    $dir = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $dir)) { [void][IO.Directory]::CreateDirectory($dir) }
    $bytes = New-Object byte[] $Count
    (New-Object Random $Seed).NextBytes($bytes)
    [IO.File]::WriteAllBytes($Path, $bytes)
}

# A fresh fake rig: settings trees, fake game root with the test app, display stub, EVR_RIG_* variables.
function New-TestEnv([string]$Name, [hashtable]$Overrides = @{}, [string]$GameDir = 'game', [string]$RunsDir = 'runs') {
    $root = Join-Path $SuiteRoot $Name
    [void][IO.Directory]::CreateDirectory($root)
    $saved = Join-Path $root 'saved\id Software\DOOMEternal'
    $steam = Join-Path $root 'steam'
    $app = Join-Path $steam 'userdata\111\782330'
    $remote = Join-Path $app 'remote'
    Write-TestFile (Join-Path $saved 'base\DOOMEternalConfig.cfg') "seta s_volume `"1`"`r`nseta r_swapInterval `"1`"`r`n"
    Write-TestFile (Join-Path $saved 'base\DOOMEternalConfig.local') "seta r_mode `"3`"`r`n"
    Write-TestFile (Join-Path $saved 'user\config.json') '{"test":true}'
    Write-TestBytes (Join-Path $remote 'PROFILE\profile.bin') 4096 1
    Write-TestBytes (Join-Path $remote 'GAME-AUTOSAVE0\game_duration.dat') 512 2
    Write-TestBytes (Join-Path $remote 'GAME-MANUAL1\game.details') 256 3
    Write-TestFile (Join-Path $app 'remotecache.vdf') '"782330" { }'
    Write-TestFile (Join-Path $steam 'userdata\111\config\localconfig.vdf') '"UserLocalConfigStore" { }'
    $game = Join-Path $root $GameDir
    [void][IO.Directory]::CreateDirectory((Join-Path $game 'base'))
    [void][IO.Directory]::CreateDirectory((Join-Path $root 'lab'))
    $exe = Join-Path $game 'evr_rig_testapp.exe'
    [IO.File]::Copy($TestApp, $exe, $true)
    $stub = Join-Path $root 'display-stub.json'
    [IO.File]::WriteAllText($stub, (ConvertTo-Json -Depth 4 -InputObject ([ordered]@{
                    available = $true; active = $false; failAdd = $false; queryError = $null; rect = [ordered]@{ x = -20000; y = -20000; width = 640; height = 360 }
                    modes = @('640x360', '1920x1080', '2560x1440'); calls = @() })))

    $vars = [ordered]@{
        EVR_RIG_TEST_ROOT = $root; EVR_RIG_RUNS_ROOT = (Join-Path $root $RunsDir); EVR_RIG_GAME_ROOT = $game
        EVR_RIG_LAB_ROOT = (Join-Path $root 'lab'); EVR_RIG_SAVED_GAMES = $saved; EVR_RIG_STEAM_ROOT = $steam
        EVR_RIG_STEAM_PROCESS = 'powershell'; EVR_RIG_DISPLAY_STUB = $stub; EVR_RIG_ELEVATION = 'not-elevated'
        EVR_RIG_CLOSE_TIMEOUT_SEC = '5'; EVR_RIG_KILL_TIMEOUT_SEC = '5'; EVR_RIG_SYNC_QUIET_SEC = '1'; EVR_RIG_SYNC_MAX_SEC = '8'
        EVR_RIG_WATCH_SEC = '2'; EVR_RIG_WINDOW_WAIT_SEC = '6'; EVR_RIG_MUTE_WAIT_SEC = '3'; EVR_RIG_LOCK_WAIT_SEC = '30'
        # Hermetic: no real process name counts as the game unless a test says so.
        EVR_RIG_GAME_PROCESSES = 'evr_rig_test_game_only'
    }
    foreach ($k in $Overrides.Keys) { $vars[$k] = $Overrides[$k] }
    foreach ($item in @(Get-ChildItem Env: | Where-Object { $_.Name -like 'EVR_RIG_*' })) { Remove-Item -LiteralPath "Env:$($item.Name)" }
    foreach ($k in $vars.Keys) { if ($null -ne $vars[$k]) { Set-Item -LiteralPath "Env:$k" -Value $vars[$k] } }

    $t = [pscustomobject]@{
        Root = $root; Runs = (Join-Path $root $RunsDir); Game = $game; Exe = $exe; Saved = $saved; Steam = $steam; Remote = $remote; Stub = $stub
        Cfg = (Join-Path $saved 'base\DOOMEternalConfig.cfg'); Profile = (Join-Path $remote 'PROFILE\profile.bin')
        Local = (Join-Path $saved 'base\DOOMEternalConfig.local'); Json = (Join-Path $saved 'user\config.json')
        Save = (Join-Path $remote 'GAME-AUTOSAVE0\game_duration.dat'); Orig = $null; Calls = 0
    }
    $t.Orig = Get-SettingsState $t
    $script:T = $t
    return $t
}

function Set-TestVar([string]$Name, [string]$Value) {
    if ($null -eq $Value -or $Value -eq '') { Remove-Item -LiteralPath "Env:$Name" -ErrorAction SilentlyContinue } else { Set-Item -LiteralPath "Env:$Name" -Value $Value }
}

function Get-SettingsState($t) {
    $table = New-RigHashTable
    foreach ($root in @($t.Saved, $t.Remote)) {
        if (-not (Test-Path -LiteralPath $root)) { continue }
        foreach ($f in @(Get-ChildItem -LiteralPath $root -Recurse -File -Force)) { $table[$f.FullName] = Get-RigSha256 $f.FullName }
    }
    return $table
}

function Test-SettingsEqual($a, $b) {
    if ($a.Count -ne $b.Count) { return $false }
    foreach ($k in $a.Keys) { if (-not $b.ContainsKey($k) -or $b[$k] -ne $a[$k]) { return $false } }
    return $true
}

function Format-Arg([string]$s) {
    if ($s -eq '' -or $s -match '[\s"]') { return '"' + ($s -replace '"', '\"') + '"' }
    return $s
}

# Runs a rig script in a child powershell.exe (as the rig does), bounded, output kept in the test folder.
function Invoke-Rig([string]$Script, [string[]]$Arguments = @(), [int]$TimeoutSec = 180) {
    $t = $script:T
    $t.Calls++
    $psi = New-Object Diagnostics.ProcessStartInfo 'powershell.exe'
    $parts = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Format-Arg (Join-Path $RigDir $Script))) + @($Arguments | ForEach-Object { Format-Arg $_ })
    $psi.Arguments = $parts -join ' '
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $p = [Diagnostics.Process]::Start($psi)
    $out = $p.StandardOutput.ReadToEndAsync()
    $err = $p.StandardError.ReadToEndAsync()
    if (-not $p.WaitForExit($TimeoutSec * 1000)) {
        try { $p.Kill() } catch { }
        Check $false "$Script did not finish within $TimeoutSec s"
    }
    $p.WaitForExit()
    $text = ''
    if ($out.Wait(10000)) { $text += $out.Result } else { Check $false "$Script output pipe stayed open (a child inherited it)" }
    if ($err.Wait(10000)) { $text += $err.Result }
    $sw.Stop()
    $log = Join-Path $t.Root ('harness-{0:d2}-{1}.log' -f $t.Calls, [IO.Path]::GetFileNameWithoutExtension($Script))
    [IO.File]::WriteAllText($log, ("> {0} {1}`r`nexit {2} after {3:n1} s`r`n{4}" -f $Script, ($Arguments -join ' '), $p.ExitCode, $sw.Elapsed.TotalSeconds, $text))
    return [pscustomobject]@{ Code = $p.ExitCode; Output = $text; Seconds = $sw.Elapsed.TotalSeconds }
}

function Get-Runs($t) {
    if (-not (Test-Path -LiteralPath $t.Runs)) { return @() }
    return @(Get-ChildItem -LiteralPath $t.Runs -Directory | Where-Object { $_.Name -match '^\d{8}-\d{6}' } | Sort-Object Name | ForEach-Object { $_.FullName })
}

function Get-Stub($t) { return ([IO.File]::ReadAllText($t.Stub) | ConvertFrom-Json) }

function Set-Stub($t, [hashtable]$Values) {
    $s = Get-Stub $t
    foreach ($k in $Values.Keys) { $s | Add-Member -NotePropertyName $k -NotePropertyValue $Values[$k] -Force }
    [IO.File]::WriteAllText($t.Stub, (ConvertTo-Json -InputObject $s -Depth 4))
}

function Read-Json([string]$Path) { if (Test-Path -LiteralPath $Path) { return ([IO.File]::ReadAllText($Path) | ConvertFrom-Json) } return $null }

function Get-AliveTestApps {
    return @(Get-Process -Name 'evr_rig_testapp' -ErrorAction SilentlyContinue | Where-Object {
            $path = $null; try { $path = $_.Path } catch { }; $path -and (Test-RigPathUnder $path $SuiteRoot) })
}

function Start-TestRun($t, [string[]]$Extra = @()) {
    $r = Invoke-Rig 'run.ps1' (@('-Exe', $t.Exe) + $Extra)
    $runs = @(Get-Runs $t)
    $dir = $null
    if ($runs.Count -gt 0) { $dir = $runs[-1] }
    return [pscustomobject]@{ Code = $r.Code; Output = $r.Output; Dir = $dir; Run = $(if ($dir) { Read-Json (Join-Path $dir 'run.json') } else { $null }) }
}

function Simulate-GameWrites($t) {
    # What a game session might do to the local settings (whole-file restore): change a config file,
    # remove one, add a file. The Steam-Cloud files have their own tests (T-115).
    Write-TestFile $t.Cfg "seta s_volume `"0`"`r`nseta r_swapInterval `"0`"`r`n"
    Remove-Item -LiteralPath $t.Json -Force
    Write-TestFile (Join-Path $t.Saved 'base\added-during-run.txt') 'new'
}

# Steam's record of the fake remote folder (remotecache.vdf, in Steam's format): size and SHA-1 per
# file, as Steam writes it at the end of an app session. $Stale: relative path (with /) -> SHA-1 to
# write instead of the file's own.
function Format-TestCloudRecord([string]$Remote, [hashtable]$Stale = @{}) {
    $root = (Get-Item -LiteralPath $Remote).FullName.TrimEnd('\')
    $text = "`"782330`"`n{`n`t`"ChangeNumber`"`t`t`"7`"`n`t`"OSType`"`t`t`"0`"`n"
    foreach ($f in @(Get-ChildItem -LiteralPath $root -Recurse -File | Sort-Object FullName)) {
        $rel = $f.FullName.Substring($root.Length + 1) -replace '\\', '/'
        $sha = (Get-FileHash -LiteralPath $f.FullName -Algorithm SHA1).Hash.ToLowerInvariant()
        if ($Stale.ContainsKey($rel)) { $sha = $Stale[$rel] }
        $text += "`t`"$rel`"`n`t{`n`t`t`"root`"`t`t`"0`"`n`t`t`"size`"`t`t`"$($f.Length)`"`n`t`t`"sha`"`t`t`"$sha`"`n`t`t`"syncstate`"`t`t`"1`"`n`t`t`"persiststate`"`t`t`"0`"`n`t}`n"
    }
    return ($text + "}`n")
}

function Write-TestCloudRecord($t, [hashtable]$Stale = @{}) {
    [IO.File]::WriteAllText((Join-Path (Split-Path -Parent $t.Remote) 'remotecache.vdf'), (Format-TestCloudRecord $t.Remote $Stale))
}

# Stand-in for the resync launch: a hidden PowerShell that, like Steam at the end of the short app
# session, records the restored cloud files in remotecache.vdf ($Accept), touches a local config file
# as a game start may, then waits to be killed.
function Set-TestResync($t, [switch]$Accept, [string]$Name = 'resync-game.ps1') {
    $vdf = Join-Path (Split-Path -Parent $t.Remote) 'remotecache.vdf'
    $body = "function Format-TestCloudRecord {`n$(${function:Format-TestCloudRecord})`n}`n"
    if ($Accept) { $body += "[IO.File]::WriteAllText('$vdf', (Format-TestCloudRecord '$($t.Remote)'))`n" }
    $body += "[IO.File]::AppendAllText('$($t.Local)', 'seta r_windowWidth `"640`"')`nStart-Sleep -Seconds 60`n"
    $script = Join-Path $t.Root $Name
    [IO.File]::WriteAllText($script, $body)
    Set-TestVar 'EVR_RIG_RESYNC_EXE' (Join-Path $PSHOME 'powershell.exe')
    Set-TestVar 'EVR_RIG_RESYNC_ARGS' "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$script`""
    Set-TestVar 'EVR_RIG_RESYNC_KILL_MS' '4000'
    return $script
}

function Get-TestResyncProcesses([string]$Script) {
    return @(Get-CimInstance Win32_Process -Filter "Name = 'powershell.exe'" | Where-Object { $_.CommandLine -and $_.CommandLine.Contains($Script) })
}

# ---------------------------------------------------------------------------------------------------
# Tests

Test-Case 'refuses-when-elevated' {
    $t = New-TestEnv 'refuses-when-elevated' @{ EVR_RIG_ELEVATION = 'elevated' }
    $r = Invoke-Rig 'run.ps1' @('-Exe', $t.Exe)
    Check ($r.Code -eq 2) "exit code $($r.Code), expected 2"
    Check ($r.Output -match 'REFUSED: running elevated') 'no elevation refusal message'
    Check (@(Get-Runs $t).Count -eq 0) 'a run folder was created'
    Check ((Get-AliveTestApps).Count -eq 0) 'the exe was started'
}

Test-Case 'refuses-without-steam' {
    $t = New-TestEnv 'refuses-without-steam' @{ EVR_RIG_STEAM_PROCESS = 'evr_no_such_process' }
    $r = Invoke-Rig 'run.ps1' @('-Exe', $t.Exe)
    Check ($r.Code -eq 2) "exit code $($r.Code), expected 2"
    Check ($r.Output -match 'is not running') 'no Steam refusal message'
    Check (@(Get-Runs $t).Count -eq 0) 'a run folder was created'
}

Test-Case 'refuses-when-game-running' {
    $t = New-TestEnv 'refuses-when-game-running' @{ EVR_RIG_GAME_PROCESSES = 'DOOMEternalx64vk;DOOMSandBox64vk;idTechLauncher;evr_rig_testapp' }
    $game = Start-Process -FilePath $t.Exe -PassThru
    Start-Sleep -Milliseconds 500
    $r = Invoke-Rig 'run.ps1' @('-Exe', $t.Exe)
    Check ($r.Code -eq 2) "exit code $($r.Code), expected 2"
    Check ($r.Output -match 'game process is already running') 'no game-running refusal message'
    Check (@(Get-Runs $t).Count -eq 0) 'a run folder was created'
    Check (-not $game.HasExited) 'the already-running game was touched'
}

Test-Case 'refuses-paths-outside-test-root' {
    $outside = Join-Path (Split-Path -Parent $SuiteRoot) 'must-not-exist-guard'
    $t = New-TestEnv 'refuses-paths-outside-test-root' @{ EVR_RIG_SAVED_GAMES = $outside }
    foreach ($s in 'run.ps1', 'cleanup.ps1', 'stop.ps1') {
        $args2 = @(); if ($s -eq 'run.ps1') { $args2 = @('-Exe', $t.Exe) }
        $r = Invoke-Rig $s $args2
        Check ($r.Code -eq 1) "$s exit code $($r.Code), expected 1"
        Check ($r.Output -match 'outside the test root') "$s did not report the guard"
    }
    Check (-not (Test-Path -LiteralPath $outside)) 'the outside path was created'
    Check (@(Get-Runs $t).Count -eq 0) 'a run folder was created'
}

Test-Case 'round-trip-snapshot-run-stop-restore' {
    $t = New-TestEnv 'round-trip'
    $s = Start-TestRun $t
    Check ($s.Code -eq 0) "run.ps1 exit code $($s.Code): $($s.Output)"
    $dir = $s.Dir; $run = $s.Run
    Check (Test-Path -LiteralPath (Join-Path $dir 'CLEANUP_PENDING')) 'no CLEANUP_PENDING during the run'
    Check ($run.phase -eq 'running') "phase $($run.phase)"
    Check ($run.exe.sha256 -eq (Get-RigSha256 $t.Exe)) 'exe SHA-256 not recorded'
    Check ($run.args -match '\+r_fullscreen 0' -and $run.args -notmatch 's_volume') "default args: $($run.args)"
    Check ($run.args -match '\+r_windowWidth 2560 \+r_windowHeight 1440') "window size not the virtual display's: $($run.args)"
    Check ($run.sessionMute.requested -eq $true) 'audio session mute not the default'
    Check (@($run.warnings) -contains 'SESSION_MUTE_NOT_APPLIED') 'missing audio session not reported'
    Check ($null -ne $run.foreground -and $null -ne $run.foreground.PSObject.Properties['before']) 'foreground window before the launch not recorded'
    Check ($run.display.rect.width -eq 2560 -and $run.display.rect.height -eq 1440) "virtual display mode $($run.display.rect.width)x$($run.display.rect.height)"
    Check ($run.env.SteamAppId -eq '782330') 'SteamAppId not recorded'
    Check ($run.elevated -eq $false) 'elevation not recorded as false'
    Check ($null -ne $run.hags) 'HAGS state not recorded'
    Check ($run.exe.workingDirectory -eq $t.Game) 'working directory is not the game root'
    Check (@($run.processes).Count -eq 1 -and $null -ne (Get-Process -Id $run.processes[0].pid -ErrorAction SilentlyContinue)) 'started process not recorded or not alive'
    # Marker first: nothing in the run folder is older than the marker.
    $markerTime = (Get-Item -LiteralPath (Join-Path $dir 'CLEANUP_PENDING')).CreationTimeUtc
    $older = @(Get-ChildItem -LiteralPath $dir -Recurse -Force | Where-Object { $_.CreationTimeUtc -lt $markerTime })
    Check ($older.Count -eq 0) ('created before the marker: ' + (($older | ForEach-Object { $_.Name }) -join ', '))
    # Snapshot of both locations, saves included, with a manifest that verifies.
    $before = Join-Path $dir 'config-before'
    Check (@(Test-SettingsSnapshot $before).Count -eq 0) 'config-before does not verify'
    $locs = @(Get-SnapshotLocations $before)
    Check (@($locs | Where-Object { $_.name -eq 'saved-games' -and $_.existed }).Count -eq 1) 'Saved Games location not snapshotted'
    Check (@($locs | Where-Object { $_.name -eq 'steam-111' -and $_.existed }).Count -eq 1) 'Steam remote location not snapshotted'
    Check (Test-Path -LiteralPath (Join-Path $before 'steam-111\GAME-MANUAL1\game.details')) 'saves not in the snapshot'
    Check ((Read-RigManifest (Join-Path $before 'SHA256SUMS')).Count -eq $t.Orig.Count) 'manifest does not list every settings file'
    # Display for the work block, window placed on it.
    $stub = Get-Stub $t
    Check ($stub.active -and @($stub.calls) -contains 'add') 'virtual display not added'
    $marker = Read-Json (Join-Path $t.Runs 'DISPLAY_ADDED')
    Check ($marker -and $marker.session -eq $run.session) 'DISPLAY_ADDED missing or without the session'
    Check ($run.display.mode -eq 'virtual') "display mode $($run.display.mode)"
    Check (@($run.windows).Count -ge 1 -and $run.windows[0].x -eq -20000 -and $run.windows[0].y -eq -20000) 'window not placed on the virtual display'

    Simulate-GameWrites $t
    $profileTime = (Get-Item -LiteralPath $t.Profile).LastWriteTimeUtc
    $changedHash = Get-RigSha256 $t.Cfg
    Write-TestFile (Join-Path $t.Game 'base\qconsole.log') 'log line'

    $c = Invoke-Rig 'collect.ps1'
    Check ($c.Code -eq 0) "collect.ps1 exit code $($c.Code)"
    Check (@(Get-ChildItem -LiteralPath (Join-Path $dir 'screenshots') -Filter '*.png' -ErrorAction SilentlyContinue).Count -eq 1) 'no screenshot'
    Check (Test-Path -LiteralPath (Join-Path $dir 'logs\qconsole.log')) 'game log not collected'

    $st = Invoke-Rig 'stop.ps1'
    Check ($st.Code -eq 0) "stop.ps1 exit code $($st.Code)"
    $stop = @(Read-Json (Join-Path $dir 'stop.json'))
    Check (@($stop | ForEach-Object { $_.processes } | Where-Object { $_.outcome -eq 'closed' }).Count -eq 1) 'the test app was not closed gracefully'
    Check ((Get-AliveTestApps).Count -eq 0) 'the test app is still running'
    Check (-not (Test-Path -LiteralPath (Join-Path $dir 'CLEANUP_PENDING'))) 'marker not cleared after a verified restore'
    $cj = Read-Json (Join-Path $dir 'cleanup.json')
    Check ($cj.outcome -eq 'restored' -and $cj.verified) "cleanup outcome $($cj.outcome)"
    Check (@($cj.restored) -contains 'saved-games\base\DOOMEternalConfig.cfg') 'changed file not restored'
    Check (@($cj.restored) -contains 'saved-games\user\config.json') 'removed file not restored'
    Check ($cj.cloudPolicy -eq 'keep-game-version' -and @($cj.cloudFiles).Count -eq 0) 'unchanged cloud files reported'
    Check (@($cj.restored).Count -eq 2) "unexpected restores: $(@($cj.restored) -join ', ')"
    Check ((Get-Item -LiteralPath $t.Profile).LastWriteTimeUtc -eq $profileTime) 'unchanged file was rewritten'
    Check (Test-Path -LiteralPath (Join-Path $t.Saved 'base\added-during-run.txt')) 'a file added during the run was deleted'
    $now = Get-SettingsState $t
    foreach ($k in $t.Orig.Keys) { Check ($now[$k] -eq $t.Orig[$k]) "not restored: $k" }
    Check ((Get-RigSha256 (Join-Path $dir 'config-after\saved-games\base\DOOMEternalConfig.cfg')) -eq $changedHash) 'config-after does not hold the post-exit file'
    $replaced = @(Get-ChildItem -LiteralPath (Join-Path $dir 'config-replaced') -Recurse -File -ErrorAction SilentlyContinue | Where-Object { $_.Name -eq 'DOOMEternalConfig.cfg' })
    Check ($replaced.Count -eq 1 -and (Get-RigSha256 $replaced[0].FullName) -eq $changedHash) 'overwritten file not kept in config-replaced'
    Check ((Get-Stub $t).active) 'display removed at the end of a run (it belongs to the work block)'

    # Idempotent: further cleanups change nothing.
    $cjTime = (Get-Item -LiteralPath (Join-Path $dir 'cleanup.json')).LastWriteTimeUtc
    $state = Get-SettingsState $t
    foreach ($i in 1, 2) {
        $again = Invoke-Rig 'cleanup.ps1' @('-All')
        Check ($again.Code -eq 0) "cleanup.ps1 -All (pass $i) exit code $($again.Code)"
    }
    Check ((Get-Item -LiteralPath (Join-Path $dir 'cleanup.json')).LastWriteTimeUtc -eq $cjTime) 'a finished run was cleaned again'
    Check (Test-SettingsEqual $state (Get-SettingsState $t)) 'settings changed by a repeated cleanup'
    Check ((Get-Stub $t).active) 'display removed without -EndBlock'
    Check (Test-Path -LiteralPath $dir) 'run folder removed'

    $end = Invoke-Rig 'cleanup.ps1' @('-EndBlock')
    Check ($end.Code -eq 0) "cleanup.ps1 -EndBlock exit code $($end.Code)"
    Check (-not (Get-Stub $t).active) 'display not removed at the end of the block'
    Check (-not (Test-Path -LiteralPath (Join-Path $t.Runs 'DISPLAY_ADDED'))) 'DISPLAY_ADDED not cleared'
    $log = @([IO.File]::ReadAllLines((Join-Path $t.Runs 'display-log.jsonl')) | ForEach-Object { $_ | ConvertFrom-Json })
    Check (@($log | Where-Object { $_.action -eq 'add' -and $_.ok }).Count -eq 1 -and @($log | Where-Object { $_.action -eq 'remove' -and $_.ok }).Count -eq 1) 'display block log incomplete'
}

Test-Case 'unexplained-settings-change-is-reported-not-adopted' {
    $t = New-TestEnv 'unexplained-change'
    $s = Start-TestRun $t
    Check ($s.Code -eq 0) "first run exit code $($s.Code)"
    Check ((Invoke-Rig 'stop.ps1').Code -eq 0) 'first stop failed'
    Write-TestFile (Join-Path $t.Saved 'base\DOOMEternalConfig.local') "seta r_mode `"5`"`r`n"
    $count = @(Get-Runs $t).Count
    $r = Invoke-Rig 'run.ps1' @('-Exe', $t.Exe)
    Check ($r.Code -eq 2) "exit code $($r.Code), expected 2"
    Check ($r.Output -match 'changed: saved-games\\base\\DOOMEternalConfig.local') 'difference not reported'
    Check (@(Get-Runs $t).Count -eq $count) 'a run folder was created'
    $r2 = Invoke-Rig 'run.ps1' @('-Exe', $t.Exe, '-AcknowledgeSettingsChange')
    Check ($r2.Code -eq 0) "acknowledged run exit code $($r2.Code)"
    $run = Read-Json (Join-Path (@(Get-Runs $t)[-1]) 'run.json')
    Check ($run.baseline.acknowledged -eq $true) 'acknowledgement not recorded'
    Check ((Invoke-Rig 'stop.ps1' @('-EndBlock')).Code -eq 0) 'second stop failed'
}

Test-Case 'game-log-files-never-block-a-run' {
    $t = New-TestEnv 'game-logs'
    Write-TestFile (Join-Path $t.Saved 'base\qconsole.log') 'first'
    $s = Start-TestRun $t
    Check ($s.Code -eq 0) "first run exit code $($s.Code)"
    Check ((Invoke-Rig 'stop.ps1').Code -eq 0) 'first stop failed'
    # A game start outside the rig scripts rewrites only the game's own logs.
    Write-TestFile (Join-Path $t.Saved 'base\qconsole.log') 'written by a later game start'
    Write-TestFile (Join-Path $t.Saved 'base\structured.log') 'new'
    Write-TestFile (Join-Path $t.Saved 'base\logs\extra.log') 'new'
    $r = Invoke-Rig 'run.ps1' @('-Exe', $t.Exe)
    Check ($r.Code -eq 0) "run after a log-only change: exit code $($r.Code)"
    Check ($r.Output -notmatch 'REFUSED') 'a log-only change refused the run'
    Write-TestFile (Join-Path $t.Saved 'base\qconsole.log') 'written during the run'
    Check ((Invoke-Rig 'stop.ps1' @('-EndBlock')).Code -eq 0) 'second stop failed'
    Check ([IO.File]::ReadAllText((Join-Path $t.Saved 'base\qconsole.log')) -eq 'written during the run') 'a game log file was restored'
    # A real setting next to the logs still counts.
    Write-TestFile (Join-Path $t.Saved 'base\DOOMEternalConfig.local') "seta r_mode `"6`"`r`n"
    $c = Invoke-Rig 'run.ps1' @('-Exe', $t.Exe)
    Check ($c.Code -eq 2) "a settings change was not refused: exit code $($c.Code)"
}

Test-Case 'failed-restore-keeps-marker-and-blocks-next-run' {
    $t = New-TestEnv 'failed-restore'
    $s = Start-TestRun $t
    Check ($s.Code -eq 0) "run exit code $($s.Code)"
    Write-TestFile $t.Local "seta r_mode `"7`"`r`n"
    # Another handle on the file that allows reading but not replacing: the restore must fail.
    $lock = [IO.File]::Open($t.Local, 'Open', 'Read', 'Read')
    try {
        $st = Invoke-Rig 'stop.ps1'
        Check ($st.Code -eq 1) "stop.ps1 exit code $($st.Code), expected 1"
        Check ((Get-AliveTestApps).Count -eq 0) 'process not stopped'
        Check (Test-Path -LiteralPath (Join-Path $s.Dir 'CLEANUP_PENDING')) 'marker cleared after a failed restore'
        $cj = Read-Json (Join-Path $s.Dir 'cleanup.json')
        Check ($cj.outcome -eq 'failed' -and $cj.reason -match 'DOOMEternalConfig.local') "cleanup.json: $($cj.outcome) $($cj.reason)"
        $count = @(Get-Runs $t).Count
        $r = Invoke-Rig 'run.ps1' @('-Exe', $t.Exe)
        Check ($r.Code -eq 2) "next run exit code $($r.Code), expected 2 (refused)"
        Check ($r.Output -match 'could not be cleaned up') 'refusal reason not reported'
        Check (@(Get-Runs $t).Count -eq $count) 'a run folder was created while a cleanup was failing'
        Check ((Get-AliveTestApps).Count -eq 0) 'the exe was started while a cleanup was failing'
    } finally { $lock.Dispose() }
    $c = Invoke-Rig 'cleanup.ps1'
    Check ($c.Code -eq 0) "cleanup after the lock was released: exit code $($c.Code)"
    Check (-not (Test-Path -LiteralPath (Join-Path $s.Dir 'CLEANUP_PENDING'))) 'marker not cleared after the retry'
    Check ((Get-RigSha256 $t.Local) -eq $t.Orig[$t.Local]) 'file not restored'
    Check ((Get-RigSha256 (Join-Path $s.Dir 'config-after\saved-games\base\DOOMEternalConfig.local')) -ne $t.Orig[$t.Local]) 'config-after lost the post-exit file'
}

Test-Case 'corrupt-snapshot-is-never-restored' {
    $t = New-TestEnv 'corrupt-snapshot'
    $s = Start-TestRun $t
    Check ($s.Code -eq 0) "run exit code $($s.Code)"
    Simulate-GameWrites $t
    $gameHash = Get-RigSha256 $t.Cfg
    $snapFile = Join-Path $s.Dir 'config-before\saved-games\base\DOOMEternalConfig.cfg'
    $good = [IO.File]::ReadAllBytes($snapFile)
    [IO.File]::WriteAllText($snapFile, 'tampered')
    $st = Invoke-Rig 'stop.ps1'
    Check ($st.Code -eq 1) "stop.ps1 exit code $($st.Code), expected 1"
    Check (Test-Path -LiteralPath (Join-Path $s.Dir 'CLEANUP_PENDING')) 'marker cleared with a corrupt snapshot'
    Check ((Read-Json (Join-Path $s.Dir 'cleanup.json')).reason -match 'config-before failed verification') 'reason not recorded'
    Check ((Get-RigSha256 $t.Cfg) -eq $gameHash) 'a file was written from a corrupt snapshot'
    Check (-not (Test-Path -LiteralPath $t.Json)) 'restore ran with a corrupt snapshot'
    [IO.File]::WriteAllBytes($snapFile, $good)
    $c = Invoke-Rig 'cleanup.ps1'
    Check ($c.Code -eq 0) "cleanup after repair: exit code $($c.Code)"
    $now = Get-SettingsState $t
    foreach ($k in $t.Orig.Keys) { Check ($now[$k] -eq $t.Orig[$k]) "not restored: $k" }
}

Test-Case 'stop-kills-a-hung-process-within-the-bound' {
    $t = New-TestEnv 'hung'
    $s = Start-TestRun $t @('-Args', '--hang')
    Check ($s.Code -eq 0) "run exit code $($s.Code)"
    $st = Invoke-Rig 'stop.ps1'
    Check ($st.Code -eq 0) "stop.ps1 exit code $($st.Code)"
    $stop = @(Read-Json (Join-Path $s.Dir 'stop.json'))
    Check (@($stop | ForEach-Object { $_.processes } | Where-Object { $_.outcome -eq 'killed' }).Count -eq 1) 'hung process not recorded as killed'
    Check ((Get-AliveTestApps).Count -eq 0) 'hung process still running'
    # close timeout 5 s + kill wait + Steam sync (quiet 1 s) + two PowerShell starts
    Check ($st.Seconds -lt 30) ("stop took {0:n1} s" -f $st.Seconds)
    Check ($st.Seconds -ge 5) ("stop returned before the close timeout ({0:n1} s)" -f $st.Seconds)
    Check (-not (Test-Path -LiteralPath (Join-Path $s.Dir 'CLEANUP_PENDING'))) 'marker not cleared'
}

foreach ($step in 'marker', 'snapshot', 'display', 'launch', 'started') {
    Test-Case "killed-at-$step-recovers-on-next-run" {
        $t = New-TestEnv "killed-at-$step"
        Set-TestVar 'EVR_RIG_TEST_ABORT_AT' $step
        $k = Invoke-Rig 'run.ps1' @('-Exe', $t.Exe)
        Set-TestVar 'EVR_RIG_TEST_ABORT_AT' ''
        Check ($k.Code -ne 0) 'the kill point did not stop run.ps1'
        $killed = @(Get-Runs $t)[0]
        Check (Test-Path -LiteralPath (Join-Path $killed 'CLEANUP_PENDING')) 'no marker after the kill'
        $stub = Get-Stub $t
        if ($step -eq 'marker') {
            Check (-not (Test-Path -LiteralPath (Join-Path $killed 'config-before'))) 'snapshot taken before the marker step finished'
            Check (-not $stub.active) 'display added before the marker step finished'
        }
        if ($step -eq 'snapshot') { Check (-not $stub.active) 'display added before the snapshot step finished' }
        if (@('marker', 'snapshot', 'display', 'launch') -contains $step) { Check ((Get-AliveTestApps).Count -eq 0) 'exe started too early' }
        if ($step -eq 'started') { Check ((Get-AliveTestApps).Count -eq 1) 'exe not running after a kill at started' }
        if (@('launch', 'started') -contains $step) { Simulate-GameWrites $t }
        if ($step -eq 'started') {
            # The killed run's game still runs: an implicit cleanup never stops it, only stop.ps1 -Run does.
            $refused = Invoke-Rig 'run.ps1' @('-Exe', $t.Exe)
            Check ($refused.Code -eq 2) "run with a live pending run: exit code $($refused.Code), expected 2"
            Check ($refused.Output -match 'stop\.ps1 -Run') 'refusal does not say how to stop the live run'
            Check ((Get-AliveTestApps).Count -eq 1) 'an implicit cleanup stopped a live run'
            Check ((Invoke-Rig 'stop.ps1' @('-Run', (Split-Path -Leaf $killed))).Code -eq 0) 'stop.ps1 -Run of the killed run failed'
        }

        $n = Invoke-Rig 'run.ps1' @('-Exe', $t.Exe)
        Check ($n.Code -eq 0) "next run exit code $($n.Code)"
        Check (-not (Test-Path -LiteralPath (Join-Path $killed 'CLEANUP_PENDING'))) 'killed run not cleaned up by the next run'
        Check (Test-Path -LiteralPath $killed) 'killed run folder removed'
        $cj = Read-Json (Join-Path $killed 'cleanup.json')
        Check ($cj -and @('restored', 'nothing-to-restore') -contains $cj.outcome) "killed run cleanup outcome: $($cj.outcome)"
        Check ((Get-AliveTestApps).Count -eq 1) 'expected exactly the new run''s process'
        Check (-not (Test-Path -LiteralPath (Join-Path $t.Runs 'LOCK'))) 'rig lock left behind by a finished run.ps1'
        $newRun = Read-Json (Join-Path (@(Get-Runs $t)[-1]) 'run.json')
        $now = Get-SettingsState $t
        foreach ($key in $t.Orig.Keys) { Check ($now[$key] -eq $t.Orig[$key]) "not restored after the kill: $key" }
        $st = Invoke-Rig 'stop.ps1' @('-EndBlock')
        Check ($st.Code -eq 0) "stop exit code $($st.Code)"
        Check (-not (Get-Stub $t).active) 'display not removed at the end of the block'
        Check ((Get-AliveTestApps).Count -eq 0) 'process left running'
        Check ($newRun.phase -eq 'running') "new run phase $($newRun.phase)"
    }
}

foreach ($step in 'cleanup-after-copy', 'cleanup-mid-restore') {
    Test-Case "killed-at-$step-recovers" {
        $t = New-TestEnv "killed-at-$step"
        $s = Start-TestRun $t
        Check ($s.Code -eq 0) "run exit code $($s.Code)"
        Simulate-GameWrites $t
        $postExit = Get-RigSha256 $t.Cfg
        Set-TestVar 'EVR_RIG_TEST_ABORT_AT' $step
        $k = Invoke-Rig 'stop.ps1'
        Set-TestVar 'EVR_RIG_TEST_ABORT_AT' ''
        Check ($k.Code -ne 0) 'the kill point did not stop the cleanup'
        Check (Test-Path -LiteralPath (Join-Path $s.Dir 'CLEANUP_PENDING')) 'marker cleared by an interrupted cleanup'
        $c = Invoke-Rig 'cleanup.ps1'
        Check ($c.Code -eq 0) "cleanup exit code $($c.Code)"
        Check (-not (Test-Path -LiteralPath (Join-Path $s.Dir 'CLEANUP_PENDING'))) 'marker not cleared'
        $now = Get-SettingsState $t
        foreach ($key in $t.Orig.Keys) { Check ($now[$key] -eq $t.Orig[$key]) "not restored: $key" }
        Check ((Get-RigSha256 (Join-Path $s.Dir 'config-after\saved-games\base\DOOMEternalConfig.cfg')) -eq $postExit) 'config-after overwritten by the retry'
        Check (@(Get-ChildItem -LiteralPath $t.Saved, $t.Remote -Recurse -Filter '*.evr-restore.tmp').Count -eq 0) 'temporary restore files left behind'
    }
}

Test-Case 'early-exit-is-a-named-error' {
    $t = New-TestEnv 'early-exit'
    $s = Start-TestRun $t @('-Args', '--exit-now')
    Check ($s.Code -eq 3) "exit code $($s.Code), expected 3"
    Check (@($s.Run.errors | Where-Object { $_ -match '^EXITED_EARLY' }).Count -eq 1) 'EXITED_EARLY not recorded'
    Check (-not (Test-Path -LiteralPath (Join-Path $s.Dir 'CLEANUP_PENDING'))) 'run not cleaned up after the early exit'
}

Test-Case 'handoff-is-recorded-and-stopped' {
    $t = New-TestEnv 'handoff'
    $s = Start-TestRun $t @('-Args', '--handoff')
    Check ($s.Code -eq 4) "exit code $($s.Code), expected 4"
    $h = @($s.Run.processes | Where-Object { $_.role -eq 'handoff' })
    Check ($h.Count -eq 1) 'hand-off process not recorded'
    Check (@($s.Run.warnings) -contains 'HANDOFF') 'HANDOFF warning not recorded'
    if ($h.Count -eq 1) { Check ($null -ne (Get-Process -Id $h[0].pid -ErrorAction SilentlyContinue)) 'hand-off process not alive' }
    $st = Invoke-Rig 'stop.ps1'
    Check ($st.Code -eq 0) "stop exit code $($st.Code)"
    Check ((Get-AliveTestApps).Count -eq 0) 'hand-off process left running'
}

Test-Case 'no-virtual-display-falls-back-with-warning' {
    $t = New-TestEnv 'no-display'
    Set-Stub $t @{ available = $false }
    $s = Start-TestRun $t
    Check ($s.Code -eq 0) "exit code $($s.Code)"
    Check ($s.Run.display.mode -eq 'none' -and @($s.Run.warnings) -contains 'NO_VIRTUAL_DISPLAY') 'fallback not recorded'
    Check ($s.Output -match 'WARNING') 'no warning printed'
    Check (@((Get-Stub $t).calls).Count -eq 0) 'display controller called although unavailable'
    Check (-not (Test-Path -LiteralPath (Join-Path $t.Runs 'DISPLAY_ADDED'))) 'DISPLAY_ADDED written without a display'
    Check ((Invoke-Rig 'stop.ps1').Code -eq 0) 'stop failed'
}

Test-Case 'display-add-failure-aborts-the-run' {
    $t = New-TestEnv 'display-add-fails'
    Set-Stub $t @{ failAdd = $true }
    $s = Start-TestRun $t
    Check ($s.Code -eq 1) "exit code $($s.Code), expected 1"
    Check ((Get-AliveTestApps).Count -eq 0) 'exe started although the display could not be added'
    Check (-not (Test-Path -LiteralPath (Join-Path $t.Runs 'DISPLAY_ADDED'))) 'DISPLAY_ADDED left behind after a failed add'
    Check (-not (Test-Path -LiteralPath (Join-Path $s.Dir 'CLEANUP_PENDING'))) 'run not cleaned up'
}

Test-Case 'display-from-an-earlier-session-is-removed' {
    $t = New-TestEnv 'stale-display'
    Check ((Invoke-Rig 'session.ps1' @('start')).Code -eq 0) 'session start failed'
    $s = Start-TestRun $t
    Check ($s.Code -eq 0) "run exit code $($s.Code)"
    Check ((Invoke-Rig 'stop.ps1').Code -eq 0) 'stop failed'
    Check ((Get-Stub $t).active) 'display removed inside the work block'
    Check ((Invoke-Rig 'session.ps1' @('start')).Code -eq 0) 'second session start failed'
    Check (-not (Get-Stub $t).active) 'display of the earlier session not removed'
    Check (-not (Test-Path -LiteralPath (Join-Path $t.Runs 'DISPLAY_ADDED'))) 'stale DISPLAY_ADDED kept'
    Check ((Invoke-Rig 'session.ps1' @('end')).Code -eq 0) 'session end failed'
}

Test-Case 'owner-run-uses-his-display-and-sound-with-the-same-restore' {
    $t = New-TestEnv 'owner-run'
    $s = Start-TestRun $t @('-Owner')
    Check ($s.Code -eq 0) "exit code $($s.Code)"
    Check ($s.Run.owner -eq $true) 'owner run not recorded'
    Check ($s.Run.args -notmatch 's_volume' -and $s.Run.args -notmatch 'r_fullscreen') "forced cvars on an owner run: $($s.Run.args)"
    Check ($s.Run.display.mode -eq 'none') "display mode $($s.Run.display.mode)"
    Check (@((Get-Stub $t).calls).Count -eq 0) 'display controller called for an owner run'
    Check (@($s.Run.windows).Count -ge 1 -and $s.Run.windows[0].x -ne -20000 -and $s.Run.windows[0].y -ne -20000) 'window moved on an owner run'
    Check (Test-Path -LiteralPath (Join-Path $s.Dir 'CLEANUP_PENDING')) 'no marker on an owner run'
    Check (@(Test-SettingsSnapshot (Join-Path $s.Dir 'config-before')).Count -eq 0) 'no verified snapshot on an owner run'
    Simulate-GameWrites $t
    $st = Invoke-Rig 'stop.ps1'
    Check ($st.Code -eq 0) "stop exit code $($st.Code)"
    Check (-not (Test-Path -LiteralPath (Join-Path $s.Dir 'CLEANUP_PENDING'))) 'marker not cleared'
    $now = Get-SettingsState $t
    foreach ($k in $t.Orig.Keys) { Check ($now[$k] -eq $t.Orig[$k]) "not restored: $k" }
}

Test-Case 'audio-session-mute-is-the-default-and-restores-the-prior-state' {
    $t = New-TestEnv 'session-mute'
    $s = Start-TestRun $t @('-Args', '--audio')
    Check ($s.Code -eq 0) "exit code $($s.Code)"
    Check ($s.Run.args -notmatch 's_volume') "+s_volume passed by default: $($s.Run.args)"
    if (-not $s.Run.sessionMute.applied) {
        Skip 'no audio session appeared for the test app (no audio device?)'
        [void](Invoke-Rig 'stop.ps1')
        return
    }
    Initialize-RigNative
    $pid1 = [int]$s.Run.processes[0].pid
    Check (@([EvrRig.Audio]::Query(@($pid1)) | Where-Object { $_.Muted }).Count -ge 1) 'audio session not muted'
    Check ($s.Run.sessionMute.priorMuted -eq $false) 'prior state not recorded as unmuted'
    $st = Invoke-Rig 'stop.ps1'
    Check ($st.Code -eq 0) "stop exit code $($st.Code)"
    Check (@(Read-Json (Join-Path $s.Dir 'stop.json'))[0].unmuted -eq $true) 'stop did not undo the mute before closing'
    Check ((Read-Json (Join-Path $s.Dir 'run.json')).sessionMute.restored -eq $true) 'restore not recorded'

    # The process dies before the mute is put back: "mute may persist" only when it was not muted before.
    $s3 = Start-TestRun $t @('-Args', '--audio')
    Stop-Process -Id ([int]$s3.Run.processes[0].pid) -Force
    Start-Sleep -Milliseconds 500
    $c = Invoke-Rig 'cleanup.ps1'
    Check ($c.Code -eq 0) "cleanup exit code $($c.Code)"
    Check ((Read-Json (Join-Path $s3.Dir 'run.json')).muteMayPersist -eq $true) 'mute may persist not recorded in run.json'
    Check ($c.Output -match 'mute may persist') 'mute may persist not reported'
    # Windows now remembers that leaked mute for this exe; the next run knows it is ours and puts back unmuted.
    $s3b = Start-TestRun $t @('-Args', '--audio')
    Check ($s3b.Run.sessionMute.priorMuted -eq $false -and $s3b.Run.sessionMute.leakFrom -eq (Split-Path -Leaf $s3.Dir)) 'leaked mute taken for the owner''s'
    Check ((Read-Json (Join-Path $s3.Dir 'run.json')).muteCleared -eq (Split-Path -Leaf $s3b.Dir)) 'leak not marked as cleared'
    Check ((Invoke-Rig 'stop.ps1').Code -eq 0) 'stop after the leaked mute failed'
    # The owner muted the game in the Windows mixer: the prior state is muted, and it is put back as muted.
    $s2 = Start-TestRun $t @('-Args', '--audio --audio-muted')
    Check ($s2.Code -eq 0) "muted-before run exit code $($s2.Code)"
    Check ($s2.Run.sessionMute.priorMuted -eq $true) 'prior muted state not recorded'
    Check ((Invoke-Rig 'stop.ps1').Code -eq 0) 'stop of the muted-before run failed'
    Check ((Read-Json (Join-Path $s2.Dir 'stop.json'))[0].unmuted -eq $true) 'prior state not put back'

    $s4 = Start-TestRun $t @('-Args', '--audio --audio-muted')
    Stop-Process -Id ([int]$s4.Run.processes[0].pid) -Force
    Start-Sleep -Milliseconds 500
    Check ((Invoke-Rig 'cleanup.ps1').Code -eq 0) 'cleanup after the muted-before kill failed'
    Check ((Read-Json (Join-Path $s4.Dir 'run.json')).muteMayPersist -ne $true) 'mute may persist recorded although the game was muted before'

    $s5 = Start-TestRun $t @('-NoMute', '-Args', '--audio')
    Check ($null -eq $s5.Run.sessionMute) '-NoMute still muted the session'
    [void](Invoke-Rig 'stop.ps1' @('-EndBlock'))
}

Test-Case 'args-and-env-arrive-comma-joined-via-file' {
    $t = New-TestEnv 'comma-args'
    # What "powershell -File run.ps1 -Args "+logFile 2","--exit-now"" hands the script: one joined string.
    $r = Invoke-Rig 'run.ps1' @('-Exe', $t.Exe, '-Args', '+logFile 2,--exit-now', '-GameEnv', 'EVR_A=1,EVR_B=two')
    Check ($r.Code -eq 3) "exit code $($r.Code), expected 3 (--exit-now must reach the exe as its own argument)"
    $run = Read-Json (Join-Path (@(Get-Runs $t)[-1]) 'run.json')
    Check ($run.args -match '\+logFile 2 --exit-now') "args: $($run.args)"
    Check ($run.env.EVR_A -eq '1' -and $run.env.EVR_B -eq 'two') 'comma-joined -GameEnv not split'
    $r2 = Invoke-Rig 'run.ps1' @('-Exe', $t.Exe, '-Args', '+logFile 2', '--exit-now')
    Check ($r2.Code -eq 3) "several -Args values: exit code $($r2.Code), expected 3"
}

Test-Case 'game-env-does-not-leak-into-the-calling-process' {
    $t = New-TestEnv 'env-restore'
    # A batch caller runs several games from one PowerShell process: after run.ps1 returns, that
    # process must hold its own values again (EVR_KEEP) and none of the game's (EVR_LEAK).
    $cmd = "`$env:EVR_KEEP = 'mine'; & '{0}' -Exe '{1}' -Args '+logFile 2','--exit-now' -GameEnv 'EVR_LEAK=1','EVR_KEEP=game' | Out-Null; " -f (Join-Path $RigDir 'run.ps1'), $t.Exe
    $cmd += "'LEAK=' + [Environment]::GetEnvironmentVariable('EVR_LEAK', 'Process'); 'KEEP=' + `$env:EVR_KEEP"
    $out = & powershell.exe -NoProfile -ExecutionPolicy Bypass -Command $cmd 2>&1 | Out-String
    $run = Read-Json (Join-Path (@(Get-Runs $t)[-1]) 'run.json')
    Check ($run.env.EVR_LEAK -eq '1' -and $run.env.EVR_KEEP -eq 'game') 'game env not recorded for the run'
    Check ($out -match '(?m)^LEAK=\s*$') "EVR_LEAK leaked into the caller: $out"
    Check ($out -match '(?m)^KEEP=mine\s*$') "EVR_KEEP not restored for the caller: $out"
}

Test-Case 'stop-waits-until-a-slowly-exiting-game-is-gone' {
    $t = New-TestEnv 'slow-exit' @{ EVR_RIG_GAME_PROCESSES = 'evr_rig_testapp' }
    $s = Start-TestRun $t @('-Args', '--slow-exit=2500')
    Check ($s.Code -eq 0) "run exit code $($s.Code)"
    Simulate-GameWrites $t
    $st = Invoke-Rig 'stop.ps1'
    Check ($st.Code -eq 0) "stop exit code $($st.Code): the cleanup saw the exiting game"
    $p = @(@(Read-Json (Join-Path $s.Dir 'stop.json'))[0].processes)
    Check ($p.Count -eq 1 -and $p[0].outcome -eq 'closed' -and -not $p[0].stillListed) 'process not closed and gone'
    Check (-not (Test-Path -LiteralPath (Join-Path $s.Dir 'CLEANUP_PENDING'))) 'marker not cleared'
    $now = Get-SettingsState $t
    foreach ($k in $t.Orig.Keys) { Check ($now[$k] -eq $t.Orig[$k]) "not restored: $k" }
}

Test-Case 'restore-never-overwrites-changes-made-after-the-run-ended' {
    $t = New-TestEnv 'late-change'
    $s = Start-TestRun $t
    Check ($s.Code -eq 0) "run exit code $($s.Code)"
    $name = Split-Path -Leaf $s.Dir
    Write-TestFile $t.Json '{"v":2,"by":"the run"}'
    Write-TestFile $t.Cfg 'cfg changed by the run'
    $lock = [IO.File]::Open($t.Cfg, 'Open', 'Read', 'Read')
    try {
        Check ((Invoke-Rig 'stop.ps1').Code -eq 1) 'stop did not fail with the config file locked'
    } finally { $lock.Dispose() }
    Check ((Get-RigSha256 $t.Json) -eq $t.Orig[$t.Json]) 'file not restored by the first attempt'
    # The owner plays and changes a setting after the failed cleanup.
    Write-TestFile $t.Json '{"v":3,"by":"the owner after the failed cleanup"}'
    $v3 = Get-RigSha256 $t.Json
    $c = Invoke-Rig 'cleanup.ps1'
    Check ($c.Code -eq 1) "retry exit code $($c.Code), expected 1 (held)"
    Check ((Get-RigSha256 $t.Json) -eq $v3) 'the owner''s later change was overwritten'
    Check ((Get-RigSha256 $t.Cfg) -eq $t.Orig[$t.Cfg]) 'the unchanged-since-exit config file was not restored'
    Check (Test-Path -LiteralPath (Join-Path $s.Dir 'CLEANUP_PENDING')) 'marker cleared with a held file'
    Check (@((Read-Json (Join-Path $s.Dir 'cleanup.json')).held) -contains 'saved-games\user\config.json') 'held file not recorded'
    Check ($c.Output -match 'AcknowledgeSettingsChange') 'no instruction for the acknowledgement'
    Check ((Invoke-Rig 'run.ps1' @('-Exe', $t.Exe)).Code -eq 2) 'a new run started while a file is held'
    $a = Invoke-Rig 'cleanup.ps1' @('-Run', $name, '-AcknowledgeSettingsChange')
    Check ($a.Code -eq 0) "acknowledged cleanup exit code $($a.Code)"
    Check ((Get-RigSha256 $t.Json) -eq $t.Orig[$t.Json]) 'file not restored after the acknowledgement'
    $kept = @(Get-ChildItem -LiteralPath (Join-Path $s.Dir 'config-replaced') -Recurse -File | Where-Object { (Get-RigSha256 $_.FullName) -eq $v3 })
    Check ($kept.Count -eq 1) 'the owner''s later change was not kept in config-replaced'
}

function Start-RigAsync([string]$Script, [string[]]$Arguments) {
    $psi = New-Object Diagnostics.ProcessStartInfo 'powershell.exe'
    $psi.Arguments = (@('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Format-Arg (Join-Path $RigDir $Script))) + @($Arguments | ForEach-Object { Format-Arg $_ })) -join ' '
    $psi.UseShellExecute = $false; $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true; $psi.CreateNoWindow = $true
    $p = [Diagnostics.Process]::Start($psi)
    return [pscustomobject]@{ Process = $p; Out = $p.StandardOutput.ReadToEndAsync(); Err = $p.StandardError.ReadToEndAsync() }
}

Test-Case 'one-rig-script-at-a-time' {
    $t = New-TestEnv 'lock'
    Set-TestVar 'EVR_RIG_TEST_SLEEP_AT' 'marker:8'
    $async = Start-RigAsync 'run.ps1' @('-Exe', $t.Exe)
    Set-TestVar 'EVR_RIG_TEST_SLEEP_AT' ''
    $deadline = (Get-Date).AddSeconds(30)
    $marker = $null
    while (-not $marker -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 100
        $marker = @(Get-Runs $t | ForEach-Object { Join-Path $_ 'CLEANUP_PENDING' } | Where-Object { Test-Path -LiteralPath $_ })[0]
    }
    Check ([bool]$marker) 'run.ps1 did not write its marker'
    Set-TestVar 'EVR_RIG_LOCK_WAIT_SEC' '1'
    $busy = Invoke-Rig 'cleanup.ps1'
    Check ($busy.Code -eq 1 -and $busy.Output -match 'BUSY') "cleanup during run.ps1: exit code $($busy.Code), expected 1 BUSY"
    Check ($marker -and (Test-Path -LiteralPath $marker)) 'a concurrent cleanup removed the marker of a run being set up'
    Set-TestVar 'EVR_RIG_LOCK_WAIT_SEC' '60'
    $waited = Invoke-Rig 'cleanup.ps1'
    Check ($waited.Code -eq 0) "cleanup after waiting for the lock: exit code $($waited.Code)"
    Check ($marker -and (Test-Path -LiteralPath $marker)) 'the cleanup that waited removed the live run''s marker'
    Check ((Get-AliveTestApps).Count -eq 1) 'the cleanup that waited stopped the live run'
    [void]$async.Process.WaitForExit(60000)
    Check ($async.Process.ExitCode -eq 0) "run.ps1 exit code $($async.Process.ExitCode)"
    Check ((Invoke-Rig 'stop.ps1').Code -eq 0) 'stop failed'
    Check (-not (Test-Path -LiteralPath $marker)) 'marker not cleared by stop'
}

Test-Case 'implicit-cleanup-never-stops-a-live-run-or-touches-an-owner-run' {
    $t = New-TestEnv 'implicit'
    $o = Start-TestRun $t @('-Owner')
    Check ($o.Code -eq 0) "owner run exit code $($o.Code)"
    $ownerPid = [int]$o.Run.processes[0].pid
    $name = Split-Path -Leaf $o.Dir
    Simulate-GameWrites $t
    $gameState = Get-SettingsState $t
    Check ((Invoke-Rig 'run.ps1' @('-Exe', $t.Exe)).Code -eq 2) 'a scripted run started while the owner''s run is pending'
    Check ($null -ne (Get-Process -Id $ownerPid -ErrorAction SilentlyContinue)) 'run.ps1 stopped the owner''s game'
    Check ((Invoke-Rig 'cleanup.ps1').Code -eq 0) 'implicit cleanup failed'
    Check ((Invoke-Rig 'session.ps1' @('end')).Code -eq 0) 'session end failed'
    Check ($null -ne (Get-Process -Id $ownerPid -ErrorAction SilentlyContinue)) 'an implicit cleanup stopped the owner''s game'
    Stop-Process -Id $ownerPid -Force
    Start-Sleep -Milliseconds 500
    Check ((Invoke-Rig 'cleanup.ps1').Code -eq 0) 'implicit cleanup after the owner quit failed'
    Check (Test-Path -LiteralPath (Join-Path $o.Dir 'CLEANUP_PENDING')) 'an implicit cleanup cleaned an owner run'
    Check (Test-SettingsEqual $gameState (Get-SettingsState $t)) 'an implicit cleanup restored an owner run'
    Check ((Invoke-Rig 'stop.ps1' @('-Run', $name)).Code -eq 0) 'stop.ps1 -Run of the owner run failed'
    Check (-not (Test-Path -LiteralPath (Join-Path $o.Dir 'CLEANUP_PENDING'))) 'owner run not cleaned by name'
    $now = Get-SettingsState $t
    foreach ($k in $t.Orig.Keys) { Check ($now[$k] -eq $t.Orig[$k]) "not restored: $k" }
}

Test-Case 'display-query-error-is-a-failure-not-unavailable' {
    $t = New-TestEnv 'display-query-error'
    Set-Stub $t @{ queryError = 'QueryDisplayConfig failed: 5' }
    $s = Start-TestRun $t
    Check ($s.Code -eq 1) "run exit code $($s.Code), expected 1"
    Check ((Get-AliveTestApps).Count -eq 0) 'the exe was started although the display state is unknown'
    Check (@($s.Run.errors | Where-Object { $_ -match 'DISPLAY_ADD_FAILED' }).Count -eq 1) 'display error not recorded'
    Set-Stub $t @{ queryError = $null }
    Check ((Invoke-Rig 'display.ps1' @('add')).Code -eq 0) 'display add failed'
    Set-Stub $t @{ queryError = 'QueryDisplayConfig failed: 5' }
    Check ((Invoke-Rig 'display.ps1' @('remove')).Code -eq 1) 'remove did not fail with a failing query'
    Check (Test-Path -LiteralPath (Join-Path $t.Runs 'DISPLAY_ADDED')) 'DISPLAY_ADDED cleared although the removal could not be checked'
    $st = Invoke-Rig 'display.ps1' @('status')
    Check ($st.Code -eq 1 -and $st.Output -match 'query failed') 'status does not surface the error'
    Set-Stub $t @{ queryError = $null }
    Check ((Invoke-Rig 'display.ps1' @('remove')).Code -eq 0) 'remove failed after the query recovered'
    Check (-not (Get-Stub $t).active) 'display not removed'
}

Test-Case 'no-display-change-while-a-game-runs' {
    $t = New-TestEnv 'display-game-running' @{ EVR_RIG_GAME_PROCESSES = 'evr_rig_testapp' }
    Check ((Invoke-Rig 'display.ps1' @('add')).Code -eq 0) 'display add failed'
    $g = Start-Process -FilePath $t.Exe -PassThru
    Start-Sleep -Milliseconds 500
    $r = Invoke-Rig 'display.ps1' @('remove')
    Check ($r.Code -eq 1 -and $r.Output -match 'game process is running') "remove while a game runs: exit code $($r.Code)"
    Check ((Get-Stub $t).active -and (Test-Path -LiteralPath (Join-Path $t.Runs 'DISPLAY_ADDED'))) 'display changed while a game runs'
    $g.Kill(); [void]$g.WaitForExit(5000); Start-Sleep -Milliseconds 300
    Check ((Invoke-Rig 'display.ps1' @('remove')).Code -eq 0) 'remove failed after the game exited'
    $g = Start-Process -FilePath $t.Exe -PassThru
    Start-Sleep -Milliseconds 500
    Check ((Invoke-Rig 'display.ps1' @('add')).Code -eq 1) 'display added while a game runs'
    Check (-not (Get-Stub $t).active) 'display changed while a game runs'
    $g.Kill(); [void]$g.WaitForExit(5000)
}

Test-Case 'display-mode-requested-or-largest-and-window-size' {
    $t = New-TestEnv 'display-mode'
    Check ((Invoke-Rig 'display.ps1' @('add', '-Width', '1920', '-Height', '1080')).Code -eq 0) 'add 1920x1080 failed'
    Check ((Get-Stub $t).rect.width -eq 1920 -and (Get-Stub $t).rect.height -eq 1080) 'requested mode not set'
    Check ((Invoke-Rig 'display.ps1' @('status')).Output -match 'VDD mode:\s+1920x1080') 'status does not show the mode'
    Check ((Invoke-Rig 'display.ps1' @('remove')).Code -eq 0) 'remove failed'
    Check ((Invoke-Rig 'display.ps1' @('add', '-Width', '1234', '-Height', '567')).Code -eq 0) 'add with an unknown mode failed'
    Check ((Get-Stub $t).rect.width -eq 2560 -and (Get-Stub $t).rect.height -eq 1440) 'largest mode not used as the fallback'
    $s = Start-TestRun $t @('-Args', '+r_windowWidth 800 +r_windowHeight 600')
    Check ($s.Code -eq 0) "run exit code $($s.Code)"
    Check ($s.Run.args -match '\+r_windowWidth 800' -and $s.Run.args -notmatch 'r_windowWidth 2560') "caller's window size overridden: $($s.Run.args)"
    Check ((Invoke-Rig 'stop.ps1' @('-EndBlock')).Code -eq 0) 'stop failed'
}

Test-Case 'guard-runs-for-the-whole-run-and-ends-with-it' {
    $t = New-TestEnv 'guard'
    $s = Start-TestRun $t
    Check ($s.Code -eq 0) "run exit code $($s.Code): $($s.Output)"
    $guardPid = Get-RigProp (Get-RigProp $s.Run 'guard') 'pid' 0
    Check ($guardPid -gt 0) 'no guard recorded in run.json'
    Check ([bool](Get-Process -Id $guardPid -ErrorAction SilentlyContinue)) 'guard not alive while the run is'
    Start-Sleep -Seconds 3
    $log = [IO.File]::ReadAllText((Join-Path $s.Dir 'rig.log'))
    Check ($log -match 'guard started') 'guard start not logged'
    Check ((Invoke-Rig 'stop.ps1' @('-EndBlock')).Code -eq 0) 'stop failed'
    $deadline = (Get-Date).AddSeconds(15)
    while ((Get-Date) -lt $deadline -and (Get-Process -Id $guardPid -ErrorAction SilentlyContinue)) { Start-Sleep -Milliseconds 250 }
    Check (-not (Get-Process -Id $guardPid -ErrorAction SilentlyContinue)) 'guard still alive 15 s after the run ended'
    $summary = Read-Json (Join-Path $s.Dir 'guard.json')
    Check ($null -ne $summary) 'no guard.json'
    Check ($summary.reason -in @('game exited', 'run cleaned up')) "guard ended for: $($summary.reason)"
}

Test-Case 'owner-run-has-no-guard' {
    $t = New-TestEnv 'guard-owner'
    $s = Start-TestRun $t @('-Owner')
    Check ($s.Code -eq 0) "run exit code $($s.Code): $($s.Output)"
    Check ($null -eq (Get-RigProp $s.Run 'guard')) 'owner run started a guard'
    Check ((Invoke-Rig 'stop.ps1' @('-Run', (Split-Path -Leaf $s.Dir))).Code -eq 0) 'stop failed'
}

Test-Case 'paths-with-brackets' {
    $t = New-TestEnv 'brackets' @{} 'game[1]' 'runs[1]'
    $s = Start-TestRun $t
    Check ($s.Code -eq 0) "run exit code $($s.Code): $($s.Output)"
    Check ($s.Run.exe.path -like '*game`[1`]*') "exe path: $($s.Run.exe.path)"
    Simulate-GameWrites $t
    Check ((Invoke-Rig 'stop.ps1' @('-EndBlock')).Code -eq 0) 'stop failed'
    $now = Get-SettingsState $t
    foreach ($k in $t.Orig.Keys) { Check ($now[$k] -eq $t.Orig[$k]) "not restored: $k" }
}

function Write-PreDevBackup([string]$Source, [string]$Dest) {
    [void][IO.Directory]::CreateDirectory($Dest)
    $root = (Get-Item -LiteralPath $Source).FullName.TrimEnd('\')
    $lines = foreach ($f in @(Get-ChildItem -LiteralPath $root -Recurse -File)) {
        '{0}  {1}  {2}' -f (Get-FileHash -LiteralPath $f.FullName -Algorithm SHA256).Hash, $f.Length, $f.FullName.Substring($root.Length + 1)
    }
    [IO.File]::WriteAllLines((Join-Path $Dest 'SHA256SUMS.txt'), [string[]]$lines)
}

Test-Case 'first-run-baseline-is-the-pre-development-backup' {
    $backups = Join-Path $SuiteRoot 'pre-dev\backups'
    $t = New-TestEnv 'pre-dev' @{ EVR_RIG_BACKUPS_ROOT = $backups }
    Write-PreDevBackup $t.Saved (Join-Path $backups 'doom-eternal-savedgames\20260101-000000-pre-dev')
    Write-PreDevBackup $t.Remote (Join-Path $backups 'doom-eternal-remote\20260101-000000-pre-dev')
    $original = [IO.File]::ReadAllBytes($t.Cfg)
    Write-TestFile $t.Cfg 'changed since the pre-development backup'
    $r = Invoke-Rig 'run.ps1' @('-Exe', $t.Exe)
    Check ($r.Code -eq 2 -and $r.Output -match 'pre-development backup') "first run with a changed setting: exit code $($r.Code), expected 2"
    Check (@(Get-Runs $t).Count -eq 0) 'a run folder was created'
    [IO.File]::WriteAllBytes($t.Cfg, $original)
    $s = Start-TestRun $t
    Check ($s.Code -eq 0) "first run matching the backup: exit code $($s.Code)"
    Check ($s.Run.baseline.source -match 'pre-development') 'baseline source not recorded'
    Check ((Invoke-Rig 'stop.ps1' @('-EndBlock')).Code -eq 0) 'stop failed'
}

# --- Steam-Cloud files (T-115) ---------------------------------------------------------------------

Test-Case 'steam-cloud-record-is-parsed' {
    $t = New-TestEnv 'cloud-record'
    Write-TestCloudRecord $t @{ 'PROFILE/profile.bin' = ('0' * 40) }
    $cache = Get-RigSteamCloudCache $t.Remote
    Check ($cache.Present -and -not $cache.Error) "remotecache.vdf not read: $($cache.Error)"
    Check ($cache.Files.Count -eq 3) "files in the record: $($cache.Files.Count), expected 3"
    $save = $cache.Files['GAME-AUTOSAVE0\game_duration.dat']
    Check ($save -and $save.Size -eq 512 -and $save.Sha1 -eq (Get-RigSha1 $t.Save) -and $save.SyncState -eq '1') 'save slot entry not parsed'
    $locs = @(Get-SettingsLocations (Get-RigConfig))
    Check ((@(Get-RigCloudLocationNames $locs) -join ',') -eq 'steam-111') 'the Steam remote folder is not the only cloud location'
    $c = Test-RigCloudConsistency $locs
    Check ((@($c.Stale) -join ',') -eq 'steam-111\PROFILE\profile.bin') "stale: $(@($c.Stale) -join ', ')"
    Check (@($c.Untracked).Count -eq 0 -and @($c.Unreadable).Count -eq 0) 'untracked or unreadable reported for a complete record'
    Write-TestFile (Join-Path (Split-Path -Parent $t.Remote) 'remotecache.vdf') '"782330" { "PROFILE/profile.bin" { "size" "1" '
    Check (@((Test-RigCloudConsistency $locs).Unreadable).Count -eq 1) 'a truncated record is not reported as unreadable'
}

Test-Case 'cloud-files-keep-the-game-version-and-are-reported' {
    $t = New-TestEnv 'cloud-kept'
    Write-TestCloudRecord $t
    $s = Start-TestRun $t @('-Args', '+r_motionblur 1')
    Check ($s.Code -eq 0) "run exit code $($s.Code): $($s.Output)"
    Simulate-GameWrites $t
    # The game rewrites its profile through Steam (with a forced cvar's name in it), removes a save slot
    # file and adds another; Steam records them at the end of the app session.
    $bytes = New-Object byte[] 3000
    (New-Object Random 11).NextBytes($bytes)
    [IO.File]::WriteAllBytes($t.Profile, ($bytes + [Text.Encoding]::ASCII.GetBytes('r_motionblur 1')))
    $gameProfile = Get-RigSha256 $t.Profile
    Remove-Item -LiteralPath $t.Save -Force
    $added = Join-Path $t.Remote 'GAME-MANUAL2\game.details'
    Write-TestBytes $added 128 7
    Write-TestCloudRecord $t

    $st = Invoke-Rig 'stop.ps1'
    Check ($st.Code -eq 0) "stop.ps1 exit code $($st.Code)"
    Check (-not (Test-Path -LiteralPath (Join-Path $s.Dir 'CLEANUP_PENDING'))) 'marker not cleared'
    Check ((Get-RigSha256 $t.Profile) -eq $gameProfile) 'the game''s profile.bin was replaced behind Steam''s back'
    Check (-not (Test-Path -LiteralPath $t.Save)) 'a save the game removed was put back behind Steam''s back'
    Check (Test-Path -LiteralPath $added) 'a cloud file added during the run was removed'
    foreach ($k in @($t.Cfg, $t.Local, $t.Json)) { Check ((Get-RigSha256 $k) -eq $t.Orig[$k]) "local file not restored: $k" }
    $cj = Read-Json (Join-Path $s.Dir 'cleanup.json')
    Check ($cj.outcome -eq 'restored' -and $cj.verified -and $cj.cloudPolicy -eq 'keep-game-version') "cleanup: $($cj.outcome) $($cj.cloudPolicy)"
    Check (@($cj.restored | Where-Object { $_ -like 'steam-111\*' }).Count -eq 0) "cloud files restored: $(@($cj.restored) -join ', ')"
    Check (((@($cj.cloudKept) | Sort-Object) -join ',') -eq 'steam-111\GAME-AUTOSAVE0\game_duration.dat,steam-111\PROFILE\profile.bin') "cloudKept: $(@($cj.cloudKept) -join ', ')"
    $p = @($cj.cloudFiles | Where-Object { $_.key -eq 'steam-111\PROFILE\profile.bin' })[0]
    Check ($p -and $p.change -eq 'changed' -and $p.action -eq 'kept') 'profile.bin entry missing'
    Check ($p -and $p.before.sha256 -eq $t.Orig[$t.Profile] -and $p.before.size -eq 4096) 'before hash and size not recorded'
    Check ($p -and $p.after.sha256 -eq $gameProfile -and $p.live.sha256 -eq $gameProfile -and $p.live.size -eq 3014) 'after hash and size not recorded'
    Check ($p -and @($p.forcedCvarsFound) -contains 'r_motionblur') 'forced cvar in the kept profile not found'
    $r = @($cj.cloudFiles | Where-Object { $_.key -eq 'steam-111\GAME-AUTOSAVE0\game_duration.dat' })[0]
    Check ($r -and $r.change -eq 'removed' -and $null -eq $r.live -and $r.before.size -eq 512) 'removed save entry wrong'
    Check (@($cj.cloudFiles | Where-Object { $_.key -eq 'steam-111\GAME-MANUAL2\game.details' -and $_.change -eq 'added' }).Count -eq 1) 'added cloud file not reported'
    Check (@($cj.cloudWarnings | Where-Object { $_ -match 'r_motionblur' -and $_ -match 'T-092' }).Count -ge 1) 'no T-092 warning for the forced cvar'
    Check ($st.Output -match 'WARN\] Steam Cloud file changed during the run, kept as the game left it') 'kept cloud file not logged as a warning'
    Check ($st.Output -match 'WARN\] forced cvar name\(s\) r_motionblur') 'forced cvar warning not logged'
    Check (@($cj.cloudStale).Count -eq 0 -and $st.Output -notmatch 'is stale') 'Steam''s record reported stale although it matches'

    # The next run takes the kept versions as the baseline: no unexplained difference.
    $n = Start-TestRun $t
    Check ($n.Code -eq 0) "next run exit code $($n.Code): $($n.Output)"
    Check (@($n.Run.baseline.differences).Count -eq 0) "baseline differences: $(@($n.Run.baseline.differences) -join '; ')"
    Check ((Invoke-Rig 'stop.ps1' @('-EndBlock')).Code -eq 0) 'second stop failed'
    Check ((Get-RigSha256 $t.Profile) -eq $gameProfile) 'the kept profile changed on the second run'
}

Test-Case 'stale-steam-record-is-reported' {
    $t = New-TestEnv 'cloud-stale'
    Write-TestCloudRecord $t @{ 'PROFILE/profile.bin' = ('a' * 40) }
    $s = Start-TestRun $t
    Check ($s.Code -eq 0) "run exit code $($s.Code) (a stale record is reported, not refused)"
    Check (@($s.Run.warnings) -contains 'STEAM_CLOUD_RECORD_STALE') 'stale record not in run.json'
    Check ($s.Output -match 'record is stale for steam-111\\PROFILE\\profile.bin') 'stale record not reported by run.ps1'
    $st = Invoke-Rig 'stop.ps1' @('-EndBlock')
    Check ($st.Code -eq 0) "stop exit code $($st.Code)"
    Check ($st.Output -match "ERROR\] Steam's record is stale for steam-111\\PROFILE\\profile.bin") 'stale record not reported by the cleanup'
    $cj = Read-Json (Join-Path $s.Dir 'cleanup.json')
    Check (@($cj.cloudStale).Count -eq 1 -and $cj.cloudStale[0].steamSha1 -eq ('a' * 40) -and $cj.cloudStale[0].fileSha1 -eq (Get-RigSha1 $t.Profile)) 'stale details not recorded'
    Check ((Get-RigSha256 $t.Profile) -eq $t.Orig[$t.Profile]) 'profile.bin written without -RestoreCloudFiles'
}

Test-Case 'restore-cloud-files-opt-in-restores-and-resyncs' {
    $t = New-TestEnv 'cloud-restore'
    Write-TestCloudRecord $t
    $s = Start-TestRun $t
    Check ($s.Code -eq 0) "run exit code $($s.Code)"
    Simulate-GameWrites $t
    Write-TestBytes $t.Profile 5000 42
    Write-TestCloudRecord $t
    $helper = Set-TestResync $t -Accept
    $st = Invoke-Rig 'stop.ps1' @('-RestoreCloudFiles')
    foreach ($v in 'EVR_RIG_RESYNC_EXE', 'EVR_RIG_RESYNC_ARGS', 'EVR_RIG_RESYNC_KILL_MS') { Set-TestVar $v '' }
    Check ($st.Code -eq 0) "stop -RestoreCloudFiles exit code $($st.Code)"
    Check (-not (Test-Path -LiteralPath (Join-Path $s.Dir 'CLEANUP_PENDING'))) 'marker not cleared'
    $now = Get-SettingsState $t
    foreach ($k in $t.Orig.Keys) { Check ($now[$k] -eq $t.Orig[$k]) "not restored: $k" }
    $cj = Read-Json (Join-Path $s.Dir 'cleanup.json')
    Check ($cj.cloudPolicy -eq 'restore-and-resync' -and @($cj.restored) -contains 'steam-111\PROFILE\profile.bin') 'profile.bin not restored with the opt-in'
    Check (@($cj.cloudKept).Count -eq 0) 'cloud files kept with the opt-in'
    $rs = @(Read-Json (Join-Path $s.Dir 'resync.json'))
    Check ($rs.Count -eq 1 -and $rs[0].outcome -eq 'done' -and $rs[0].killAfterMs -eq 4000) 'resync launch not recorded'
    Check ((Get-TestResyncProcesses $helper).Count -eq 0) 'the resync launch was not killed'
    Check ($st.Output -match 'again \(touched by the resync launch\)') 'local file touched by the resync launch not restored again'
    Check (@((Test-RigCloudConsistency @(Get-SettingsLocations (Get-RigConfig))).Stale).Count -eq 0) 'Steam''s record does not match after the resync'
}

Test-Case 'restore-cloud-files-fails-while-steam-record-stays-stale' {
    $t = New-TestEnv 'cloud-restore-stale'
    Write-TestCloudRecord $t
    $s = Start-TestRun $t
    Check ($s.Code -eq 0) "run exit code $($s.Code)"
    Write-TestBytes $t.Profile 5000 43
    Write-TestCloudRecord $t
    $helper = Set-TestResync $t
    $st = Invoke-Rig 'stop.ps1' @('-RestoreCloudFiles')
    Check ($st.Code -eq 1) "stop exit code $($st.Code), expected 1"
    Check (Test-Path -LiteralPath (Join-Path $s.Dir 'CLEANUP_PENDING')) 'marker cleared with Steam''s record stale'
    $cj = Read-Json (Join-Path $s.Dir 'cleanup.json')
    Check ($cj.reason -match 'still describes other versions of: steam-111\\PROFILE\\profile.bin') "reason: $($cj.reason)"
    Check ((Get-RigSha256 $t.Profile) -eq $t.Orig[$t.Profile]) 'profile.bin not restored'
    Check ((Get-TestResyncProcesses $helper).Count -eq 0) 'the resync launch was not killed'
    # The retry resyncs again (nothing left to restore); Steam now accepts the file.
    $helper2 = Set-TestResync $t -Accept -Name 'resync-game-2.ps1'
    $c = Invoke-Rig 'cleanup.ps1' @('-Run', (Split-Path -Leaf $s.Dir), '-RestoreCloudFiles')
    foreach ($v in 'EVR_RIG_RESYNC_EXE', 'EVR_RIG_RESYNC_ARGS', 'EVR_RIG_RESYNC_KILL_MS') { Set-TestVar $v '' }
    Check ($c.Code -eq 0) "retry exit code $($c.Code)"
    Check (@(Read-Json (Join-Path $s.Dir 'resync.json')).Count -eq 2) 'the retry did not resync'
    Check ((Get-TestResyncProcesses $helper2).Count -eq 0) 'the second resync launch was not killed'
    $now = Get-SettingsState $t
    foreach ($k in $t.Orig.Keys) { Check ($now[$k] -eq $t.Orig[$k]) "not restored: $k" }
}

Test-Case 'test-seams-are-ignored-outside-test-mode' {
    $t = New-TestEnv 'seams'
    Set-TestVar 'EVR_RIG_TEST_ROOT' ''
    foreach ($kv in @{ EVR_RIG_STEAM_PROCESS = 'not-steam'; EVR_RIG_GAME_PROCESSES = 'not-the-game'; EVR_RIG_CLOSE_TIMEOUT_SEC = '1'
            EVR_RIG_ELEVATION = 'not-elevated'; EVR_RIG_TEST_ABORT_AT = 'marker'; EVR_RIG_LOCK_WAIT_SEC = '1' }.GetEnumerator()) { Set-TestVar $kv.Key $kv.Value }
    try {
        $cfg = Get-RigConfig
        Check (-not $cfg.TestMode) 'test mode without EVR_RIG_TEST_ROOT'
        Check ($cfg.SteamProcess -eq 'steam') "Steam process seam honoured in real mode: $($cfg.SteamProcess)"
        Check ((@($cfg.GameProcesses) -join ';') -eq 'DOOMEternalx64vk;DOOMSandBox64vk;idTechLauncher') 'game process seam honoured in real mode'
        Check ($cfg.CloseTimeoutSec -eq 20 -and $cfg.LockWaitSec -eq 120) 'timeout seams honoured in real mode'
        Check ($cfg.ElevationProbe -eq 'auto' -and -not $cfg.AbortAt -and -not $cfg.DisplayStub) 'elevation, kill point or display stub honoured in real mode'
    } finally {
        foreach ($item in @(Get-ChildItem Env: | Where-Object { $_.Name -like 'EVR_RIG_*' })) { Remove-Item -LiteralPath "Env:$($item.Name)" }
    }
}

# ---------------------------------------------------------------------------------------------------
# Summary

foreach ($item in @(Get-ChildItem Env: | Where-Object { $_.Name -like 'EVR_RIG_*' })) { Remove-Item -LiteralPath "Env:$($item.Name)" }
Stop-TestApps
$pass = @($script:Results | Where-Object { $_.Status -eq 'PASS' }).Count
$fail = @($script:Results | Where-Object { $_.Status -eq 'FAIL' }).Count
$skip = @($script:Results | Where-Object { $_.Status -eq 'SKIP' }).Count
Write-Host ''
Write-Host '==================== rig script tests ===================='
foreach ($r in $script:Results) { Write-Host ('{0,-4} {1,-58} {2,6:n1} s' -f $r.Status, $r.Name, $r.Seconds) }
Write-Host ('{0} passed, {1} failed, {2} skipped; suite folder {3}' -f $pass, $fail, $skip, $SuiteRoot)
if ($fail -gt 0 -or $script:Results.Count -eq 0) { exit 1 }
exit 0
