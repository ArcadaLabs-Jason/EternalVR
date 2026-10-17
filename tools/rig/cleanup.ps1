<#
.SYNOPSIS
Completes pending runs (idempotent): waits for Steam's post-exit sync (bounded), keeps the post-exit
settings in config-after\, restores every changed or removed local settings file (the Saved Games tree)
from config-before\ atomically (the file being replaced is kept in config-replaced\ first) and verifies
it by SHA-256. CLEANUP_PENDING is cleared only after that verification; otherwise it stays and the exit
code is 1. Run folders are never deleted.

Steam-Cloud files (userdata\<id>\782330\remote: PROFILE\profile.bin and the save slots) are not
restored: the game's version is kept, so Steam's record of them (remotecache.vdf) stays consistent, and
each change is logged with its before and after SHA-256 and size (cleanup.json cloudFiles). A forced
cvar name that appears in a kept cloud file is reported as a warning (T-092). -RestoreCloudFiles restores
them as well and then runs the resync launch (the game started directly and killed about 4 s in, before
its profile load), after which Steam's record must match the restored files (T-115).

-RestoreCloudFrom <run> puts the Steam-Cloud files back as they were when that finished run started
(its config-before\), after a series of runs that kept the game's versions: the pending runs are cleaned
first, then a run folder of its own (<stamp>-cloud-restore) gets CLEANUP_PENDING, a snapshot of the live
local files with the baseline's cloud files, the restore of every cloud file that differs and the resync
launch as with -RestoreCloudFiles, verified the same way. Refused while elevated, without Steam or while a
game process runs; a cloud file the baseline did not have is kept and listed.

-HoldCloudRestore [-HeldBy <who>] makes such a folder before a series, from the settings as they are now
(pending runs cleaned first; refused while a game process runs or a pending run stays), and leaves it
pending, held by the calling process: while that process lives, implicit cleanups (run.ps1's too) leave it
alone and do not count it against a start; it finishes when the holder names it (cleanup.ps1 -Run
<folder>), or with the next cleanup of anyone once the holder is gone, however it ended (the rig QA suite
holds one for its runs).

Never stops a process: a run whose game is still running is skipped (stop it with stop.ps1 -Run).
Without -Run, owner runs are skipped too. A file that changed after the run ended is not overwritten:
it is reported, and restored only with -AcknowledgeSettingsChange (after the owner agreed).

The virtual display is removed with -EndBlock (end of a work block), or when DISPLAY_ADDED was written by
an earlier session, and only while no run has a live process and no game process runs.
#>
param(
    [string]$Run,
    [switch]$All,
    [switch]$EndBlock,
    [switch]$AcknowledgeSettingsChange,
    [switch]$RestoreCloudFiles,
    [string]$RestoreCloudFrom,
    [switch]$HoldCloudRestore,
    [string]$HeldBy = 'cleanup.ps1 -HoldCloudRestore',
    [string]$SessionId
)

. (Join-Path $PSScriptRoot 'common.ps1')

try {
    $cfg = Get-RigConfig
} catch {
    Write-Host "cleanup.ps1: configuration error: $($_.Exception.Message)"
    exit 1
}
if (-not (Test-Path -LiteralPath $cfg.RunsRoot)) {
    # A held cloud restore is the first folder of its series: it makes the runs root.
    if (-not $HoldCloudRestore) { Write-Host 'cleanup.ps1: no runs root yet; nothing to do'; exit 0 }
    New-RigDirectory $cfg $cfg.RunsRoot
}
Set-RigLogFiles @(Join-Path $cfg.RunsRoot 'rig.log')
if (-not (Enter-RigLock $cfg 'cleanup.ps1')) { Exit-RigScript 1 }
$session = Get-RigSession $cfg $SessionId
if ($HoldCloudRestore) {
    if ($Run -or $All -or $RestoreCloudFiles -or $RestoreCloudFrom) { Write-RigLog 'cleanup.ps1: -HoldCloudRestore takes no other cleanup switch' 'ERROR'; Exit-RigScript 1 }
    $games = @(Get-RigGameProcesses $cfg)
    if ($games.Count -gt 0) {
        Write-RigLog ("REFUSED: cloud restore not held: a game process is running ({0})" -f (($games | ForEach-Object { "$($_.ProcessName) $($_.Id)" }) -join ', ')) 'ERROR'
        Exit-RigScript 1
    }
    # Pending runs first (an earlier series' restore among them), so the snapshot is the settings without them.
    $clean = Invoke-RigCleanup $cfg -Session $session
    if ($clean.Failed -or $clean.Pending.Count -gt 0) {
        foreach ($p in $clean.Pending) { Write-RigLog "pending: $($p.Reason)" 'ERROR' }
        Write-RigLog 'REFUSED: cloud restore not held: a pending run could not be cleaned up (cleanup.ps1, then again)' 'ERROR'
        Exit-RigScript 1
    }
    # Another live hold (a second suite while one runs): the files are mid-series there, not a baseline.
    foreach ($dir in @(Get-RigRunDirs $cfg | Where-Object { Test-RigPending $_ })) {
        $other = $null; try { $other = Get-RigProp (Read-RigJson (Join-Path $dir 'CLEANUP_PENDING')) 'heldBy' } catch { }
        if ($other -and (Test-RigProcessAlive $other.pid $other.startFileTimeUtc)) {
            Write-RigLog "REFUSED: cloud restore not held: $(Split-Path -Leaf $dir) is held by $($other.by) (pid $($other.pid)) and its runs are not over" 'ERROR'
            Exit-RigScript 1
        }
    }
    try { $heldDir = New-RigCloudRestoreRun $cfg '' $session -HeldBy $HeldBy } catch {
        Write-RigLog "cloud restore not held: $($_.Exception.Message)" 'ERROR'
        Exit-RigScript 1
    }
    Write-RigLog "cloud restore held: $heldDir (finish it with cleanup.ps1 -Run $(Split-Path -Leaf $heldDir))"
    Exit-RigScript 0
}
if ($RestoreCloudFrom) {
    if ($Run -or $All -or $RestoreCloudFiles) { Write-RigLog 'cleanup.ps1: -RestoreCloudFrom takes no -Run, -All or -RestoreCloudFiles' 'ERROR'; Exit-RigScript 1 }
    try { $baseline = Resolve-RigRunDir $cfg $RestoreCloudFrom } catch { Write-RigLog $_.Exception.Message 'ERROR'; Exit-RigScript 1 }
    $refusal = $null
    if (Test-RigElevated $cfg) { $refusal = 'running elevated' }
    elseif (-not (Test-RigSteamRunning $cfg)) { $refusal = "$($cfg.SteamProcess).exe is not running (Steam must see the resync launch)" }
    else {
        $games = @(Get-RigGameProcesses $cfg)
        if ($games.Count -gt 0) { $refusal = 'a game process is running ({0})' -f (($games | ForEach-Object { "$($_.ProcessName) $($_.Id)" }) -join ', ') }
    }
    if ($refusal) { Write-RigLog "REFUSED: cloud restore from $(Split-Path -Leaf $baseline): $refusal" 'ERROR'; Exit-RigScript 1 }
    # Pending runs first, as run.ps1 cleans them before a start.
    $clean = Invoke-RigCleanup $cfg -Session $session
    if ($clean.Failed -or $clean.Pending.Count -gt 0) {
        foreach ($p in $clean.Pending) { Write-RigLog "pending: $($p.Reason)" 'ERROR' }
        Write-RigLog 'REFUSED: a pending run could not be cleaned up before the cloud restore' 'ERROR'
        Exit-RigScript 1
    }
    try { $restoreDir = New-RigCloudRestoreRun $cfg $baseline $session } catch {
        Write-RigLog "cloud restore from $(Split-Path -Leaf $baseline) not started: $($_.Exception.Message)" 'ERROR'
        Exit-RigScript 1
    }
    $r = Invoke-RigCleanup $cfg -RunDirs @($restoreDir) -Explicit -Session $session -Acknowledge:$AcknowledgeSettingsChange -RestoreCloud
    if ($r.Failed) { Exit-RigScript 1 }
    Write-RigLog "cloud restore done: the Steam-Cloud files are as $(Split-Path -Leaf $baseline) started ($(Split-Path -Leaf $restoreDir))"
    Exit-RigScript 0
}
$dirs = $null
$explicit = $false
if ($Run -and -not $All) {
    try { $dirs = @(Resolve-RigRunDir $cfg $Run) } catch { Write-RigLog $_.Exception.Message 'ERROR'; Exit-RigScript 1 }
    $explicit = $true
}
$r = Invoke-RigCleanup $cfg -RunDirs $dirs -Explicit:$explicit -EndBlock:$EndBlock -Session $session -Acknowledge:$AcknowledgeSettingsChange -RestoreCloud:$RestoreCloudFiles
if ($r.Failed) { Exit-RigScript 1 }
if ($r.Pending.Count -gt 0) { Write-RigLog ("cleanup complete; {0} run(s) left pending (see above)" -f $r.Pending.Count) } else { Write-RigLog 'cleanup complete' }
Exit-RigScript 0
