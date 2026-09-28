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
    [string]$SessionId
)

. (Join-Path $PSScriptRoot 'common.ps1')

try {
    $cfg = Get-RigConfig
} catch {
    Write-Host "cleanup.ps1: configuration error: $($_.Exception.Message)"
    exit 1
}
if (-not (Test-Path -LiteralPath $cfg.RunsRoot)) { Write-Host 'cleanup.ps1: no runs root yet; nothing to do'; exit 0 }
Set-RigLogFiles @(Join-Path $cfg.RunsRoot 'rig.log')
if (-not (Enter-RigLock $cfg 'cleanup.ps1')) { Exit-RigScript 1 }
$session = Get-RigSession $cfg $SessionId
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
