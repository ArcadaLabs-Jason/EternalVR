<#
.SYNOPSIS
The benchmark routes' runs as one Markdown table: each route and size with the mean over its runs and the spread
(lowest to highest run), from the bench.json files the bench scenarios write (qa-scenarios-bench.ps1).

.EXAMPLE
powershell -NoProfile -File tools\rig\qa\qa-bench-table.ps1 runs\QA-b1 runs\QA-b2 runs\QA-b3
#>
param([Parameter(Mandatory, ValueFromRemainingArguments)][string[]]$Dirs)
$ErrorActionPreference = 'Stop'
$inv = [Globalization.CultureInfo]::InvariantCulture

$runs = foreach ($d in $Dirs) {
    Get-ChildItem -LiteralPath $d -Recurse -Filter 'bench.json' | ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json }
}
if (-not $runs) { throw "no bench.json under $($Dirs -join ', ')" }

# 'mean (lowest-highest)' of one value over the runs.
function Format-Spread([double[]]$Values, [string]$Fmt = 'N1') {
    $v = @($Values | Where-Object { -not [double]::IsNaN($_) })
    if ($v.Count -eq 0) { return '-' }
    $m = $v | Measure-Object -Average -Minimum -Maximum
    if ($v.Count -eq 1) { return $m.Average.ToString($Fmt, $inv) }
    return '{0} ({1}-{2})' -f $m.Average.ToString($Fmt, $inv), $m.Minimum.ToString($Fmt, $inv), $m.Maximum.ToString($Fmt, $inv)
}

'| Route | Size per eye | Runs | Ticks/s | Tick CPU p50 / p99 ms | GPU busy per tick p50 / p99 ms | GPU share | Shown image p99 ms | Periods without a new image % | Ticks / GPU over 13.9 ms % | Pose age mean / p99 ms | VRAM peak MB | Bound |'
'|---|---|---|---|---|---|---|---|---|---|---|---|---|'
foreach ($g in ($runs | Group-Object Scenario | Sort-Object Name)) {
    $r = @($g.Group)
    $sizes = (@($r | ForEach-Object { $_.Size }) | Sort-Object -Unique) -join ', '
    $bound = (@($r | ForEach-Object { $_.Bound }) | Group-Object | ForEach-Object { "$($_.Name) $($_.Count)" }) -join ', '
    '| {0} | {1} (asked {2}) | {3} | {4} | {5} / {6} | {7} / {8} | {9} | {10} | {11} | {16} / {17} | {12} / {13} | {14} | {15} |' -f $g.Name, $sizes,
        $r[0].Asked, $r.Count,
        (Format-Spread ($r | ForEach-Object { $_.Ticks })),
        (Format-Spread ($r | ForEach-Object { $_.TickCpu.P50 })), (Format-Spread ($r | ForEach-Object { $_.TickCpu.P99 })),
        (Format-Spread ($r | ForEach-Object { $_.TickGpu.P50 })), (Format-Spread ($r | ForEach-Object { $_.TickGpu.P99 })),
        (Format-Spread ($r | ForEach-Object { 100 * $_.GpuShare }) 'N0'),
        (Format-Spread ($r | ForEach-Object { $_.Shown.P99 })),
        (Format-Spread ($r | ForEach-Object { if ($null -ne $_.MissedPeriodsPct) { $_.MissedPeriodsPct } else { $_.MissedPct } })),
        (Format-Spread ($r | ForEach-Object { $_.PoseAge.Mean })), (Format-Spread ($r | ForEach-Object { $_.PoseAge.P99 })),
        (Format-Spread ($r | ForEach-Object { $_.VramPeakMb }) 'N0'), $bound,
        (Format-Spread ($r | ForEach-Object { $_.TicksOver72Pct })), (Format-Spread ($r | ForEach-Object { $_.GpuOver72Pct }))
}
