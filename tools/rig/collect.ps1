<#
.SYNOPSIS
Collects a run's artefacts into its folder: a screenshot of the virtual display (of the primary display
when the virtual one is not active) in screenshots\, and the game's log files written since the run
started in logs\.
#>
param(
    [string]$Run = 'latest'
)

. (Join-Path $PSScriptRoot 'common.ps1')

try {
    $cfg = Get-RigConfig
    $runDir = Resolve-RigRunDir $cfg $Run
} catch {
    Write-Host "collect.ps1: $($_.Exception.Message)"
    exit 1
}
Set-RigLogFiles @(Join-Path $runDir 'rig.log')
$run = Read-RigJson (Join-Path $runDir 'run.json')
$status = 0

# Screenshot
try {
    Add-Type -AssemblyName System.Drawing
    Add-Type -AssemblyName System.Windows.Forms
    Initialize-RigNative
    $state = Get-RigDisplayState $cfg
    if ($state.Active -and $state.Rect) {
        $r = $state.Rect
        $which = 'virtual display'
    } else {
        $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
        $r = [pscustomobject]@{ x = $b.X; y = $b.Y; width = $b.Width; height = $b.Height }
        $which = 'primary display'
    }
    $dir = Join-Path $runDir 'screenshots'
    New-RigDirectory $cfg $dir
    $file = Join-Path $dir ('{0}.png' -f (Get-Date).ToString('yyyyMMdd-HHmmss-fff'))
    Assert-RigWritable $cfg $file
    $bmp = New-Object System.Drawing.Bitmap ([int]$r.width), ([int]$r.height)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    try {
        $g.CopyFromScreen([int]$r.x, [int]$r.y, 0, 0, $bmp.Size)
        $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $g.Dispose()
        $bmp.Dispose()
    }
    Write-RigLog "screenshot of the $which saved: $file"
} catch {
    Write-RigLog "screenshot failed: $($_.Exception.Message)" 'WARN'
    $status = 1
}

# Game logs written since the run started
$since = [datetime]::MinValue
if ($run -and (Get-RigProp $run 'launchTime')) { $since = ([datetime](Get-RigProp $run 'launchTime')).AddSeconds(-5) }
$gameRoot = $cfg.GameRoot
$exe = Get-RigProp $run 'exe'
if ($exe -and (Get-RigProp $exe 'workingDirectory')) { $gameRoot = $exe.workingDirectory }
$candidates = @()
foreach ($d in @($gameRoot, (Join-Path $gameRoot 'base'), (Join-Path $cfg.SavedGamesDir 'base'))) {
    if (Test-Path -LiteralPath $d) {
        $candidates += @(Get-ChildItem -LiteralPath $d -File -Filter '*.log' -ErrorAction SilentlyContinue | Where-Object { $_.LastWriteTime -ge $since })
    }
}
if ($candidates.Count -gt 0) {
    $logs = Join-Path $runDir 'logs'
    New-RigDirectory $cfg $logs
    foreach ($f in $candidates) {
        $dest = Join-Path $logs $f.Name
        if (Test-Path -LiteralPath $dest) { $dest = Join-Path $logs ("{0}-{1}" -f (Split-Path -Leaf $f.DirectoryName), $f.Name) }
        Assert-RigWritable $cfg $dest
        Copy-Item -LiteralPath $f.FullName -Destination $dest -Force
        Write-RigLog "log copied: $($f.FullName)"
    }
} else {
    Write-RigLog 'no game logs written since the run started'
}
exit $status
