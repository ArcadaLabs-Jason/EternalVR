<#
.SYNOPSIS
Work-session bookkeeping for the rig scripts.

  start   new session ID in <runs>\SESSION, then cleanup of pending runs whose game has exited; a
          virtual display left by an earlier session is removed.
  end     the same cleanup and the end of the display's work block (cleanup.ps1 -EndBlock).
  status  session ID, pending runs and the display marker.

Neither start nor end stops a running game or cleans an owner run: use stop.ps1 -Run for those.
#>
param(
    [ValidateSet('start', 'end', 'status')][string]$Action = 'status'
)

. (Join-Path $PSScriptRoot 'common.ps1')

try { $cfg = Get-RigConfig } catch { Write-Host "session.ps1: configuration error: $($_.Exception.Message)"; exit 1 }
New-RigDirectory $cfg $cfg.RunsRoot
Set-RigLogFiles @(Join-Path $cfg.RunsRoot 'rig.log')
switch ($Action) {
    'start' {
        if (-not (Enter-RigLock $cfg 'session.ps1 start')) { Exit-RigScript 1 }
        $id = New-RigSession $cfg
        Write-RigLog "session $id started"
        $r = Invoke-RigCleanup $cfg -Session $id
        if ($r.Failed) { Exit-RigScript 1 } else { Exit-RigScript 0 }
    }
    'end' {
        if (-not (Enter-RigLock $cfg 'session.ps1 end')) { Exit-RigScript 1 }
        $id = Get-RigSession $cfg ''
        $r = Invoke-RigCleanup $cfg -EndBlock -Session $id
        Write-RigLog ("session $id ended (cleanup ok: {0}, still pending: {1})" -f (-not $r.Failed), $r.Pending.Count)
        if ($r.Failed) { Exit-RigScript 1 } else { Exit-RigScript 0 }
    }
    'status' {
        $s = Read-RigJson (Join-Path $cfg.RunsRoot 'SESSION')
        if ($s) { Write-Host "session:        $($s.id) (started $($s.started))" } else { Write-Host 'session:        none yet' }
        $pending = @(Get-RigRunDirs $cfg | Where-Object { Test-RigPending $_ })
        Write-Host ("pending runs:   {0}" -f $pending.Count)
        foreach ($p in $pending) { Write-Host "  $p" }
        $m = Get-RigDisplayMarker $cfg
        if ($m) { Write-Host "DISPLAY_ADDED:  session $($m.session), $($m.time)" } else { Write-Host 'DISPLAY_ADDED:  no' }
        exit 0
    }
}
