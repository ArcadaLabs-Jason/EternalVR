<#
.SYNOPSIS
Ends a run: puts the game's audio-session mute back to its prior state, sends a window-close request to
the recorded processes, stops them after CloseTimeoutSec (20 s), waits until they are gone from the
process list, then cleans the run up (settings restore, verified; Steam-Cloud files are kept as the
game left them and reported unless -RestoreCloudFiles, see cleanup.ps1). The only script that stops a
live run. The virtual display stays for the work block unless -EndBlock is given.

Exit codes: 0 stopped and cleaned up, 1 the cleanup failed (CLEANUP_PENDING kept; see cleanup.json).
#>
param(
    [string]$Run = 'latest',
    [switch]$EndBlock,
    [switch]$AcknowledgeSettingsChange,
    [switch]$RestoreCloudFiles,
    [string]$SessionId
)

. (Join-Path $PSScriptRoot 'common.ps1')

try {
    $cfg = Get-RigConfig
    $runDir = Resolve-RigRunDir $cfg $Run
} catch {
    Write-Host "stop.ps1: $($_.Exception.Message)"
    exit 1
}
Set-RigLogFiles @((Join-Path $cfg.RunsRoot 'rig.log'), (Join-Path $runDir 'rig.log'))
if (-not (Enter-RigLock $cfg 'stop.ps1')) { Exit-RigScript 1 }
$session = Get-RigSession $cfg $SessionId
Write-RigLog "stop $runDir"
if (-not (Test-RigPending $runDir)) { Write-RigLog 'run is not pending (already cleaned up)' }
Set-RigLogFiles @(Join-Path $cfg.RunsRoot 'rig.log')
$r = Invoke-RigCleanup $cfg -RunDirs @($runDir) -Explicit -AllowStop -EndBlock:$EndBlock -Session $session -Acknowledge:$AcknowledgeSettingsChange -RestoreCloud:$RestoreCloudFiles
if ($r.Failed) { Exit-RigScript 1 }
Exit-RigScript 0
