<#
.SYNOPSIS
A/B runs of CPU-saving cvar sets (docs/rig-findings/perf-cpu-cvars.md): the same Route S scene once per set, with
the layer's CPU timing, then a table of the per-eye stage times and the tick rate per run and per set.

.DESCRIPTION
For each round and each set, in interleaved order (base, A, B, ..., base, A, B, ...):
  1. starts a Route S run on the simulator through the rig's rsrun.ps1 (-RsRun; it stages the tree's layer build
     as <Prefix><set>-<round>, logs in tmp-vr\rs\<name>-logs) with ETERNALVR_CPU_TIMING=1 and the set as
     ETERNALVR_DEBUG_CVARS, into -Map (e1m2_battle's start: the player stands still, the simulator's head does not
     move);
  2. waits for the layer's "aim: head aim on" line (the player is in the map), then -Seconds more (bounded);
  3. stops the run with stop.ps1 -RestoreCloudFiles and checks that its last line says the cleanup is done;
     anything else stops the batch (the rig refuses new runs until the cleanup is resolved).
Then (or with -ParseOnly, from logs already there) it reads each run's layer log: the 10 s "cpu:" windows that
start -Warmup seconds or more after "aim: head aim on", and prints per run and per set (median over the rounds):
ticks/s, the tick period p50/p95, "eye L" (the time outside the frame-end jobs: the game frame and eye L's
views) and "eye R" (eye R's render) wall p50/p95, and the whole process's CPU per tick p50/p95, all in ms (the
median over the windows of each window's p50 and p95). It also lists the layer's "cvars:" lines for each
set's cvars, so a cvar that was not found or not written shows.

Needs a build of the layer with caps (this branch or later) in -Tree (build\windows-msvc\src\vkcore, which
rsrun.ps1 stages), Steam running, the rig's virtual display and OpenXR-Simulator (rsrun.ps1's defaults). One run
is about 2.5 minutes with the load. Never start it while another agent or the owner uses the rig.

  powershell -NoProfile -ExecutionPolicy Bypass -File tools\rig\cpu-cvar-ab.ps1 -Tree <worktree> -Rounds 4
  powershell -NoProfile -ExecutionPolicy Bypass -File tools\rig\cpu-cvar-ab.ps1 -ParseOnly -Prefix cc

.PARAMETER Sets
Names and ETERNALVR_DEBUG_CVARS values, as 'name=cvar=value;cvar=value' strings. 'base=' is the reference
(no cvar). Default: the candidates of docs/rig-findings/perf-cpu-cvars.md section 4.

.PARAMETER SetCommands
Console commands for a set, as 'name=<ETERNALVR_DEBUG_COMMANDS value>' strings (docs/rig-findings/debug-commands.md;
seconds count from the player being in the map, so '1:is_update 0' bounds a cvar that cannot be set at start-up).
A set may have commands and no cvars: 'stream=' in -Sets with 'stream=1:is_update 0' here.

.PARAMETER Only
Run only these set names (plus 'base', always).
#>
param(
    [string]$Tree = '',
    [string]$RsRun = '',
    [string]$RunsRoot = '',
    [string]$Prefix = 'cc',
    [string]$Map = 'game/sp/e1m2_battle/e1m2_battle',
    [string[]]$Sets = @(
        'base=',
        'saver=r_shadowMaxStaleFrames=1,1,1,2,2;r_skipPlayerShadow=1;r_shadowsDistanceFadeMultiplier=<=1;r_lightDistanceFadeMultiplier=<=1',
        'stale=r_shadowMaxStaleFrames=1,1,1,2,2',
        'player=r_skipPlayerShadow=1',
        'shadowfade=r_shadowsDistanceFadeMultiplier=<=1',
        'lightfade=r_lightDistanceFadeMultiplier=<=1',
        'sunstale=r_shadowParallelMaxStaleFrames=1,1,1,2',
        'sunslices=r_shadowNumAccurateSunSlices=0',
        'props=prop_skipShadows=1',
        'lowfade=r_shadowsDistanceFadeMultiplier=<=0.8;r_lightDistanceFadeMultiplier=<=0.8',
        'decals=r_decalDistanceFadeMultiplier=<=0.5',
        'particles=r_particleFadeQualityMultiplier=<=1',
        'pquality=r_particleQualityLevel=1',
        'lod=r_lodScale=<=1.625',
        'flares=r_skipFlares=1'
    ),
    [string[]]$SetCommands = @(),
    [string[]]$Only = @(),
    [int]$Rounds = 4,
    [int]$Seconds = 60,
    [int]$Warmup = 15,
    [int]$InMapTimeoutSec = 240,
    [string[]]$ExtraEnv = @(),
    [switch]$ParseOnly
)
# Defaults: this checkout, and the rig runs folder in the workspace (workspace.ps1, EVR_WORKSPACE).
. (Join-Path $PSScriptRoot 'workspace.ps1')
if (-not $Tree) { $Tree = Split-Path -Parent (Split-Path -Parent $PSScriptRoot) }
if (-not $RunsRoot) { $RunsRoot = Join-Path (Get-EvrWorkspace) 'tmp-vr\rs' }
if (-not $RsRun) { $RsRun = Join-Path $RunsRoot 'rsrun.ps1' }

$ErrorActionPreference = 'Stop'
# With -File a list arrives as one comma-separated string. Set values hold commas too (1,1,1,2,2), so a set list
# splits only before a comma followed by the next set's name and '='.
$Only = @($Only | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$Sets = @($Sets | ForEach-Object { $_ -split ',(?=[A-Za-z0-9]+=)' } | Where-Object { $_ })
$SetCommands = @($SetCommands | ForEach-Object { $_ -split ',(?=[A-Za-z0-9]+=)' } | Where-Object { $_ })

function Get-SetList {
    $list = @()
    foreach ($s in $Sets) {
        $eq = $s.IndexOf('=')
        if ($eq -lt 1) { throw "bad set '$s' (name=cvar=value;...)" }
        $name = $s.Substring(0, $eq)
        if ($name -notmatch '^[A-Za-z0-9]+$') { throw "bad set name '$name' (letters and digits)" }
        if ($Only.Count -gt 0 -and $name -ne 'base' -and $Only -notcontains $name) { continue }
        $list += [pscustomobject]@{ Name = $name; Cvars = $s.Substring($eq + 1); Commands = '' }
    }
    foreach ($c in $SetCommands) {
        $eq = $c.IndexOf('=')
        if ($eq -lt 1) { throw "bad set commands '$c' (name=<seconds>:<command>|...)" }
        $name = $c.Substring(0, $eq)
        if ($name -eq 'base') { throw "the 'base' set is the reference and takes no commands" }
        $set = $list | Where-Object { $_.Name -eq $name }
        if (-not $set) {
            if ($Sets | Where-Object { $_ -like "$name=*" }) { continue }
            throw "commands for '$name', which is not in -Sets"
        }
        $set.Commands = $c.Substring($eq + 1)
    }
    return $list
}

function Get-Median([double[]]$v) {
    if (-not $v -or $v.Count -eq 0) { return [double]::NaN }
    $s = $v | Sort-Object
    $n = $s.Count
    if ($n % 2) { return [double]$s[($n - 1) / 2] }
    return ([double]$s[$n / 2 - 1] + [double]$s[$n / 2]) / 2
}

function Get-RunLog([string]$run) {
    $dir = Join-Path $RunsRoot "$run-logs"
    if (-not (Test-Path $dir)) { return $null }
    return Get-ChildItem (Join-Path $dir 'eternalvr-2*.log') -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1
}

# Log lines start with "[  seconds] [thread] ".
function Get-LineTime([string]$line) {
    if ($line -match '^\[\s*([0-9.]+)\]') { return [double]$Matches[1] }
    return [double]::NaN
}

function Read-Run([string]$run, [string]$cvars) {
    $log = Get-RunLog $run
    $r = [ordered]@{ Run = $run; Windows = 0; TicksPerS = [double]::NaN; PeriodP50 = [double]::NaN; PeriodP95 = [double]::NaN
        EyeLP50 = [double]::NaN; EyeLP95 = [double]::NaN; EyeRP50 = [double]::NaN; EyeRP95 = [double]::NaN
        ProcP50 = [double]::NaN; ProcP95 = [double]::NaN; Note = '' }
    if (-not $log) { $r.Note = 'no layer log'; return [pscustomobject]$r }
    $lines = Get-Content $log.FullName
    $inMap = $lines | Where-Object { $_ -match 'aim: head aim on' } | Select-Object -First 1
    if (-not $inMap) { $r.Note = 'never in the map'; return [pscustomobject]$r }
    $from = (Get-LineTime $inMap) + $Warmup
    $ticks = @(); $pp50 = @(); $pp95 = @(); $l50 = @(); $l95 = @(); $r50 = @(); $r95 = @(); $c50 = @(); $c95 = @()
    $window = $false
    foreach ($line in $lines) {
        if ($line -notmatch '\] cpu: ') { continue }
        $t = Get-LineTime $line
        if ($line -match 'cpu: last ([0-9.]+) s: (\d+) tick\(s\).*tick period mean/p50/p95/p99/max [0-9.]+/([0-9.]+)/([0-9.]+)/') {
            $window = $t -ge $from
            if (-not $window) { continue }
            $ticks += [double]$Matches[2] / [double]$Matches[1]
            $pp50 += [double]$Matches[3]; $pp95 += [double]$Matches[4]
        } elseif ($window -and $line -match 'game frame \+ eye L views\) [0-9.]+/([0-9.]+) wall \(p95 ([0-9.]+)\).*eye R render [0-9.]+/([0-9.]+) wall \(p95 ([0-9.]+)\)') {
            $l50 += [double]$Matches[1]; $l95 += [double]$Matches[2]; $r50 += [double]$Matches[3]; $r95 += [double]$Matches[4]
        } elseif ($window -and $line -match 'whole process CPU [0-9.]+/([0-9.]+) \(p95 ([0-9.]+)\)') {
            $c50 += [double]$Matches[1]; $c95 += [double]$Matches[2]
        }
    }
    $r.Windows = $ticks.Count
    if ($ticks.Count -eq 0) { $r.Note = 'no cpu: window after the warm-up (ETERNALVR_CPU_TIMING?)'; return [pscustomobject]$r }
    $r.TicksPerS = Get-Median $ticks; $r.PeriodP50 = Get-Median $pp50; $r.PeriodP95 = Get-Median $pp95
    $r.EyeLP50 = Get-Median $l50; $r.EyeLP95 = Get-Median $l95; $r.EyeRP50 = Get-Median $r50; $r.EyeRP95 = Get-Median $r95
    $r.ProcP50 = Get-Median $c50; $r.ProcP95 = Get-Median $c95
    # The layer's word on each cvar of the set: registered, written, read back.
    $notes = @()
    foreach ($item in ($cvars -split ';' | Where-Object { $_ -match '=' })) {
        $name = ($item -split '=')[0].Trim()
        $said = $lines | Where-Object { $_ -match "\] cvars: $([regex]::Escape($name)) " } | Select-Object -First 1
        $held = $lines | Where-Object { $_ -match "\] cvars: held at run time: .*$([regex]::Escape($name)) " } | Select-Object -First 1
        $alone = $lines | Where-Object { $_ -match "\] cvars: $([regex]::Escape($name)) registered" } | Select-Object -First 1
        if ($said) { $notes += ($said -replace '^.*\] cvars: ', '') }
        elseif ($alone) { $notes += ($alone -replace '^.*\] cvars: ', '') }
        elseif ($held) { $notes += "$name held, never written (already at the value)" }
        else { $notes += "$name (not in the held list: an older layer build?)" }
    }
    $r.Note = $notes -join '; '
    return [pscustomobject]$r
}

function Show-Tables($list) {
    $rows = @()
    foreach ($set in $list) {
        for ($i = 1; $i -le 50; $i++) {
            $run = "$Prefix$($set.Name)-$i"
            if (-not (Get-RunLog $run)) { continue }
            $row = Read-Run $run $set.Cvars
            $row | Add-Member -NotePropertyName Set -NotePropertyValue $set.Name
            $rows += $row
        }
    }
    if ($rows.Count -eq 0) { Write-Output "no runs named $Prefix<set>-<round> under $RunsRoot"; return }
    $fmt = { param($v) if ([double]::IsNaN([double]$v)) { '-' } else { '{0:0.00}' -f $v } }
    Write-Output ''
    Write-Output 'Per run (ms; medians of the 10 s windows after the warm-up)'
    Write-Output ('{0,-22} {1,4} {2,8} {3,15} {4,15} {5,15} {6,15}' -f 'run', 'win', 'ticks/s', 'period p50/p95', 'eye L p50/p95', 'eye R p50/p95', 'proc CPU p50/95')
    foreach ($r in $rows) {
        Write-Output ('{0,-22} {1,4} {2,8} {3,15} {4,15} {5,15} {6,15}' -f $r.Run, $r.Windows, (& $fmt $r.TicksPerS),
            ((& $fmt $r.PeriodP50) + '/' + (& $fmt $r.PeriodP95)), ((& $fmt $r.EyeLP50) + '/' + (& $fmt $r.EyeLP95)),
            ((& $fmt $r.EyeRP50) + '/' + (& $fmt $r.EyeRP95)), ((& $fmt $r.ProcP50) + '/' + (& $fmt $r.ProcP95)))
    }
    Write-Output ''
    Write-Output 'Per set (median over the rounds; delta ticks/s against base)'
    $base = Get-Median @($rows | Where-Object { $_.Set -eq 'base' -and -not [double]::IsNaN($_.TicksPerS) } | ForEach-Object { $_.TicksPerS })
    Write-Output ('{0,-12} {1,3} {2,8} {3,7} {4,15} {5,15} {6,15}' -f 'set', 'n', 'ticks/s', 'delta', 'eye L p50/p95', 'eye R p50/p95', 'proc CPU p50/95')
    foreach ($set in $list) {
        $mine = @($rows | Where-Object { $_.Set -eq $set.Name -and -not [double]::IsNaN($_.TicksPerS) })
        if ($mine.Count -eq 0) { continue }
        $m = { param($p) Get-Median @($mine | ForEach-Object { $_.$p }) }
        $tps = & $m 'TicksPerS'
        $delta = if ([double]::IsNaN($base)) { '-' } else { '{0:+0.0;-0.0}' -f ($tps - $base) }
        Write-Output ('{0,-12} {1,3} {2,8} {3,7} {4,15} {5,15} {6,15}' -f $set.Name, $mine.Count, (& $fmt $tps), $delta,
            ((& $fmt (& $m 'EyeLP50')) + '/' + (& $fmt (& $m 'EyeLP95'))), ((& $fmt (& $m 'EyeRP50')) + '/' + (& $fmt (& $m 'EyeRP95'))),
            ((& $fmt (& $m 'ProcP50')) + '/' + (& $fmt (& $m 'ProcP95'))))
    }
    Write-Output ''
    Write-Output 'What the layer logged for each set''s cvars (first run of each set)'
    foreach ($set in $list) {
        $first = $rows | Where-Object { $_.Set -eq $set.Name } | Select-Object -First 1
        if ($first -and $set.Cvars) { Write-Output ("{0}: {1}" -f $set.Name, $first.Note) }
        elseif ($first -and $first.Note) { Write-Output ("{0}: {1}" -f $set.Name, $first.Note) }
    }
}

$setList = Get-SetList
if (-not ($setList | Where-Object { $_.Name -eq 'base' })) { throw "the sets need a 'base=' reference" }

if (-not $ParseOnly) {
    if (-not (Test-Path $RsRun)) { throw "no rsrun.ps1 at $RsRun" }
    $stop = Join-Path $Tree 'tools\rig\stop.ps1'
    for ($i = 1; $i -le $Rounds; $i++) {
        foreach ($set in $setList) {
            $name = "$Prefix$($set.Name)-$i"
            $logs = Join-Path $RunsRoot "$name-logs"
            if (Test-Path $logs) { Remove-Item -Recurse -Force $logs }
            $gameEnv = @('ETERNALVR_CPU_TIMING=1') + $ExtraEnv
            if ($set.Cvars) { $gameEnv += "ETERNALVR_DEBUG_CVARS=$($set.Cvars)" }
            if ($set.Commands) { $gameEnv += "ETERNALVR_DEBUG_COMMANDS=$($set.Commands)" }
            Write-Output "=== $name ($(Get-Date -Format HH:mm:ss)): $($set.Cvars) $($set.Commands)"
            & $RsRun -Name $name -Tree $Tree -Ack -Map $Map -WatchSeconds 30 -ExtraEnv $gameEnv 2>&1 | Select-Object -Last 3
            # In the map, then the measured stretch (bounded either way).
            $deadline = (Get-Date).AddSeconds($InMapTimeoutSec)
            $inMap = $false
            while ((Get-Date) -lt $deadline) {
                $log = Get-RunLog $name
                if ($log -and (Select-String -Path $log.FullName -Pattern 'aim: head aim on' -Quiet)) { $inMap = $true; break }
                if (-not (Get-Process DOOMEternalx64vk -ErrorAction SilentlyContinue)) { break }
                Start-Sleep -Seconds 2
            }
            if ($inMap) { Start-Sleep -Seconds ($Seconds + $Warmup + 12) } else { Write-Output "${name}: never reached the map" }
            # stop.ps1 reports through the host (information stream): *>&1 collects every stream.
            $lines = @(& $stop -RestoreCloudFiles *>&1 | ForEach-Object { "$_" } | Where-Object { $_.Trim() })
            $last = $lines | Select-Object -Last 1
            Write-Output "stop: $last"
            # Steam's record of the cloud files sometimes lags the resync launch ("still pending"): the rig's own
            # remedy is one more cleanup of that run, which then finds nothing left to restore.
            $pending = $lines | Where-Object { $_ -match 'run (\S+) still pending' } | Select-Object -First 1
            if ($pending -and $pending -match 'run (\S+) still pending') {
                $cleanup = Join-Path $Tree 'tools\rig\cleanup.ps1'
                $lines = @(& $cleanup -Run $Matches[1] -RestoreCloudFiles *>&1 | ForEach-Object { "$_" } | Where-Object { $_.Trim() })
                Write-Output "cleanup again: $($lines | Where-Object { $_ -match 'cleanup (done|FAILED)' } | Select-Object -Last 1)"
            }
            if (-not ($lines | Where-Object { $_ -match 'cleanup done' }) -or ($lines | Where-Object { $_ -match 'cleanup FAILED' })) { throw "stop.ps1 did not report 'cleanup done' after $name; resolve the cleanup before more runs" }
            if (Get-Process DOOMEternalx64vk -ErrorAction SilentlyContinue) { throw "the game is still running after stop.ps1 ($name)" }
        }
    }
}

Show-Tables $setList
