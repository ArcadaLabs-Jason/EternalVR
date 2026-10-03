<#
.SYNOPSIS
Automated rig QA: simulator runs of a layer build with scripted input, screenshots and layer-log assertions,
plus the launcher's end-to-end suite; writes <Out>\report.md.

.DESCRIPTION
Each game scenario stages the layer build, starts Route S stereo on OpenXR-Simulator through launch-ht.ps1
(the game window and the mirror on the virtual display), waits until the player is in the map, runs its
timeline (scripted input through ETERNALVR_TEST_INPUT, screenshots at named checkpoints), stops the run
with stop.ps1 -RestoreCloudFiles, restores the 100% save into GAME-AUTOSAVE0 (always, also on failure) and
then asserts on the layer log. See tools\rig\qa\README.md.

Before every scenario: <workspace>\tmp-vr\STOP-RIG ends the suite at once (the owner is using
the PC; it is also checked during every wait), and a running DOOMEternalx64vk.exe refuses it.

  powershell -NoProfile -ExecutionPolicy Bypass -File tools\rig\qa\qa-suite.ps1 -LayerSrc <build>\src\vkcore
  ... -Only baseline,token-pickup

Exit codes: 0 every scenario run passed, 1 a scenario failed, 2 refused or stopped (STOP-RIG, game running,
save restore failed).

.PARAMETER LayerSrc
Folder with EternalVR.dll, VK_LAYER_ETERNALVR.json and openxr_loader.dll (default: the repository's
build\windows-msvc\src\vkcore).

.PARAMETER Out
Report folder (default E:\Code\resources\Testing\QA-<yyyyMMdd-HHmm>): report.md, qa.log, the PNG
screenshots, and per scenario a folder with the layer log copy, launch.txt and stop.txt.

.PARAMETER Only
Scenario names to run (default: all, in order).

.PARAMETER LauncherBin
The launcher's Release bin folder (<launcher>\src\EternalVR.Launcher\bin\Release) for launcher-e2e; without
it the scenario uses the repository's own bin\Release if it exists and is skipped otherwise.

.PARAMETER DlssDll
A newer nvngx_dlss.dll for every game scenario, passed as the launcher passes it (ETERNALVR_DLSS_DLL with
ETERNALVR_DLSS_PRESET=K): the route most players are on, which installs two more inline hooks. Run the
suite once without and once with it before a release or a headset package.

.PARAMETER GameEnv
NAME=VALUE pairs for every game scenario, after -DlssDll's (a test knob such as
ETERNALVR_TEST_HIDE_KHR_MAINTENANCE=1).
#>
param(
    [string]$LayerSrc = '',
    [string]$Out = '',
    [string[]]$Only = @(),
    [string]$LauncherBin = '',
    [string]$DlssDll = '',
    [string[]]$GameEnv = @()
)
$ErrorActionPreference = 'Continue'
. (Join-Path $PSScriptRoot 'qa-common.ps1')
. (Join-Path $PSScriptRoot 'qa-scenarios.ps1')

if (-not $LayerSrc) { $LayerSrc = Join-Path $script:QaRepo 'build\windows-msvc\src\vkcore' }
if (-not $Out) { $Out = "E:\Code\resources\Testing\QA-$((Get-Date).ToString('yyyyMMdd-HHmm'))" }
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path -LiteralPath $Out).Path
$script:QaLogFile = Join-Path $Out 'qa.log'

# -Only from another shell with -File arrives as one comma-joined string.
$Only = @($Only | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
$names = @($script:QaScenarios | ForEach-Object { $_.Name })
foreach ($n in $Only) { if ($names -notcontains $n) { throw "unknown scenario '$n' (scenarios: $($names -join ', '))" } }
$selected = @($script:QaScenarios | Where-Object { $Only.Count -eq 0 -or $Only -contains $_.Name })

$needsGame = @($selected | Where-Object { $_.Kind -eq 'game' }).Count -gt 0
if ($needsGame) {
    foreach ($f in 'EternalVR.dll', 'VK_LAYER_ETERNALVR.json', 'openxr_loader.dll') {
        if (-not (Test-Path -LiteralPath (Join-Path $LayerSrc $f))) { throw "no $f in $LayerSrc (-LayerSrc)" }
    }
    $LayerSrc = (Resolve-Path -LiteralPath $LayerSrc).Path
    if (-not (Test-Path -LiteralPath $script:QaSimJson)) { throw "no simulator runtime manifest at $script:QaSimJson" }
    if ($DlssDll) {
        if (-not (Test-Path -LiteralPath $DlssDll)) { throw "no $DlssDll (-DlssDll)" }
        $DlssDll = (Resolve-Path -LiteralPath $DlssDll).Path
        $script:QaExtraEnv = @("ETERNALVR_DLSS_DLL=$DlssDll", 'ETERNALVR_DLSS_PRESET=K')
    }
    # -GameEnv from another shell with -File arrives as one comma-joined string.
    $script:QaExtraEnv += @($GameEnv | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
}

# One simulator run; the stop and the save restore always happen once the launch was attempted.
function Invoke-QaGameScenario($Scenario, $Ctx) {
    $launched = $false
    try {
        New-QaStage $Ctx $LayerSrc
        if ($null -ne $Scenario.Input) { Write-QaInput $Ctx @($Scenario.Input) }
        Start-QaDumpWatch $Ctx
        $launched = $true
        Start-QaGame $Ctx $Scenario
        Write-QaLine "  run: $($Ctx.RunDir)"
        $inMap = Wait-QaLog $Ctx $script:QaPlayMarker 200
        Add-QaResult $Ctx 'in the map' $inMap "layer log matches '$script:QaPlayMarker' within 200 s"
        if ($inMap) { & $Scenario.Timeline $Ctx }
    } catch [OperationCanceledException] {
        $Ctx.Stopped = $true
        Add-QaResult $Ctx 'not stopped by STOP-RIG' $false $_.Exception.Message
    } catch {
        Add-QaResult $Ctx 'run completed' $false $_.Exception.Message
    } finally {
        if ($launched) {
            try { Stop-QaGame $Ctx } catch { Add-QaResult $Ctx 'stopped and cleaned up' $false $_.Exception.Message }
            $save = Restore-QaSave
            Add-QaResult $Ctx 'save restored' $save.Ok $save.Detail
            if (-not $save.Ok) { $Ctx.Abort = $true }
            Stop-QaDumpWatch $Ctx
        }
    }
    if (-not $launched) { return }
    $log = Get-QaLayerLog $Ctx
    if ($log) {
        Copy-Item -LiteralPath $log (Join-Path $Ctx.Dir 'eternalvr.log') -ErrorAction SilentlyContinue
        Add-QaNote $Ctx "layer log $log"
        & $Scenario.Asserts $Ctx
        # Every scenario: a full hook pool turns a feature off with only this line to show for it.
        Test-QaAbsent $Ctx 'no free (inline )?hook slot' -Regex
    } else {
        Add-QaResult $Ctx 'layer log found' $false "no eternalvr-2*.log in $($Ctx.LogsDir) or the run folder"
    }
    $dumps = @(Get-ChildItem -LiteralPath "$($Ctx.Stage)-dumps" -Filter '*.dmp' -ErrorAction SilentlyContinue)
    if (Test-Path -LiteralPath $script:QaProcDump) {
        Add-QaResult $Ctx 'no crash dump' ($dumps.Count -eq 0) "$($dumps.Count) in $($Ctx.Stage)-dumps"
    }
}

# The launcher's end-to-end suite against a Release build (no game; nothing outside -Root is written).
function Invoke-QaE2eScenario($Ctx) {
    $bin = $LauncherBin
    if (-not $bin) {
        $bin = Join-Path $script:QaRepo 'launcher\src\EternalVR.Launcher\bin\Release'
        if (-not (Test-Path -LiteralPath $bin)) { $Ctx.Skipped = $true; Add-QaNote $Ctx "skipped: no $bin and no -LauncherBin"; return }
    }
    if (-not (Test-Path -LiteralPath $bin)) { Add-QaResult $Ctx 'launcher bin folder' $false "no $bin"; return }
    $bin = (Resolve-Path -LiteralPath $bin).Path
    # e2e.ps1 finds the launcher and the stand-in game from its own folder: <launcher>\tests\e2e.ps1 for
    # <launcher>\src\EternalVR.Launcher\bin\<Configuration>.
    $launcherDir = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $bin)))
    $e2e = Join-Path $launcherDir 'tests\e2e.ps1'
    if (-not (Test-Path -LiteralPath $e2e)) { Add-QaResult $Ctx 'e2e.ps1 found' $false "no $e2e for $bin"; return }
    $root = Join-Path $Ctx.Out 'e2e'
    Write-QaLine "  $e2e -Root $root"
    $text = & powershell -NoProfile -ExecutionPolicy Bypass -File $e2e -Root $root -Runtime $script:QaSimJson `
        -Configuration (Split-Path -Leaf $bin) 2>&1 | ForEach-Object { "$_" }
    Set-Content -LiteralPath (Join-Path $Ctx.Dir 'e2e.txt') -Value $text -Encoding utf8
    $failed = @($text | Where-Object { $_ -match '^\s*FAIL ' })
    Add-QaResult $Ctx 'E2E OK' ([bool]($text | Where-Object { $_ -match 'E2E OK' })) "$($failed.Count) failed check(s); e2e.txt"
}

function Format-QaCell([string]$Text) { return ($Text -replace '\|', '\|' -replace "`r?`n", ' ') }

function Write-QaReport($Records, $Pending) {
    $ran = @($Records | Where-Object { -not $_.Skipped })
    $passed = @($ran | Where-Object { $_.Pass }).Count
    $md = New-Object System.Collections.Generic.List[string]
    $md.Add("# EternalVR rig QA $((Split-Path -Leaf $Out))")
    $md.Add('')
    $md.Add("Layer: ``$LayerSrc``  ")
    if ($DlssDll) { $md.Add("DLSS DLL: ``$DlssDll`` (preset K)  ") }
    $md.Add("Started: $($script:QaStarted.ToString('yyyy-MM-dd HH:mm')); repository: ``$script:QaRepo`` ($script:QaCommit)")
    $md.Add('')
    $md.Add('| Scenario | Result | Duration | Run folder | Screenshots |')
    $md.Add('|---|---|---|---|---|')
    foreach ($r in $Records) {
        $result = if ($r.Skipped) { 'SKIP' } elseif ($r.Pass) { 'PASS' } else { 'FAIL' }
        $shots = (@($r.Ctx.Shots) | ForEach-Object { Split-Path -Leaf $_ }) -join ', '
        $run = if ($r.Ctx.RunDir) { Split-Path -Leaf $r.Ctx.RunDir } else { '' }
        $md.Add("| $($r.Ctx.Name) | $result | $([int]$r.Seconds) s | $run | $shots |")
    }
    foreach ($p in $Pending) { $md.Add("| $($p.Name) | NOT RUN | | | |") }
    foreach ($r in $Records) {
        $md.Add('')
        $md.Add("## $($r.Ctx.Name)")
        $md.Add('')
        $md.Add($r.Proves)
        $md.Add('')
        foreach ($a in $r.Ctx.Results) {
            $md.Add("- $(if ($a.Ok) { 'PASS' } else { 'FAIL' }) $(Format-QaCell $a.Name)$(if ($a.Detail) { ': ' + (Format-QaCell $a.Detail) })")
        }
        foreach ($n in $r.Ctx.Notes) { $md.Add("- note: $(Format-QaCell $n)") }
        foreach ($s in $r.Ctx.Shots) { $md.Add("- screenshot: ``$s``") }
        if ($r.Ctx.RunDir) { $md.Add("- run folder: ``$($r.Ctx.RunDir)``") }
    }
    $md.Add('')
    $md.Add("QA: $passed of $($ran.Count) scenarios passed")
    $skipped = @($Records | Where-Object { $_.Skipped }).Count
    if ($skipped -or @($Pending).Count) { $md.Add("($skipped skipped, $(@($Pending).Count) not run)") }
    Set-Content -LiteralPath (Join-Path $Out 'report.md') -Value $md -Encoding utf8
    return "QA: $passed of $($ran.Count) scenarios passed"
}

$script:QaStarted = Get-Date
$script:QaCommit = (& git -C $script:QaRepo rev-parse --short HEAD 2>$null)
Write-QaLine "QA suite: $($selected.Count) scenario(s) into $Out; layer $LayerSrc$(if ($DlssDll) { "; DLSS DLL $DlssDll" })"
$records = New-Object System.Collections.ArrayList
$exitCode = 0
for ($i = 0; $i -lt $selected.Count; ++$i) {
    $sc = $selected[$i]
    if ($sc.Kind -eq 'game') {
        try { Assert-QaCanStart } catch {
            Write-QaLine "suite ends before '$($sc.Name)': $($_.Exception.Message)"
            $exitCode = 2
            break
        }
    } elseif (Test-QaStopRequested) {
        Write-QaLine "suite ends before '$($sc.Name)': STOP-RIG"
        $exitCode = 2
        break
    }
    Write-QaLine "scenario $($sc.Name)"
    $ctx = New-QaContext $sc $Out
    $watch = [Diagnostics.Stopwatch]::StartNew()
    if ($sc.Kind -eq 'game') { Invoke-QaGameScenario $sc $ctx } else { Invoke-QaE2eScenario $ctx }
    $pass = -not $ctx.Skipped -and $ctx.Results.Count -gt 0 -and @($ctx.Results | Where-Object { -not $_.Ok }).Count -eq 0
    [void]$records.Add([pscustomobject]@{ Ctx = $ctx; Pass = $pass; Skipped = $ctx.Skipped; Seconds = $watch.Elapsed.TotalSeconds; Proves = $sc.Proves })
    Write-QaLine ("scenario {0}: {1} in {2:N0} s" -f $sc.Name, $(if ($ctx.Skipped) { 'SKIP' } elseif ($pass) { 'PASS' } else { 'FAIL' }), $watch.Elapsed.TotalSeconds)
    [void](Write-QaReport $records @($selected | Select-Object -Skip ($i + 1)))
    if ($ctx.Stopped -or $ctx.Abort) {
        Write-QaLine $(if ($ctx.Stopped) { 'suite ends: STOP-RIG' } else { 'suite ends: the save restore failed; check GAME-AUTOSAVE0 by hand' })
        $exitCode = 2
        break
    }
}
$summary = Write-QaReport $records @($selected | Where-Object { $n = $_.Name; -not ($records | Where-Object { $_.Ctx.Name -eq $n }) })
Write-QaLine $summary
Write-QaLine "report: $(Join-Path $Out 'report.md')"
if ($exitCode -eq 0 -and @($records | Where-Object { -not $_.Skipped -and -not $_.Pass }).Count -gt 0) { $exitCode = 1 }
exit $exitCode
