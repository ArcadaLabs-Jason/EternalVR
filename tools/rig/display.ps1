<#
.SYNOPSIS
Virtual display for a work block of session-driven runs ('VDD by MTT', MTT1337), through the Windows
display configuration API (QueryDisplayConfig / SetDisplayConfig, and ChangeDisplaySettingsEx on the
virtual display only, without registry update): no administrator rights, no driver install, no file
written on C:. If Windows would need elevation for a change, it is reported, never elevated.

  add     extend the desktop onto the virtual monitor (never as primary) and set its mode (-Width,
          -Height, default 2560x1440; the largest mode it reports when that one is not offered);
          writes <runs>\DISPLAY_ADDED and saves the topology before the change in display-before.json.
  remove  take it away again if DISPLAY_ADDED says we added it: only its path is deactivated, every
          other display stays as it is; clears DISPLAY_ADDED.
  status  read-only report, including the virtual display's mode and whether Windows would accept the
          add (validation only).

No change is made while a game process runs. Every add and remove is appended to
<runs>\display-log.jsonl with the topology before and after.
Exit codes: 0 ok, 1 failed, 2 (add) the virtual monitor is not available.
#>
param(
    [ValidateSet('add', 'remove', 'status')][string]$Action = 'status',
    [int]$Width = 2560,
    [int]$Height = 1440,
    [switch]$Json,
    [string]$SessionId
)

. (Join-Path $PSScriptRoot 'common.ps1')

try { $cfg = Get-RigConfig } catch { Write-Host "display.ps1: configuration error: $($_.Exception.Message)"; exit 1 }

switch ($Action) {
    'status' {
        $state = Get-RigDisplayState $cfg
        $validation = 'n/a'
        if ($state.Mode -eq 'ccd' -and $state.Available -and -not $state.Active -and -not $state.Error) {
            try {
                $v = [EvrRig.Ccd]::AddVdd($script:RigVddMatch, $script:RigVddFriendlyName, $true)
                if ($v) { $validation = $v } else { $validation = 'ok (Windows would accept the add)' }
            } catch { $validation = "error: $($_.Exception.Message)" }
        }
        $marker = Get-RigDisplayMarker $cfg
        if ($Json) {
            $report = [ordered]@{
                mode = $state.Mode; available = $state.Available; active = $state.Active; vddPrimary = $state.VddPrimary
                rect = $state.Rect; vddMode = $state.VddMode; largestMode = $state.LargestMode; addValidation = $validation
                displayAddedMarker = $marker; error = $state.Error; topology = $state.Topology
            }
            ConvertTo-Json -InputObject $report -Depth 6
            exit 0
        }
        $primaryNote = ''
        if ($state.VddPrimary) { $primaryNote = ' (PRIMARY)' }
        Write-Host ("controller:     {0}" -f $state.Mode)
        Write-Host ("VDD available:  {0}" -f $state.Available)
        Write-Host ("VDD active:     {0}{1}" -f $state.Active, $primaryNote)
        if ($state.Rect) { Write-Host ("VDD rect:       {0},{1} {2}x{3}" -f $state.Rect.x, $state.Rect.y, $state.Rect.width, $state.Rect.height) }
        if ($state.VddMode) { Write-Host ("VDD mode:       {0} (largest offered {1})" -f $state.VddMode, $state.LargestMode) }
        Write-Host ("add validation: {0}" -f $validation)
        if ($marker) { Write-Host "DISPLAY_ADDED:  session $($marker.session), $($marker.time)" } else { Write-Host 'DISPLAY_ADDED:  no' }
        if ($state.Error) { Write-Host "ERROR:          display configuration query failed: $($state.Error)" }
        Write-Host 'active displays:'
        foreach ($t in $state.Topology) {
            $flags = ''
            if ($t.primary) { $flags += ' primary' }
            if ($t.vdd) { $flags += ' [VDD]' }
            Write-Host ("  {0,-30} {1,-14} {2},{3} {4}x{5} {6} Hz{7}" -f $t.name, $t.gdi, $t.x, $t.y, $t.width, $t.height, $t.refresh, $flags)
        }
        if ($state.Error) { exit 1 }
        if (-not $state.Available -and -not $state.Active) {
            Write-Host 'The virtual monitor path is not available: run.ps1 falls back to -NoDisplay with a warning.'
        }
        exit 0
    }
    'add' {
        New-RigDirectory $cfg $cfg.RunsRoot
        Set-RigLogFiles @(Join-Path $cfg.RunsRoot 'rig.log')
        if (-not (Enter-RigLock $cfg 'display.ps1 add')) { Exit-RigScript 1 }
        $session = Get-RigSession $cfg $SessionId
        $r = Add-RigDisplay $cfg $session 'display.ps1' $Width $Height
        Write-RigLog $r.Note
        if ($r.Ok) { Exit-RigScript 0 } elseif ($r.Unavailable) { Exit-RigScript 2 } else { Exit-RigScript 1 }
    }
    'remove' {
        if (-not (Test-Path -LiteralPath $cfg.RunsRoot)) { Write-Host 'nothing to remove'; exit 0 }
        Set-RigLogFiles @(Join-Path $cfg.RunsRoot 'rig.log')
        if (-not (Enter-RigLock $cfg 'display.ps1 remove')) { Exit-RigScript 1 }
        $busy = @(Get-RigRunDirs $cfg | Where-Object { (Test-RigPending $_) -and @(Get-RigRunLiveProcesses $_).Count -gt 0 })
        if ($busy.Count -gt 0) { Write-RigLog "not removed: a run still has a live process ($($busy[0])); stop it first" 'ERROR'; Exit-RigScript 1 }
        $r = Remove-RigDisplay $cfg -Reason 'display.ps1 remove'
        Write-RigLog $r.Note
        if ($r.Ok) { Exit-RigScript 0 } else { Exit-RigScript 1 }
    }
}
