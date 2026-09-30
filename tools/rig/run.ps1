<#
.SYNOPSIS
Starts one DOOM Eternal run (or the test application) into a new run folder and returns once it is up.

.DESCRIPTION
Refuses when elevated, when Steam is not running, when a game process is already running, when a pending
run cannot be cleaned up (a live or owner run is never cleaned implicitly: stop it with stop.ps1 -Run),
or, with EVR_RIG_OWNER_SETTINGS_GUARD=1, when the settings differ from the last verified restore (or,
before the first one, from the pre-development backups) without an explanation; with the guard off (the
default outside the tests) a difference is logged and adopted. Otherwise: writes CLEANUP_PENDING, snapshots every
settings location into config-before\, makes sure the virtual display is part of the desktop (once per
work block, 2560x1440 by default), starts the exe windowed (+r_fullscreen 0, window size = the virtual
display's) with SteamAppId=782330, mutes the game's own audio session (its prior mute state is recorded
and put back on stop), watches it (hand-off detection), moves its window onto the virtual display without
activating it, gives the foreground back to the window that had it before the launch, and returns.
stop.ps1 ends the run.

Exit codes: 0 running, 1 error, 2 refused, 3 the game exited early (run cleaned up), 4 hand-off (the
started process exited and another game process took over; it is tracked, but our environment may be
lost).

.PARAMETER Exe
retail | sandbox | lab | <path to an exe>.

.PARAMETER Args
Extra game arguments, appended to the command line as written. From PowerShell:
& .\run.ps1 -Exe retail -Args @('+logFile 2', '+com_skipIntroVideo 1'). From another shell with -File,
-Args "+logFile 2","+com_skipIntroVideo 1" arrives as one comma-joined string; it is split again at every
comma followed by + or -.

.PARAMETER GameEnv
NAME=VALUE pairs for the game's environment (same comma rule).

.PARAMETER Owner
A run with the owner at the controls: no virtual display, no mute, no forced window mode, the foreground
left alone; the same marker, snapshot, verified restore and cleanup (cleaned only by name).

.PARAMETER NoMute
Do not mute the game's audio session.

.PARAMETER KeepFocus
Leave the game in the foreground. The game pauses whenever its window loses focus, so runs that must keep
playing unattended (level entry, cinematic skips, headset sessions) use this.
#>
param(
    [string]$Exe,
    [Alias('Args')][string[]]$GameArgs = @(),
    [string]$Label,
    [switch]$NoDisplay,
    [switch]$NoMute,
    [switch]$SessionMute,
    [switch]$Owner,
    [switch]$KeepFocus,
    [int]$DisplayWidth = 2560,
    [int]$DisplayHeight = 1440,
    [string[]]$GameEnv = @(),
    [string]$SessionId,
    [switch]$AcknowledgeSettingsChange,
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$MoreArgs = @()
)

. (Join-Path $PSScriptRoot 'common.ps1')

function Exit-Rig([int]$Code, [string]$Message, [string]$Level = 'ERROR') {
    if ($Message) { Write-RigLog $Message $Level }
    Exit-RigScript $Code
}

try {
    $cfg = Get-RigConfig
} catch {
    Write-Host "run.ps1: configuration error: $($_.Exception.Message)"
    exit 1
}
New-RigDirectory $cfg $cfg.RunsRoot
Set-RigLogFiles @(Join-Path $cfg.RunsRoot 'rig.log')

# --- Parameters ----------------------------------------------------------------------------------
if (-not $Exe) { Exit-Rig 1 'run.ps1: -Exe retail|sandbox|lab|<path> is required' }
# Further game arguments after -Args ("+a 1" "+b 2") arrive here; a stray -Name is a mistyped parameter.
$bad = @($MoreArgs | Where-Object { $_ -match '^-[A-Za-z]' })
if ($bad.Count -gt 0) { Exit-Rig 1 "run.ps1: unknown parameter(s): $($bad -join ' ')" }
$GameArgs = @(Split-RigList (@($GameArgs) + @($MoreArgs)) '[+-]')
$GameEnv = @(Split-RigList $GameEnv '[A-Za-z_][A-Za-z0-9_]*=')
$muteSession = -not $NoMute
if ($Owner) {
    # The owner at the controls: his display, his sound, his normal window mode.
    $NoDisplay = [switch]$true
    $muteSession = $false
}
$kind = $Exe.ToLowerInvariant()
$workDir = $cfg.GameRoot
switch ($kind) {
    'retail' { $exePath = Join-Path $cfg.GameRoot 'DOOMEternalx64vk.exe' }
    'sandbox' { $exePath = Join-Path $cfg.GameRoot 'doomSandBox\DOOMSandBox64vk.exe' }
    'lab' { $exePath = Join-Path $cfg.LabRoot 'DOOMEternalx64vk.exe'; $workDir = $cfg.LabRoot }
    default { $kind = 'path'; $exePath = [IO.Path]::GetFullPath($Exe) }
}
if (-not (Test-Path -LiteralPath $exePath -PathType Leaf)) { Exit-Rig 1 "exe not found: $exePath" }
if (-not (Test-Path -LiteralPath $workDir -PathType Container)) { Exit-Rig 1 "working directory not found: $workDir" }
$exeName = [IO.Path]::GetFileNameWithoutExtension($exePath)
if (-not $Label) {
    if ($kind -eq 'path') { $Label = $exeName } else { $Label = $kind }
}
$Label = ($Label -replace '[^A-Za-z0-9_.-]', '_')
$envPairs = [ordered]@{ SteamAppId = '782330' }
foreach ($pair in $GameEnv) {
    $i = $pair.IndexOf('=')
    if ($i -lt 1) { Exit-Rig 1 "-GameEnv entries are NAME=VALUE: $pair" }
    $envPairs[$pair.Substring(0, $i)] = $pair.Substring($i + 1)
}

# --- Refusals (nothing has been changed yet) ----------------------------------------------------
if (Test-RigElevated $cfg) {
    Exit-Rig 2 'REFUSED: running elevated. The rig scripts and the game never run as administrator (the Vulkan loader ignores the layer path variables in an elevated process).'
}
if (-not (Test-RigSteamRunning $cfg)) {
    Exit-Rig 2 "REFUSED: $($cfg.SteamProcess).exe is not running. Start Steam first, so the game never starts a Steam client that inherits our environment."
}
if (-not (Enter-RigLock $cfg 'run.ps1')) { Exit-Rig 2 'REFUSED: another rig script is busy (see above).' }
$games = @(Get-RigGameProcesses $cfg)
if ($games.Count -gt 0) {
    $hint = ''
    $liveRuns = @(Get-RigRunDirs $cfg | Where-Object { (Test-RigPending $_) -and @(Get-RigRunLiveProcesses $_).Count -gt 0 })
    if ($liveRuns.Count -gt 0) { $hint = " It belongs to run $(Split-Path -Leaf $liveRuns[-1]): stop it with stop.ps1 -Run $(Split-Path -Leaf $liveRuns[-1])." }
    else { $hint = ' The owner may be playing.' }
    Exit-Rig 2 ("REFUSED: a game process is already running ({0}).{1}" -f (($games | ForEach-Object { "$($_.ProcessName) $($_.Id)" }) -join ', '), $hint)
}
$session = Get-RigSession $cfg $SessionId
$pending = @(Get-RigRunDirs $cfg | Where-Object { Test-RigPending $_ })
if ($pending.Count -gt 0) {
    Write-RigLog ("{0} pending run(s) from earlier; cleaning up first" -f $pending.Count)
}
# Completes pending runs whose processes are gone (never stops a live run, never touches an owner run);
# removes a display only when an earlier session left it.
$clean = Invoke-RigCleanup $cfg -Session $session
if ($clean.Failed -or $clean.Pending.Count -gt 0) {
    foreach ($p in $clean.Pending) { Write-RigLog "pending: $($p.Reason)" 'ERROR' }
    Exit-Rig 2 'REFUSED: a pending run could not be cleaned up (see above, its cleanup.json and rig.log). Report it to the owner if a restore failed.'
}

# Settings must match the last verified restore (before the first one: the pre-development backups);
# an unexplained difference is reported, never adopted.
$baseline = $null
$lastRestored = @(Get-RigRunDirs $cfg | Where-Object {
        $c = Read-RigJson (Join-Path $_ 'cleanup.json')
        $c -and (Get-RigProp $c 'outcome') -eq 'restored' -and (Test-Path -LiteralPath (Join-Path $_ 'config-before\SHA256SUMS'))
    }) | Select-Object -Last 1
$bName = $null; $bLocations = $null; $expected = $null
if ($lastRestored) {
    # Expected state: config-before, plus the files the run added (kept, never deleted) as they were
    # after it exited.
    $bName = "the last verified restore ($(Split-Path -Leaf $lastRestored))"
    $bDir = Join-Path $lastRestored 'config-before'
    $bLocations = Get-SnapshotLocations $bDir
    $expected = Read-RigManifest (Join-Path $bDir 'SHA256SUMS')
    $lastCleanup = Read-RigJson (Join-Path $lastRestored 'cleanup.json')
    $afterSums = Join-Path $lastRestored 'config-after\SHA256SUMS'
    if (Test-Path -LiteralPath $afterSums) {
        $afterManifest = Read-RigManifest $afterSums
        foreach ($key in @($lastCleanup.added)) {
            if ($key -and $afterManifest.ContainsKey($key)) { $expected[$key] = $afterManifest[$key] }
        }
    }
    # Steam-Cloud files that cleanup kept as the game left them (T-115): as they were at that cleanup.
    foreach ($e in @(Get-RigProp $lastCleanup 'cloudFiles' @())) {
        if (-not $e -or (Get-RigProp $e 'action') -ne 'kept' -or (Get-RigProp $e 'change') -eq 'added') { continue }
        $live = Get-RigProp $e 'live'
        if ($live) { $expected[$e.key] = $live.sha256 } else { $expected.Remove($e.key) }
    }
} else {
    $pre = Get-RigPreDevBaseline $cfg
    if ($pre) { $bName = "the pre-development backup ($($pre.Name))"; $bLocations = $pre.Locations; $expected = $pre.Manifest }
}
if ($bLocations) {
    $diff = Compare-SettingsWithManifest $bLocations $expected
    $newLocations = @()
    if ($lastRestored) {
        $newLocations = @(Get-SettingsLocations $cfg | Where-Object {
                $n = $_.Name; -not @($bLocations | Where-Object { $_.name -eq $n -and $_.existed }).Count -and @(Get-TreeFiles $_.Path).Count -gt 0
            } | ForEach-Object { "$($_.Name) (new location)" })
    }
    $differences = @($diff.Changed | ForEach-Object { "changed: $_" }) + @($diff.Removed | ForEach-Object { "removed: $_" }) +
        @($diff.Added | ForEach-Object { "added: $_" }) + @($diff.Unreadable | ForEach-Object { "unreadable: $_" }) + $newLocations
    $baseline = [ordered]@{ source = $bName; differences = $differences; acknowledged = $false }
    if ($differences.Count -gt 0) {
        if (-not $AcknowledgeSettingsChange -and $cfg.OwnerSettingsGuard) {
            foreach ($d in $differences) { Write-RigLog "settings differ from $($bName): $d" 'ERROR' }
            Exit-Rig 2 'REFUSED: the settings changed since the baseline and no pending run explains it. Report this to the owner; rerun with -AcknowledgeSettingsChange only once he has confirmed the change is his.'
        }
        $baseline.acknowledged = $true
        $how = $(if ($AcknowledgeSettingsChange) { 'acknowledged' } else { 'adopted (EVR_RIG_OWNER_SETTINGS_GUARD is off)' })
        Write-RigLog ("settings differences {0}: {1}" -f $how, ($differences -join '; ')) 'WARN'
    }
}

# Steam's record of the cloud files must match them, or the game may reset its profile at this launch
# (T-115). Reported, not refused: the rig wrote no cloud file.
$cloudCheck = Test-RigCloudConsistency @(Get-SettingsLocations $cfg)
foreach ($d in $cloudCheck.Details) {
    Write-RigLog ("Steam's record is stale for {0} (record {1} bytes, file {2} bytes): the game may reset its profile at this launch" -f $d.key, $d.steamSize, $d.fileSize) 'WARN'
}

# --- 1. Marker first -----------------------------------------------------------------------------
$stamp = (Get-Date).ToString('yyyyMMdd-HHmmss')
$runDir = Join-Path $cfg.RunsRoot "$stamp-$Label"
for ($n = 2; Test-Path -LiteralPath $runDir; $n++) { $runDir = Join-Path $cfg.RunsRoot "$stamp-$Label-$n" }
New-RigDirectory $cfg $runDir
Write-RigJson $cfg (Join-Path $runDir 'CLEANUP_PENDING') ([ordered]@{ session = $session; created = (Get-Date).ToString('o'); pid = $PID
        startFileTimeUtc = (Get-RigProcessStartFileTime (Get-Process -Id $PID)); owner = [bool]$Owner })
Set-RigLogFiles @((Join-Path $cfg.RunsRoot 'rig.log'), (Join-Path $runDir 'rig.log'))
Write-RigLog "run folder $runDir (session $session)"
Invoke-RigAbortPoint $cfg 'marker'

$run = [ordered]@{
    schema = 1; session = $session; label = $Label; created = (Get-Date).ToString('o'); phase = 'created'; owner = [bool]$Owner
    exe = [ordered]@{ kind = $kind; path = $exePath; sha256 = $null; workingDirectory = $workDir }
    args = $null; env = $envPairs; elevated = $false; hags = (Get-RigHags)
    display = [ordered]@{ requested = (-not $NoDisplay); mode = 'none'; note = ''; rect = $null }
    baseline = $baseline; settingsLocations = @(); launchTime = $null; launchFileTimeUtc = $null
    processes = @(); windows = @(); foreground = $null; sessionMute = $null; muteMayPersist = $false; warnings = @(); errors = @()
}
if ($cloudCheck.Stale.Count -gt 0) { $run.warnings += 'STEAM_CLOUD_RECORD_STALE'; $run.cloudStale = @($cloudCheck.Details) }
$runFile = Join-Path $runDir 'run.json'
function Save-Run { Write-RigJson $cfg $runFile $run }
Save-Run

try {
    # --- 2. Snapshot every settings location (saves included) ----------------------------------
    $run.exe.sha256 = Get-RigSha256 $exePath
    $snap = New-SettingsSnapshot $cfg $runDir 'config-before' @(Get-SettingsLocations $cfg)
    $run.settingsLocations = @($snap.Locations)
    $run.phase = 'snapshot'
    Save-Run
    Write-RigLog ("config-before: {0} file(s) from {1} location(s)" -f $snap.Manifest.Count, @($snap.Locations | Where-Object { $_.existed }).Count)
    Invoke-RigAbortPoint $cfg 'snapshot'

    # --- 3. Virtual display for the work block -------------------------------------------------
    $rect = $null
    if (-not $NoDisplay) {
        $d = Add-RigDisplay $cfg $session (Split-Path -Leaf $runDir) $DisplayWidth $DisplayHeight
        if ($d.Ok) {
            $rect = $d.Rect
            $run.display.mode = 'virtual'; $run.display.rect = $rect; $run.display.note = $d.Note
        } elseif ($d.Unavailable) {
            $run.display.note = "$($d.Note); running without it (-NoDisplay fallback)"
            $run.warnings += 'NO_VIRTUAL_DISPLAY'
            Write-RigLog "WARNING: $($run.display.note)" 'WARN'
        } else {
            $run.errors += "DISPLAY_ADD_FAILED: $($d.Note)"
            $run.phase = 'failed'
            Save-Run
            [void](Invoke-RigRunCleanup $cfg $runDir -Explicit -AllowStop)
            Exit-Rig 1 $d.Note
        }
    } elseif ($Owner) {
        $run.display.note = '-Owner: the owner''s own display'
    } else {
        $run.display.note = '-NoDisplay'
    }
    $run.phase = 'display'
    Save-Run
    Invoke-RigAbortPoint $cfg 'display'

    # --- 4. Start the exe ----------------------------------------------------------------------
    $argList = @()
    if (-not $Owner) { $argList += '+r_fullscreen 0' }
    if ($rect -and -not @($GameArgs | Where-Object { $_ -match 'r_windowWidth|r_windowHeight' }).Count) {
        $argList += ('+r_windowWidth {0} +r_windowHeight {1}' -f $rect.width, $rect.height)
    }
    $argList += @($GameArgs | Where-Object { $_ })
    $argString = $argList -join ' '
    $run.args = $argString

    $before = @{}
    foreach ($p in @(Get-Process -ErrorAction SilentlyContinue)) { $before[$p.Id] = $true }
    Initialize-RigNative
    $fgBefore = [EvrRig.Windows]::Foreground()
    $run.foreground = [ordered]@{ before = $fgBefore; beforePid = [EvrRig.Windows]::ForegroundPid(); handedBack = 0 }
    $launch = Get-Date
    $run.launchTime = $launch.ToString('o')
    $run.launchFileTimeUtc = $launch.ToFileTimeUtc()
    $run.phase = 'launching'
    Save-Run
    Invoke-RigAbortPoint $cfg 'launch'
    # The game's variables are set on this process only for the start and restored right after it,
    # so a caller that runs several games from one PowerShell process does not leak one run's
    # ETERNALVR_* values into the next.
    $envBefore = @{}
    foreach ($key in $envPairs.Keys) {
        $envBefore[$key] = [Environment]::GetEnvironmentVariable($key, 'Process')
        [Environment]::SetEnvironmentVariable($key, $envPairs[$key], 'Process')
    }
    # ShellExecute start: the game inherits no handles of this process (callers' pipes close on our
    # exit), and the path is taken literally.
    $psi = New-Object Diagnostics.ProcessStartInfo
    $psi.FileName = $exePath
    $psi.WorkingDirectory = $workDir
    $psi.Arguments = $argString
    $psi.UseShellExecute = $true
    try {
        $proc = [Diagnostics.Process]::Start($psi)
    } finally {
        foreach ($key in $envBefore.Keys) { [Environment]::SetEnvironmentVariable($key, $envBefore[$key], 'Process') }
    }
    $run.processes += [ordered]@{ pid = $proc.Id; name = $exeName; path = $exePath; startFileTimeUtc = (Get-RigProcessStartFileTime $proc); role = 'started' }
    $run.phase = 'started'
    Save-Run
    Write-RigLog "started pid $($proc.Id): $exePath $argString"
    Invoke-RigAbortPoint $cfg 'started'

    # --- 5. Watch: hand-off, session mute, window placement, foreground ------------------------
    $tracked = @($proc)
    $exitedAt = $null
    $handoff = $false
    $moved = @{}
    $muteDone = -not $muteSession
    $leakedMutes = @()
    if ($muteSession) {
        $run.sessionMute = [ordered]@{ requested = $true; applied = $false; priorMuted = $null; restored = $false; pids = @(); leakFrom = $null }
        $leakedMutes = @(Get-RigRunDirs $cfg | Where-Object {
                $r = Read-RigJson (Join-Path $_ 'run.json')
                $r -and (Get-RigProp $r 'muteMayPersist' $false) -and $r.exe.path -eq $exePath
            })
    }
    $watchEnd = $launch.AddSeconds($cfg.WatchSec)
    $windowEnd = $launch.AddSeconds([Math]::Max($cfg.WatchSec, $cfg.WindowWaitSec))
    $muteEnd = $launch.AddSeconds([Math]::Max($cfg.WatchSec, $cfg.MuteWaitSec))
    while ($true) {
        $now = Get-Date
        $alive = @($tracked | Where-Object { -not $_.HasExited })
        if ($alive.Count -eq 0) {
            if (-not $exitedAt) { $exitedAt = $now; Write-RigLog "pid $($proc.Id) exited after $([int]($now - $launch).TotalSeconds) s; looking for a hand-off" 'WARN' }
            $names = @($cfg.GameProcesses) + @($exeName) | Select-Object -Unique
            $new = @(Get-Process -Name $names -ErrorAction SilentlyContinue | Where-Object { -not $before.ContainsKey($_.Id) } | Where-Object { $id = $_.Id; -not @($tracked | Where-Object { $_.Id -eq $id }).Count })
            if ($new.Count -gt 0) {
                foreach ($p in $new) {
                    $path = $null; try { $path = $p.Path } catch { }
                    $run.processes += [ordered]@{ pid = $p.Id; name = $p.ProcessName; path = $path; startFileTimeUtc = (Get-RigProcessStartFileTime $p); role = 'handoff' }
                    $tracked += $p
                    Write-RigLog "HANDOFF: pid $($proc.Id) exited and $($p.ProcessName) pid $($p.Id) appeared; tracking it (our environment may be lost)" 'WARN'
                }
                $handoff = $true
                $exitedAt = $null
                $run.warnings += 'HANDOFF'
                Save-Run
                continue
            }
            if ($now -ge $watchEnd -and ($now - $exitedAt).TotalSeconds -ge 5) {
                $code = $null; try { $code = $proc.ExitCode } catch { }
                $run.errors += "EXITED_EARLY: pid $($proc.Id) exited with code $code within $($cfg.WatchSec) s and no game process took over"
                $run.phase = 'exited-early'
                Save-Run
                Write-RigLog $run.errors[-1] 'ERROR'
                [void](Invoke-RigRunCleanup $cfg $runDir -Explicit -AllowStop)
                Exit-RigScript 3
            }
        } else {
            $pids = [int[]]@($alive | ForEach-Object { $_.Id })
            if (-not $muteDone) {
                try {
                    $sessions = @([EvrRig.Audio]::Query($pids))
                    if ($sessions.Count -gt 0) {
                        # The prior state is recorded before the change, so every stop path can put it back.
                        $run.sessionMute.priorMuted = [bool](@($sessions | Where-Object { $_.Muted }).Count)
                        # A mute leaked by an earlier killed run (Windows remembers it per exe) is ours,
                        # not the owner's: the state to put back is unmuted.
                        if ($run.sessionMute.priorMuted) {
                            foreach ($leak in $leakedMutes) {
                                $run.sessionMute.priorMuted = $false
                                $run.sessionMute.leakFrom = Split-Path -Leaf $leak
                                Update-RigJson $cfg (Join-Path $leak 'run.json') @{ muteMayPersist = $false; muteCleared = (Split-Path -Leaf $runDir) }
                                Write-RigLog "the mute found is the one left by run $(Split-Path -Leaf $leak); it is put back to unmuted on stop"
                            }
                        }
                        $run.sessionMute.applied = $true; $run.sessionMute.pids = $pids
                        Save-Run
                        $n = [EvrRig.Audio]::SetMute($pids, $true)
                        Write-RigLog "audio session muted ($n session(s), prior muted=$($run.sessionMute.priorMuted))"
                        $muteDone = $true
                    }
                } catch {
                    Write-RigLog "audio session mute failed: $($_.Exception.Message)" 'WARN'
                    $muteDone = $true
                }
            }
            if ($rect) {
                foreach ($w in @([EvrRig.Windows]::TopLevel($pids) | Where-Object { $_.Visible })) {
                    $inside = ($w.X -ge $rect.x -and $w.Y -ge $rect.y -and $w.X -lt ($rect.x + $rect.width) -and $w.Y -lt ($rect.y + $rect.height))
                    if (-not $inside) {
                        [void][EvrRig.Windows]::Move($w.Handle, [int]$rect.x, [int]$rect.y)
                        Write-RigLog "moved window '$($w.Title)' of pid $($w.Pid) to the virtual display (not activated)"
                    }
                    $moved[[string]$w.Handle] = $true
                }
            }
            # The game must not keep the owner's keyboard and mouse: give the foreground back.
            if (-not $Owner -and -not $KeepFocus -and $fgBefore -ne 0 -and $run.foreground.handedBack -lt 10 -and $pids -contains [EvrRig.Windows]::ForegroundPid()) {
                $ok = [EvrRig.Windows]::RestoreForeground($fgBefore)
                $run.foreground.handedBack++
                Write-RigLog "the game took the foreground; handed it back to the previous window (ok=$ok)"
            }
        }
        $windowsDone = (-not $rect) -or ($moved.Count -gt 0)
        if ($now -ge $watchEnd -and $alive.Count -gt 0 -and
            ($windowsDone -or $now -ge $windowEnd) -and ($muteDone -or $now -ge $muteEnd)) { break }
        Start-Sleep -Milliseconds 250
    }

    $pids = [int[]]@($tracked | Where-Object { -not $_.HasExited } | ForEach-Object { $_.Id })
    $run.windows = @([EvrRig.Windows]::TopLevel($pids) | Where-Object { $_.Visible } | ForEach-Object {
            [ordered]@{ pid = $_.Pid; title = $_.Title; className = $_.ClassName; x = $_.X; y = $_.Y; width = $_.Width; height = $_.Height }
        })
    if ($rect -and $moved.Count -eq 0) { $run.warnings += 'WINDOW_NOT_PLACED'; Write-RigLog 'WARNING: no game window appeared to place on the virtual display' 'WARN' }
    if ($muteSession -and -not $run.sessionMute.applied) { $run.warnings += 'SESSION_MUTE_NOT_APPLIED'; Write-RigLog 'WARNING: no audio session appeared to mute; the game may be audible' 'WARN' }
    # The game's window appears late and it can take the foreground again at any time: guard.ps1 keeps
    # the window on the virtual display and hands the foreground back for the rest of the run.
    if (-not $Owner -and ($rect -or -not $KeepFocus)) {
        $guardArgs = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', ('"{0}"' -f (Join-Path $PSScriptRoot 'guard.ps1')),
            '-RunDir', ('"{0}"' -f $runDir), '-Foreground', $fgBefore)
        if ($rect) { $guardArgs += @('-Rect', ('{0},{1},{2},{3}' -f $rect.x, $rect.y, $rect.width, $rect.height)) }
        if ($KeepFocus) { $guardArgs += '-NoFocus' }
        try {
            $guard = Start-Process -FilePath 'powershell.exe' -ArgumentList $guardArgs -WindowStyle Hidden -PassThru
            $run.guard = [ordered]@{ pid = $guard.Id; startFileTimeUtc = (Get-RigProcessStartFileTime $guard) }
        } catch {
            $run.warnings += 'GUARD_NOT_STARTED'
            Write-RigLog "WARNING: the window and focus guard did not start: $($_.Exception.Message)" 'WARN'
        }
    }
    $run.phase = 'running'
    Save-Run
    if ($handoff) {
        Write-RigLog "run started with a HANDOFF; stop it with stop.ps1 -Run $(Split-Path -Leaf $runDir)" 'WARN'
        Exit-RigScript 4
    }
    Write-RigLog "running; stop with: stop.ps1 -Run $(Split-Path -Leaf $runDir)"
    Exit-RigScript 0
} catch {
    $run.errors += "ERROR: $($_.Exception.Message)"
    try { Save-Run } catch { }
    Write-RigLog "run.ps1 failed: $($_.Exception.Message); CLEANUP_PENDING stays until cleanup succeeds" 'ERROR'
    Exit-RigScript 1
}
