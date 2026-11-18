# The benchmark routes (docs/rig-findings/bench-routes.md; ROADMAP M1, T-075, T-105, T-111): three 60 s routes run at
# the launcher's default settings, each measured from the layer's own logs. Dot-sourced by qa-suite.ps1 after
# qa-scenarios-coexist.ps1 (whose Get-QaCoexistModules it uses); by name only (measurements, not checks of a merge).
#
#   bench-combat, bench-outdoor, bench-hub          each route at the launcher's auto render size
#   bench-combat-q3, bench-outdoor-q3, bench-hub-q3  the same at Quest 3 size (ETERNALVR_RENDER_SIZE=2496x2688; the
#                                                    runtime's limits may fit it smaller: the summary says what it took)
#
# Every run: the launcher's default environment (as `EternalVR.Launcher.exe --dry-run` prints it for a fresh data
# folder), ray tracing held off (the gates are RT off), GPU timing on every frame and CPU timing, the hold behind a
# runtime menu off (the simulator's session is VISIBLE whenever its window is not in front; a headset's is FOCUSED),
# the game at Normal priority (a run started from a scheduled task can inherit Below Normal, present-latency.md; on
# the test PC it read Normal in every bench run). The route's script runs 70 s; the measurement is the six whole 10 s windows of the layer's lines inside it
# (60 s), and the per-frame rows of the GPU and frames tables in those windows. One summary line per run (a note in
# the report, and bench.json in the scenario's folder for qa-bench-table.ps1). Run three times for the spread: three
# suites, each with all the routes, so a change in the PC's background load spreads over every route.

$script:QaBenchLauncherEnv = @(
    # The launcher's defaults for a fresh data folder (0.1.94, --dry-run on the test PC), less what the suite sets itself
    # (the layer path, the log folder, the window and the mirror's place) and the render size, which each route sets.
    'ETERNALVR_WORLD_SCALE=1.00', 'ETERNALVR_ALTERNATE_EYES=0', 'ETERNALVR_UI_LAYER=1', 'ETERNALVR_PERF_OVERLAY=0',
    'ETERNALVR_HUD=panel', 'ETERNALVR_HUD_FRONT=1', 'ETERNALVR_CONTROLLERS=1', 'ETERNALVR_AIM=hand',
    'ETERNALVR_RENDER_SCALE=1.00', 'ETERNALVR_MIRROR_SIZE=1280x720', 'ETERNALVR_MIRROR_CROP=full',
    'ETERNALVR_CINEMA_ASPECT=16:9', 'ETERNALVR_POSTURE=auto', 'ETERNALVR_HEIGHT=slayer', 'ETERNALVR_RECENTER_HOLD=2.0',
    'ETERNALVR_TURN=smooth', 'ETERNALVR_SNAP_DEGREES=45', 'ETERNALVR_TURN_RATE=230', 'ETERNALVR_VIGNETTE=off',
    'ETERNALVR_GLORY_KILLS=follow', 'ETERNALVR_PICKUP_CAMERA=still', 'ETERNALVR_HANDEDNESS=right', 'ETERNALVR_ARMS=shown',
    'ETERNALVR_OFFHAND=free', 'ETERNALVR_ARM_SOLVE=bind', 'ETERNALVR_TWO_HAND=0', 'ETERNALVR_LOCOMOTION=look',
    'ETERNALVR_DOSSIER=hold', 'ETERNALVR_MAP_STICKS=weapon', 'ETERNALVR_WHEEL_SELECT=stick',
    'ETERNALVR_THUMBREST_WHEEL=off', 'ETERNALVR_THUMBREST_PICK=wheel', 'ETERNALVR_THUMBREST_FACE_TOUCH=0',
    'ETERNALVR_THUMBREST_SLOWDOWN=1', 'ETERNALVR_WRIST_FLICK=1', 'ETERNALVR_SWING=0', 'ETERNALVR_HANDS_JUMP=0',
    'ETERNALVR_PUNCH_SPEED=2.80', 'ETERNALVR_HOLD_SECONDS=0.25', 'ETERNALVR_WEAPON_HAND_TILT=0.00',
    'ETERNALVR_OFF_HAND_TILT=0.00', 'ETERNALVR_UI_RETICLE=1', 'ETERNALVR_BODY_FOLLOW=1', 'ETERNALVR_HEAD_FADE=1',
    'ETERNALVR_AIM_SMOOTHING=0.30', 'ETERNALVR_HAPTICS=0.60', 'ETERNALVR_BHAPTICS=0', 'ETERNALVR_BHAPTICS_INTENSITY=1.00',
    'ETERNALVR_UI_DISTANCE=1.50', 'ETERNALVR_UI_WIDTH=2.00', 'ETERNALVR_UI_OFFSET_Y=0.00', 'ETERNALVR_UI_RETICLE_SIZE=1.00',
    'ETERNALVR_MIRROR=left', 'ETERNALVR_CUTSCENES=cinema', 'ETERNALVR_CUTSCENE_ARMS=hidden', 'ETERNALVR_CUTSCENE_3D=1',
    'ETERNALVR_SHOT_ORIGIN=hand', 'ETERNALVR_MENU_BEAM=1', 'ETERNALVR_STEREO_SSDO=1', 'ETERNALVR_STEREO_SSR=1',
    'ETERNALVR_PACE=headset', 'ETERNALVR_DEPTH_LAYER=0', 'ETERNALVR_CPU_SAVER=is_cacheGreedily=0',
    'DISABLE_XR_APILAYER_VIRTUALDESKTOP_OCULUS_COMPATIBILITY=1'
)
# What the measurement adds: every frame timed (the CSV), the CPU split, the simulator's VISIBLE session not held, RT off.
$script:QaBenchMeasureEnv = @('ETERNALVR_GPU_TIMING=1', 'ETERNALVR_CPU_TIMING=1', 'ETERNALVR_TEST_UNFOCUSED_CAP=0',
    'ETERNALVR_DEBUG_CVARS=r_useRayTracing=0')
$script:QaBenchQuest3 = '2496x2688'
$script:QaBenchSeconds = 70
$script:QaBenchCycle = 10
$script:QaBenchTutorials = 'g_setting_hud_auto_dismiss_tutorials 1;g_setting_tutorials 0'

# The game at Normal priority, as a player's (a scheduled task's children can start Below Normal), noted with what it was.
function Set-QaBenchPriority($Ctx) {
    $p = Get-Process -Name $script:QaGameProcess -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $p) { Add-QaNote $Ctx 'priority: no game process'; return }
    try {
        $was = $p.PriorityClass
        $p.PriorityClass = 'Normal'
        Add-QaNote $Ctx "priority: the game's was $was, set to Normal"
    } catch { Add-QaNote $Ctx "priority: not set ($($_.Exception.Message))" }
}

# One cycle of a route's script, repeated for QaBenchSeconds: @(seconds into the cycle, input lines), in order.
function Invoke-QaBenchScript($Ctx, [object[]]$Cycle) {
    $start = Get-Date
    $Ctx.BenchStart = $start
    for ($k = 0; $k * $script:QaBenchCycle -lt $script:QaBenchSeconds; $k++) {
        foreach ($step in $Cycle) {
            $at = $start.AddSeconds($k * $script:QaBenchCycle + $step[0])
            $left = ($at - (Get-Date)).TotalSeconds
            if ($left -gt 0) { Wait-QaSeconds $Ctx $left }
            Write-QaInput $Ctx @($step[1])
        }
    }
    $end = $start.AddSeconds($script:QaBenchSeconds)
    $left = ($end - (Get-Date)).TotalSeconds
    if ($left -gt 0) { Wait-QaSeconds $Ctx $left }
    $Ctx.BenchEnd = Get-Date
}

# A still player turning: 0.4 s of the turn stick (about 90 degrees at the default 230 degrees a second) twice a cycle.
$script:QaBenchLookCycle = @(
    @(0.0, @('right.aim = 0, -5')),
    @(2.0, @('right.aim = 0, -5', 'right.stick = 1, 0')), @(2.4, @('right.aim = 0, -5')),
    @(7.0, @('right.aim = 0, -5', 'right.stick = 1, 0')), @(7.4, @('right.aim = 0, -5'))
)
# A view held on one heading: ahead, right, ahead, left, each a 0.3 s push of the turn stick (about 60 degrees; equal
# pushes each way, so the smooth turn's ramp cannot add up to a drift over the cycles).
$script:QaBenchVistaCycle = @(
    @(0.0, @('right.aim = 0, -5')),
    @(2.0, @('right.aim = 0, -5', 'right.stick = 1, 0')), @(2.3, @('right.aim = 0, -5')),
    @(4.0, @('right.aim = 0, -5', 'right.stick = -1, 0')), @(4.3, @('right.aim = 0, -5')),
    @(6.0, @('right.aim = 0, -5', 'right.stick = -1, 0')), @(6.3, @('right.aim = 0, -5')),
    @(8.0, @('right.aim = 0, -5', 'right.stick = 1, 0')), @(8.3, @('right.aim = 0, -5'))
)
# Combat: bursts of fire straight ahead (0.6 s on, 0.4 s off), the same two turns.
$script:QaBenchFightCycle = @(
    @(0.0, @('right.aim = 0, -5', 'right.trigger = 1')), @(0.6, @('right.aim = 0, -5')),
    @(1.0, @('right.aim = 0, -5', 'right.trigger = 1')), @(1.6, @('right.aim = 0, -5')),
    @(2.0, @('right.aim = 0, -5', 'right.stick = 1, 0')), @(2.4, @('right.aim = 0, -5')),
    @(3.0, @('right.aim = 0, -5', 'right.trigger = 1')), @(3.6, @('right.aim = 0, -5')),
    @(4.0, @('right.aim = 0, -5', 'right.trigger = 1')), @(4.6, @('right.aim = 0, -5')),
    @(5.0, @('right.aim = 0, -5', 'right.trigger = 1')), @(5.6, @('right.aim = 0, -5')),
    @(6.0, @('right.aim = 0, -5', 'right.trigger = 1')), @(6.6, @('right.aim = 0, -5')),
    @(7.0, @('right.aim = 0, -5', 'right.stick = 1, 0')), @(7.4, @('right.aim = 0, -5')),
    @(8.0, @('right.aim = 0, -5', 'right.trigger = 1')), @(8.6, @('right.aim = 0, -5')),
    @(9.0, @('right.aim = 0, -5', 'right.trigger = 1')), @(9.6, @('right.aim = 0, -5'))
)

# ---- The measurement --------------------------------------------------------------------------------------------

# Nearest-rank percentile of a sorted array.
function Get-QaBenchPct([double[]]$Sorted, [double]$P) {
    if ($Sorted.Count -eq 0) { return [double]::NaN }
    $i = [int][Math]::Ceiling($P * $Sorted.Count) - 1
    return $Sorted[[Math]::Max(0, [Math]::Min($Sorted.Count - 1, $i))]
}

function Get-QaBenchStats([System.Collections.Generic.List[double]]$Values) {
    $a = $Values.ToArray()
    [Array]::Sort($a)
    $mean = if ($a.Count) { ($a | Measure-Object -Average).Average } else { [double]::NaN }
    return [pscustomobject]@{ N = $a.Count; Mean = $mean; P50 = (Get-QaBenchPct $a 0.50); P95 = (Get-QaBenchPct $a 0.95)
        P99 = (Get-QaBenchPct $a 0.99); Max = $(if ($a.Count) { $a[-1] } else { [double]::NaN }) }
}

# The layer log's lines matching $Pattern as (seconds since the log's start, match).
function Get-QaBenchLines([string[]]$Lines, [string]$Pattern) {
    $inv = [Globalization.CultureInfo]::InvariantCulture
    $out = New-Object System.Collections.ArrayList
    foreach ($l in $Lines) {
        if ($l -notmatch '^\[\s*([\d.]+)\]') { continue }
        $t = [double]::Parse($Matches[1], $inv)
        $m = [regex]::Match($l, $Pattern)
        if ($m.Success) { [void]$out.Add([pscustomobject]@{ T = $t; M = $m }) }
    }
    return $out
}

# A CSV's rows (header split into an index) without Import-Csv's cost.
function Read-QaBenchCsv([string]$Path) {
    $text = Read-QaShared $Path
    $rows = @($text -split "`r?`n" | Where-Object { $_ })
    if ($rows.Count -lt 1) { return $null }
    $cols = @{}
    $h = $rows[0] -split ','
    for ($i = 0; $i -lt $h.Count; $i++) { $cols[$h[$i]] = $i }
    return [pscustomobject]@{ Cols = $cols; Rows = @($rows | Select-Object -Skip 1) }
}

function Format-QaBenchStats($s, [string]$Fmt = 'N1') {
    if (-not $s -or $s.N -eq 0) { return 'none' }
    return ('{0}/{1}/{2}/{3} (max {4}, n {5})' -f $s.P50.ToString($Fmt), $s.P95.ToString($Fmt), $s.P99.ToString($Fmt),
        $s.Mean.ToString($Fmt), $s.Max.ToString($Fmt), $s.N)
}

# The route's measurement from the layer log and its two tables: one summary note, bench.json in the scenario folder.
function Measure-QaBench($Ctx) {
    $inv = [Globalization.CultureInfo]::InvariantCulture
    $logPath = Get-QaLayerLog $Ctx
    if (-not $logPath -or -not $Ctx.BenchStart) { Add-QaResult $Ctx 'bench: measured' $false 'no layer log or no route start'; return }
    $text = Read-QaShared $logPath
    $lines = @($text -split "`r?`n")
    if ($text -notmatch 'log start (\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3}) local') { Add-QaResult $Ctx 'bench: measured' $false 'no log start line'; return }
    $anchor = [datetime]::ParseExact($Matches[1], 'yyyy-MM-dd HH:mm:ss.fff', $inv)
    $from = ($Ctx.BenchStart - $anchor).TotalSeconds
    $to = ($Ctx.BenchEnd - $anchor).TotalSeconds
    $logDir = Split-Path -Parent $logPath
    $procId = if ((Split-Path -Leaf $logPath) -match '-(\d+)\.log$') { $Matches[1] } else { '' }
    $r = [ordered]@{ Scenario = $Ctx.Name; Run = $(if ($Ctx.RunDir) { Split-Path -Leaf $Ctx.RunDir } else { '' }); Asked = $Ctx.BenchSize }

    # The render size the runtime recommended and the one the game's swapchain took.
    $rec = @(Get-QaBenchLines $lines 'size: the runtime recommends (\d+)x(\d+) per eye') | Select-Object -Last 1
    $took = @(Get-QaBenchLines $lines "size: the game's swapchain has the render size (\d+)x(\d+)") | Select-Object -Last 1
    $r.Recommended = if ($rec) { "$($rec.M.Groups[1].Value)x$($rec.M.Groups[2].Value)" } else { '' }
    $r.Size = if ($took) { "$($took.M.Groups[1].Value)x$($took.M.Groups[2].Value)" } else { '' }
    $r.RtOff = [bool]($text -match 'cvars: r_useRayTracing 1 -> 0')

    # GPU table: the whole 10 s gpu windows inside the route, by their cumulative frame counts.
    $gpu = @(Get-QaBenchLines $lines 'gpu: last ([\d.]+) s: (\d+) frame\(s\) measured')
    $cum = 0; $rowFrom = -1; $rowTo = -1; $gpuWindows = 0
    foreach ($g in $gpu) {
        $len = [double]::Parse($g.M.Groups[1].Value, $inv); $n = [int]$g.M.Groups[2].Value
        if ($g.T - $len -ge $from -and $g.T -le $to + 0.5 -and $gpuWindows -lt 6) {
            if ($rowFrom -lt 0) { $rowFrom = $cum }
            $rowTo = $cum + $n; $gpuWindows++
        }
        $cum += $n
    }
    $tickCpu = New-Object 'System.Collections.Generic.List[double]'
    $tickGpu = New-Object 'System.Collections.Generic.List[double]'
    $csv = if ($procId) { Read-QaBenchCsv (Join-Path $logDir "eternalvr-gpu-$procId.csv") } else { $null }
    if ($csv -and $rowFrom -ge 0) {
        $c = $csv.Cols; $left = $null
        for ($i = $rowFrom; $i -lt [Math]::Min($rowTo, $csv.Rows.Count); $i++) {
            $f = $csv.Rows[$i] -split ','
            $row = @{ Frame = [long]$f[$c['frame']]; Eye = $f[$c['eye']]; Cpu = [double]::Parse($f[$c['cpu_ms']], $inv)
                      Busy = [double]::Parse($f[$c['gpu_busy_ms']], $inv); Batches = [int]$f[$c['batches']] }
            if ($row.Eye -eq 'L') { $left = $row; continue }
            if ($row.Eye -eq 'R' -and $left -and $left.Frame + 1 -eq $row.Frame) {
                if ($left.Batches -gt 0) { $tickGpu.Add($left.Busy + $row.Busy) }
                if ($left.Cpu -gt 0 -and $row.Cpu -gt 0) { $tickCpu.Add($left.Cpu + $row.Cpu) }
            }
            $left = $null
        }
    }
    $r.GpuWindows = $gpuWindows
    $r.TickCpu = Get-QaBenchStats $tickCpu
    $r.TickGpu = Get-QaBenchStats $tickGpu
    # Ticks longer than a 72 Hz period (13.9 ms): what a 72 Hz headset would miss with the same work (an inference: at
    # 72 Hz the hold would also wait less).
    $r.TicksOver72Pct = if ($tickCpu.Count) { 100.0 * @($tickCpu | Where-Object { $_ -gt 1000.0 / 72 }).Count / $tickCpu.Count } else { [double]::NaN }
    $r.GpuOver72Pct = if ($tickGpu.Count) { 100.0 * @($tickGpu | Where-Object { $_ -gt 1000.0 / 72 }).Count / $tickGpu.Count } else { [double]::NaN }
    $r.Start = $Ctx.BenchStart.ToString('o'); $r.End = $Ctx.BenchEnd.ToString('o')

    # Frames table: the whole 10 s xr windows inside the route, by the cumulative XR frame count of each xr line.
    $xr = @(Get-QaBenchLines $lines 'xr: (\d+) frame\(s\), \d+ new image')
    $prevT = $null; $prevN = 0; $xrFrom = -1; $xrTo = -1; $xrWindows = 0; $winFrom = $null; $winTo = $null
    foreach ($x in $xr) {
        $n = [long]$x.M.Groups[1].Value
        if ($null -ne $prevT -and $prevT -ge $from -and $x.T -le $to + 0.5 -and $xrWindows -lt 6) {
            if ($xrFrom -lt 0) { $xrFrom = $prevN; $winFrom = $prevT }
            $xrTo = $n; $winTo = $x.T; $xrWindows++
        }
        $prevT = $x.T; $prevN = $n
    }
    $r.XrWindows = $xrWindows
    $interval = New-Object 'System.Collections.Generic.List[double]'
    $shown = New-Object 'System.Collections.Generic.List[double]'
    $poseAge = New-Object 'System.Collections.Generic.List[double]'
    $frames = 0; $missed = 0; $tracked = 0; $periodSum = 0.0; $skips = 0
    $ft = if ($procId) { Read-QaBenchCsv (Join-Path $logDir "eternalvr-frames-$procId.csv") } else { $null }
    if ($ft -and $xrFrom -ge 0) {
        $c = $ft.Cols; $prevTime = $null; $lastNew = $null; $period = 0.0; $firstTime = $null
        foreach ($line in $ft.Rows) {
            $f = $line -split ','
            if ($f.Count -lt $c.Count) { continue }
            $n = [long]$f[$c['xr_frame']]
            if ($n -le $xrFrom -or $n -gt $xrTo) { continue }
            $time = [double]$f[$c['display_time_ns']] / 1e6
            $period = [double]::Parse($f[$c['period_ms']], $inv)
            $frames++; $periodSum += $period
            if ($null -ne $prevTime) {
                $d = $time - $prevTime
                $interval.Add($d)
                if ($d -gt 1.5 * $period) { $skips++ }
            }
            $prevTime = $time
            if ($null -eq $firstTime) { $firstTime = $time }
            if ($f[$c['repeat']] -eq 'new') {
                if ($null -ne $lastNew) { $shown.Add($time - $lastNew) }
                $lastNew = $time
            } else { $missed++ }
            if ([long]$f[$c['view']] -ne 0) { $tracked++; $poseAge.Add([double]::Parse($f[$c['pose_age_ms']], $inv)) }
        }
    }
    $r.XrFrames = $frames
    $r.PeriodMs = if ($frames) { $periodSum / $frames } else { [double]::NaN }
    $r.MissedPct = if ($frames) { 100.0 * $missed / $frames } else { [double]::NaN }
    # The display periods of the windows (the runtime's own skips count: a headset shows no new image in them either),
    # and how many of them showed no new image: the missed headset frames at the runtime's rate.
    $r.Periods = if ($frames -gt 1) { [int][Math]::Round(($prevTime - $firstTime) / $r.PeriodMs) + 1 } else { 0 }
    $r.MissedPeriodsPct = if ($r.Periods) { 100.0 * ($r.Periods - ($frames - $missed)) / $r.Periods } else { [double]::NaN }
    $r.TrackedPct = if ($frames) { 100.0 * $tracked / $frames } else { [double]::NaN }
    $r.RuntimeSkips = $skips
    $r.XrInterval = Get-QaBenchStats $interval
    $r.Shown = Get-QaBenchStats $shown
    $r.PoseAge = Get-QaBenchStats $poseAge

    # The 10 s rates, VRAM and CPU lines of the same windows.
    $inWin = { param($t) $null -ne $winFrom -and $t -gt $winFrom + 0.5 -and $t -le $winTo + 0.5 }
    $rates = @(Get-QaBenchLines $lines 'rates: game ([\d.]+) present\(s\)/s, ([\d.]+) tick\(s\)/s, ([\d.]+) stereo pair\(s\)/s shown; XR ([\d.]+) frame\(s\)/s, ([\d.]+) new image' | Where-Object { & $inWin $_.T })
    $avg = { param($rows, $g) if (@($rows).Count) { (@($rows) | ForEach-Object { [double]::Parse($_.M.Groups[$g].Value, $inv) } | Measure-Object -Average).Average } else { [double]::NaN } }
    $r.Ticks = & $avg $rates 2
    $r.TicksMin = if ($rates.Count) { (@($rates) | ForEach-Object { [double]::Parse($_.M.Groups[2].Value, $inv) } | Measure-Object -Minimum).Minimum } else { [double]::NaN }
    $r.Pairs = & $avg $rates 3
    $r.XrRate = & $avg $rates 4
    $r.NewImages = & $avg $rates 5
    $vram = @(Get-QaBenchLines $lines 'vram: the process uses (\d+) MB of local video memory, budget (\d+) MB .*peak (\d+) MB' | Where-Object { & $inWin $_.T })
    $r.VramPeakMb = if ($vram.Count) { (@($vram) | ForEach-Object { [int]$_.M.Groups[3].Value } | Measure-Object -Maximum).Maximum } else { 0 }
    $r.VramBudgetMb = if ($vram.Count) { [int]$vram[-1].M.Groups[2].Value } else { 0 }
    $cpu = @(Get-QaBenchLines $lines 'cpu: CPU per tick by thread: whole process ([\d.]+) ms \(([\d.]+) cores busy\)' | Where-Object { & $inWin $_.T })
    $r.ProcessCpuMs = & $avg $cpu 1
    $r.CoresBusy = & $avg $cpu 2
    $outside = @(Get-QaBenchLines $lines 'outside the frame-end jobs \(game frame \+ eye L views\) ([\d.]+)/' | Where-Object { & $inWin $_.T })
    $r.OutsideMs = & $avg $outside 1
    $eyeR = @(Get-QaBenchLines $lines 'eye R render ([\d.]+)/' | Where-Object { & $inWin $_.T })
    $r.EyeRMs = & $avg $eyeR 1
    $held = @(Get-QaBenchLines $lines 'pace: a runtime menu is over the game: the game is held' | Where-Object { & $inWin $_.T })
    $r.UnfocusedHold = $held.Count

    # Where the tick's time goes: the GPU's busy share of the tick's CPU time (both means).
    $r.GpuShare = if ($r.TickCpu.N -and $r.TickGpu.N) { $r.TickGpu.Mean / $r.TickCpu.Mean } else { [double]::NaN }
    # GPU-bound when the GPU is busy for most of the tick; otherwise the CPU side (the game's frame and the two eyes'
    # views, docs/rig-findings/perf-baselines.md) or the headset's hold sets the tick.
    $r.Bound = if ([double]::IsNaN($r.GpuShare)) { 'unknown' } elseif ($r.GpuShare -ge 0.85) { 'GPU' } else { 'CPU' }
    $r.AboveRefresh = $r.Ticks -gt 1000.0 / $r.PeriodMs
    $r.Over250 = @($interval | Where-Object { $_ -gt 250 }).Count + @($shown | Where-Object { $_ -gt 250 }).Count

    $line = ('bench: {0} {1} (asked {2}{3}) RT {4}, {5:N2} ms period: {6:N1} ticks/s (lowest 10 s {7:N1}); tick CPU p50/p95/p99/mean {8} ms; ' +
             'GPU busy per tick {9} ms (GPU {10:P0} of the tick, {11}-bound); shown image p50/p95/p99/mean {12} ms; ' +
             'display periods without a new image {26:N1}% of {27} (XR frames {13:N1}% of {14}, runtime skips {15}); pose age {16} ms; VRAM peak {17} of {18} MB; ' +
             'process CPU {19:N1} ms per tick ({20:N1} cores), game frame + eye L views {24:N1} ms and eye R render {25:N1} ms wall; ticks over 13.9 ms (a 72 Hz period) {28:N1}%, GPU over it {29:N1}%; frames over 250 ms {21}; windows {22} gpu / {23} xr') -f
        $Ctx.Name, $r.Size, $r.Asked, $(if ($r.Recommended) { ", runtime recommends $($r.Recommended)" } else { '' }),
        $(if ($r.RtOff) { 'off' } else { 'NOT held off' }), $r.PeriodMs, $r.Ticks, $r.TicksMin, (Format-QaBenchStats $r.TickCpu),
        (Format-QaBenchStats $r.TickGpu), $r.GpuShare, $r.Bound, (Format-QaBenchStats $r.Shown), $r.MissedPct, $r.XrFrames,
        $r.RuntimeSkips, (Format-QaBenchStats $r.PoseAge), $r.VramPeakMb, $r.VramBudgetMb, $r.ProcessCpuMs, $r.CoresBusy,
        $r.Over250, $r.GpuWindows, $r.XrWindows, $r.OutsideMs, $r.EyeRMs, $r.MissedPeriodsPct, $r.Periods, $r.TicksOver72Pct, $r.GpuOver72Pct
    $r.Line = $line
    Add-QaNote $Ctx $line
    # The tables beside the log copy (the stage's folder is emptied by the next run of the scenario); the
    # release-to-present and per-tick CPU tables only when a rig switch wrote them (ETERNALVR_TEST_LATENCY_CSV,
    # ETERNALVR_TEST_CPU_CSV).
    foreach ($t in "eternalvr-gpu-$procId.csv", "eternalvr-frames-$procId.csv", "eternalvr-latency-$procId.csv",
        "eternalvr-cpu-$procId.csv") {
        Copy-Item -LiteralPath (Join-Path $logDir $t) -Destination $Ctx.Dir -ErrorAction SilentlyContinue
    }
    ($r | ConvertTo-Json -Depth 4) | Set-Content -LiteralPath (Join-Path $Ctx.Dir 'bench.json') -Encoding utf8
    Add-QaResult $Ctx 'bench: six 10 s windows measured in the route (GPU and frames tables)' ($gpuWindows -eq 6 -and $xrWindows -eq 6 -and $r.TickCpu.N -gt 0 -and $frames -gt 0) "$gpuWindows gpu, $xrWindows xr window(s); $($r.TickCpu.N) tick(s), $frames XR frame(s)"
    Add-QaResult $Ctx 'bench: ray tracing held off' $r.RtOff ''
    Add-QaResult $Ctx 'bench: the eye views head-tracked through the route (95% or more of the XR frames)' ($r.TrackedPct -ge 95) ('{0:N1}%' -f $r.TrackedPct)
}

# ---- The routes -------------------------------------------------------------------------------------------------

# A route as a scenario: its map (or menu start), the debug schedule that places the player, the setup before the
# script (a scriptblock run once in the map), and the script's cycle.
function New-QaBenchScenario([string]$Name, [string]$Size, [hashtable]$Route) {
    $s = @{
        Name = $Name; Kind = 'game'; OnlyByName = $true; BenchSize = $Size; BenchRoute = $Route
        # The game saves the held RT off into the profile (qa-cloud.ps1 takes it as this scenario's own, and the cloud
        # files go back before the next scenario).
        ProfileHeld = @('r_useRayTracing')
        Proves = "Benchmark route $($Route.Title) at render size $Size (docs/rig-findings/bench-routes.md): the launcher's default settings, RT off, $script:QaBenchSeconds s of scripted input, measured over six whole 10 s windows; a summary line"
        Env = @($script:QaBenchLauncherEnv) + @("ETERNALVR_RENDER_SIZE=$Size") + @($script:QaBenchMeasureEnv) +
              @("ETERNALVR_DEBUG_COMMANDS=$($Route.Schedule)")
        Input = @('right.aim = 0, -5')
        Timeline = {
            param($c)
            $c.BenchSize = $c.Scenario.BenchSize
            Set-QaBenchPriority $c
            Add-QaNote $c "game modules from outside the Windows folder: $(Get-QaCoexistModules)"
            if (-not (& $c.Scenario.BenchRoute.Setup $c)) { return }
            Save-QaXrShot $c 'start'
            Invoke-QaBenchScript $c $c.Scenario.BenchRoute.Cycle
            Write-QaInput $c @('right.aim = 0, -5')
            Save-QaXrShot $c 'end'
            # The last 10 s lines of the route are written up to 10 s after it.
            Wait-QaSeconds $c 11
        }
        Asserts = {
            param($c)
            $c.BenchSize = $c.Scenario.BenchSize
            Test-QaPresent $c 'stereo: eye views'
            Test-QaAbsent $c 'ERROR|exception|crash' -Regex -CaseSensitive
            Measure-QaBench $c
        }
    }
    foreach ($k in 'MenuMission', 'MenuSlot', 'MenuMap', 'MenuSaveNote', 'Map') { if ($Route.ContainsKey($k)) { $s[$k] = $Route[$k] } }
    return $s
}

# Combat arena: Cultist Base's Slayer Gate. The checkpoint cp_03_shoot_gate, the player teleported to the gate's
# golden gore nest and Use pressed on it (the hand aimed at it: tried at a list of aims until the nest's sync starts);
# 2.5 s after the sync the game itself teleports the player into the gate's arena, whose encounter starts. Then bursts
# of fire and two quarter turns a cycle, god mode on.
$script:QaBenchGateAims = @(@(-90, -25), @(0, -80), @(0, -30), @(0, 0), @(45, -25), @(-45, -25), @(90, -25), @(135, -25), @(-135, -25), @(180, -25))
$script:QaBenchRoutes = [ordered]@{
    combat = @{
        Title = 'combat arena (Cultist Base, the Slayer Gate)'
        Map = 'game/sp/e1m3_cult/e1m3_cult -checkpoint cp_03_shoot_gate'
        Schedule = "3:god;$script:QaBenchTutorials|5:teleport interact_slayer_gate_nest_1"
        Cycle = $script:QaBenchFightCycle
        Setup = {
            param($c)
            if (-not (Wait-QaLog $c 'debug-commands: [\d.]+ s: teleport interact_slayer_gate_nest_1' 60)) { Add-QaNote $c 'bench: the teleport to the golden nest never ran'; return $false }
            Wait-QaSeconds $c 1.5
            $before = [regex]::Matches((Get-QaLogText $c), 'golden_gore_nest_sync').Count
            foreach ($a in $script:QaBenchGateAims) {
                Write-QaInput $c @("right.aim = $($a[0]), $($a[1])"); Wait-QaSeconds $c 1.2
                Write-QaInput $c @("right.aim = $($a[0]), $($a[1])", 'right.click = 1'); Wait-QaSeconds $c 0.3
                Write-QaInput $c @("right.aim = $($a[0]), $($a[1])"); Wait-QaSeconds $c 0.6
                if ([regex]::Matches((Get-QaLogText $c), 'golden_gore_nest_sync').Count -gt $before) {
                    Add-QaNote $c "bench: the golden nest's sync started with Use at aim ($($a[0]), $($a[1]))"
                    Write-QaInput $c @('right.aim = 0, -5')
                    # The sync (2.2 s), the game's teleport into the arena 2.5 s after it, the encounter's first spawns.
                    Wait-QaSeconds $c 10
                    return $true
                }
            }
            Add-QaResult $c 'bench: the golden nest took Use' $false "no golden_gore_nest_sync after $($script:QaBenchGateAims.Count) aims"
            return $false
        }
    }
    # Outdoor: Exultia's opening (the suite's own map start): 1.2 s of walking out of the start room's opening onto the
    # balcony, then the long view over the valley to the citadel and the mountains, looking ahead, right and left.
    outdoor = @{
        Title = 'outdoor (Exultia, the map start)'
        Map = 'game/sp/e1m2_battle/e1m2_battle'
        Schedule = "3:god;$script:QaBenchTutorials"
        Cycle = $script:QaBenchVistaCycle
        Setup = {
            param($c)
            Wait-QaSeconds $c 6
            Write-QaInput $c @('right.aim = 0, -5', 'left.stick = 0, 1'); Wait-QaSeconds $c 1.2
            Write-QaInput $c @('right.aim = 0, -5'); Wait-QaSeconds $c 2
            return $true
        }
    }
    # Hub: the late Fortress of Doom, Game Slot 1's Continue Game (the 100% save), a still player turning.
    hub = @{
        Title = 'hub (the Fortress of Doom, Game Slot 1 Continue Game)'
        MenuMission = 'continue'; MenuSlot = 1; MenuMap = 'game/hub/hub'
        MenuSaveNote = 'the 100% save (a finished campaign: Continue Game loads the late Fortress of Doom)'
        Schedule = "3:$script:QaBenchTutorials"
        Cycle = $script:QaBenchLookCycle
        Setup = { param($c) Wait-QaSeconds $c 8; return $true }
    }
}

$benchScenarios = @()
foreach ($k in $script:QaBenchRoutes.Keys) {
    $benchScenarios += (New-QaBenchScenario "bench-$k" 'auto' $script:QaBenchRoutes[$k])
    $benchScenarios += (New-QaBenchScenario "bench-$k-q3" $script:QaBenchQuest3 $script:QaBenchRoutes[$k])
}

# Before the launcher's end-to-end suite, which stays last.
$e2e = @($script:QaScenarios | Where-Object { $_.Kind -eq 'e2e' })
$script:QaScenarios = @($script:QaScenarios | Where-Object { $_.Kind -ne 'e2e' }) + $benchScenarios + $e2e
