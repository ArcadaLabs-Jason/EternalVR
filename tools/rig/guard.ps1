<#
.SYNOPSIS
Keeps a running game off the owner's screen and away from the owner's keyboard and mouse for the whole
run. Started by run.ps1 (hidden) once its launch watch ends; exits by itself.

.DESCRIPTION
The game opens its window late (about a minute after the process starts on the rig) and can take the
foreground again at any time (map loads, a menu, a second window of an OpenXR runtime), long after
run.ps1's launch watch has returned. Every PollMs this guard:
- moves each visible top-level window of the run's live processes that lies outside the virtual display
  onto it, without activating it (-Rect; skipped when empty);
- gives the foreground back to the last window outside the game that had it, whenever the game has it
  (skipped with -NoFocus, and while keys.ps1 holds the focus on purpose: runs\FOCUS_HOLD names a live
  keys.ps1 process).
The game keeps running without focus (the layer's keep-active hook), so handing the foreground back
does not pause it.

It exits when the run has no live process left (after the first one was seen, or after StartSec when
none ever appears), or when the run is no longer pending (cleaned up). What it did is logged to the
run's rig.log and summarised in the run folder's guard.json.
#>
param(
    [Parameter(Mandatory = $true)][string]$RunDir,
    [string]$Rect = '',
    [long]$Foreground = 0,
    [switch]$NoFocus,
    [int]$PollMs = 250,
    [int]$StartSec = 300
)

. (Join-Path $PSScriptRoot 'common.ps1')

$cfg = Get-RigConfig
Set-RigLogFiles @(Join-Path $RunDir 'rig.log')
Initialize-RigNative

$area = $null
if ($Rect) {
    $v = @($Rect -split ',' | ForEach-Object { [int]$_ })
    $area = [pscustomobject]@{ x = $v[0]; y = $v[1]; width = $v[2]; height = $v[3] }
}
$holdFile = Join-Path $cfg.RunsRoot 'FOCUS_HOLD'
$started = Get-Date
$ownerWindow = $Foreground
$movedHandles = @{}
$handedBack = 0
$failedBack = 0
$lastBack = [DateTime]::MinValue
$seen = $false
$pids = [int[]]@()
$nextScan = [DateTime]::MinValue
$summary = Join-Path $RunDir 'guard.json'

function Test-FocusHeld {
    if (-not (Test-Path -LiteralPath $holdFile)) { return $false }
    $holder = 0
    try { $holder = [int]([IO.File]::ReadAllText($holdFile).Trim()) } catch { return $false }
    return [bool](Get-Process -Id $holder -ErrorAction SilentlyContinue)
}

function Save-GuardSummary([string]$Reason) {
    $o = [ordered]@{ pid = $PID; started = $started.ToString('o'); ended = (Get-Date).ToString('o'); reason = $Reason
        windowsMoved = $movedHandles.Count; foregroundHandedBack = $handedBack; foregroundHandBackFailed = $failedBack }
    try { [IO.File]::WriteAllText($summary, (ConvertTo-Json -InputObject $o)) } catch { }
}

Write-RigLog ("guard started (pid {0}): {1}{2}" -f $PID,
    $(if ($area) { "window kept on the virtual display at $Rect" } else { 'no virtual display' }),
    $(if ($NoFocus) { '; foreground left to the game (-KeepFocus)' } else { '; foreground handed back to the owner' }))

$reason = 'unknown'
try {
    while ($true) {
        $now = Get-Date
        if ($now -ge $nextScan) {
            $nextScan = $now.AddSeconds(2)
            if (-not (Test-RigPending $RunDir)) { $reason = 'run cleaned up'; break }
            $pids = [int[]]@(Get-RigRunLiveProcesses $RunDir | ForEach-Object { $_.Id })
            if ($pids.Count -gt 0) { $seen = $true }
            elseif ($seen) { $reason = 'game exited'; break }
            elseif (($now - $started).TotalSeconds -ge $StartSec) { $reason = "no game process within $StartSec s"; break }
        }
        if ($pids.Count -gt 0) {
            if ($area) {
                foreach ($w in @([EvrRig.Windows]::TopLevel($pids) | Where-Object { $_.Visible })) {
                    $inside = ($w.X -ge $area.x -and $w.Y -ge $area.y -and $w.X -lt ($area.x + $area.width) -and $w.Y -lt ($area.y + $area.height))
                    if (-not $inside) {
                        [void][EvrRig.Windows]::Move($w.Handle, [int]$area.x, [int]$area.y)
                        $key = [string]$w.Handle
                        if (-not $movedHandles.ContainsKey($key)) {
                            $movedHandles[$key] = $true
                            Write-RigLog "guard: moved window '$($w.Title)' of pid $($w.Pid) from $($w.X),$($w.Y) to the virtual display (not activated)"
                        }
                    }
                }
            }
            if (-not $NoFocus) {
                $fgPid = [EvrRig.Windows]::ForegroundPid()
                if ($fgPid -ne 0 -and $pids -notcontains $fgPid) {
                    $ownerWindow = [EvrRig.Windows]::Foreground()
                } elseif ($pids -contains $fgPid -and $ownerWindow -ne 0 -and ($now - $lastBack).TotalMilliseconds -ge 500 -and -not (Test-FocusHeld)) {
                    $lastBack = $now
                    if ([EvrRig.Windows]::RestoreForeground($ownerWindow)) {
                        $handedBack++
                        if ($handedBack -le 5 -or $handedBack % 20 -eq 0) {
                            Write-RigLog "guard: the game took the foreground; handed it back (#$handedBack)"
                        }
                    } else {
                        $failedBack++
                        if ($failedBack -le 3 -or $failedBack % 20 -eq 0) {
                            Write-RigLog "guard: the game took the foreground and Windows refused to hand it back (#$failedBack)" 'WARN'
                        }
                    }
                }
            }
        }
        Start-Sleep -Milliseconds $PollMs
    }
} catch {
    $reason = "error: $($_.Exception.Message)"
    Write-RigLog "guard stopped on an error: $($_.Exception.Message)" 'WARN'
}
Save-GuardSummary $reason
Write-RigLog ("guard ended ({0}): {1} window(s) moved, foreground handed back {2} time(s), {3} refused" -f $reason, $movedHandles.Count, $handedBack, $failedBack)
