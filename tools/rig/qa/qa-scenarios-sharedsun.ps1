# Eye R on eye L's sun cascades under Route S (ETERNALVR_SHARED_SUN, src/vkcore/shared_sun_hooks.hpp;
# docs/rig-findings/perf-sun-cascades.md). Dot-sourced by qa-suite.ps1 after qa-scenarios-bench.ps1, whose routes and
# measurement it uses; by name only.
#
#   shared-sun        the combat bench route with ETERNALVR_SHARED_SUN=1: both hooks installed on this game build in
#                     mode shared, a 10 s line with stereo ticks in every window of the route, eye R keeping cascade 0
#                     (eye L's of the tick) in 90% or more of the ticks of every such line and listing fewer sun
#                     slices than eye L (its far cascade turns), nothing left off at the listing (that is the
#                     ceiling's), and the bench summary
#   sun-eyes-combat   still scenes with eye captures every 45 pairs, the head held still: the combat arena, the hub's
#   sun-eyes-hub      bridge and Exultia's start (the sun on the valley). The switch comes from -GameEnv
#   sun-eyes-outdoor  (ETERNALVR_SHARED_SUN=...; ETERNALVR_STEREO_SAME_VIEW=1 for both eyes on one view), so the same
#                     scenario runs each way; compare with tools/stereo/eye_diff.py

$script:QaSunCaptureEvery = 45
$script:QaSunStill = @('ETERNALVR_HEAD_POSITION=0', 'ETERNALVR_TEST_HEAD_SWAY=0,0,1')
$script:QaSunLine = 'seq-sun: last 10 s \((\w+)\): (\d+) stereo ticks; listed sun/spot/point/player: eye L (\d+)/\d+/\d+/\d+, eye R (\d+)/\d+/\d+/\d+, .* eye R''s sun checks (\d+), eye L''s of the tick (\d+), kept by cascade (\d+)/(\d+)/(\d+)/(\d+), left off at the listing (\d+)'

# The 10 s lines of the hooks: mode, ticks, the sun slices each eye listed, eye R's checks, eye L's of the tick (counter 1 and listed by eye L in the tick), the
# entries eye R kept by cascade and the slices left off at the listing.
function Get-QaSunLines($Ctx) {
    $rows = @()
    foreach ($line in @(Get-QaMatches $Ctx $script:QaSunLine -Regex)) {
        if ($line -notmatch $script:QaSunLine) { continue }
        $n = @(2..12 | ForEach-Object { [long]$Matches[$_] })
        $rows += [pscustomobject]@{ Mode = $Matches[1]; Ticks = $n[0]; SunLeft = $n[1]; SunRight = $n[2]; Checks = $n[3]
                                    Fresh = $n[4]; Kept = $n[5..8]; LeftOff = $n[9] }
    }
    return $rows
}

function Test-QaSharedSun($Ctx) {
    Test-QaPresent $Ctx 'seq-sun: shadow setup hooked (stale check RVA 0x1CFF33E, listing RVA 0x1CFF357), mode shared'
    Test-QaAbsent $Ctx 'seq-sun: .*(not installed|hook failed|not a known value)' -Regex
    $rows = @(Get-QaSunLines $Ctx | Where-Object { $_.Mode -eq 'shared' -and $_.Ticks -gt 100 })
    $kept0 = @($rows | Where-Object { $_.Kept[0] * 10 -ge $_.Ticks * 9 })
    $fewR = @($rows | Where-Object { $_.SunRight -lt $_.SunLeft })
    $leftOff = @($rows | Where-Object { $_.LeftOff -gt 0 })
    $text = @($rows | ForEach-Object { '{0} ticks, sun listed L {1} R {2}, kept {3}' -f $_.Ticks, $_.SunLeft, $_.SunRight, ($_.Kept -join '/') }) -join '; '
    Add-QaResult $Ctx 'a 10 s line with stereo ticks in mode shared, 6 or more' ($rows.Count -ge 6) "$($rows.Count) line(s): $text"
    Add-QaResult $Ctx 'eye R kept cascade 0 (eye L''s of the tick) in 90% of the stereo ticks or more, in every line' ($rows.Count -gt 0 -and $kept0.Count -eq $rows.Count) "$($kept0.Count) of $($rows.Count)"
    Add-QaResult $Ctx 'eye R listed fewer sun slices than eye L (its far cascade turns only), in every line' ($rows.Count -gt 0 -and $fewR.Count -eq $rows.Count) "$($fewR.Count) of $($rows.Count)"
    Add-QaResult $Ctx 'nothing left off at the listing (only the ceiling does that)' ($leftOff.Count -eq 0) "$($leftOff.Count) line(s)"
}

$sharedSun = New-QaBenchScenario 'shared-sun' 'auto' $script:QaBenchRoutes['combat']
$sharedSun.Proves = "The combat bench route with eye R on eye L's sun cascades (ETERNALVR_SHARED_SUN=1): both hooks installed on this game build, eye R keeping cascade 0 and listing few sun slices in every 10 s window, and the bench summary"
$sharedSun.Env = @($sharedSun.Env) + @('ETERNALVR_SHARED_SUN=1')
$sharedSun.Asserts = {
    param($c)
    $c.BenchSize = $c.Scenario.BenchSize
    Test-QaPresent $c 'stereo: eye views'
    Test-QaAbsent $c 'ERROR|exception|crash' -Regex -CaseSensitive
    Test-QaSharedSun $c
    Measure-QaBench $c
}

# A still scene for eye captures: the route's start and setup, then the head and hands held still for 40 s.
function New-QaSunEyesScenario([string]$Name, [string]$Route) {
    $r = $script:QaBenchRoutes[$Route]
    $s = @{
        Name = $Name; Kind = 'game'; OnlyByName = $true; SunRoute = $Route
        ProfileHeld = @('r_useRayTracing')
        Proves = "Eye captures every $script:QaSunCaptureEvery pairs at a still view of $($r.Title), the head held still, the bench's settings with pacing off; for comparing the sun's shadows between the eyes and between runs with and without ETERNALVR_SHARED_SUN"
        Env = @($script:QaBenchLauncherEnv) + @('ETERNALVR_RENDER_SIZE=auto', 'ETERNALVR_PACE=off') + @($script:QaBenchMeasureEnv) +
              @($script:QaSunStill) + @("ETERNALVR_DEBUG_COMMANDS=$($r.Schedule)")
        EyeCaptures = $script:QaSunCaptureEvery
        Input = @('right.aim = 0, -5')
        Timeline = {
            param($c)
            if (-not (& $script:QaBenchRoutes[$c.Scenario.SunRoute].Setup $c)) { return }
            Write-QaInput $c @('right.aim = 0, -5')
            Save-QaXrShot $c 'start'
            Wait-QaSeconds $c 40
            Save-QaXrShot $c 'end'
        }
        Asserts = {
            param($c)
            Test-QaPresent $c 'stereo: eye views'
            $pairs = Get-QaEyePairCount $c
            Add-QaResult $c 'eye captures: 20 pairs or more' ($pairs -ge 20) "$pairs pair(s)"
            $hooked = @(Get-QaMatches $c 'seq-sun: shadow setup hooked')
            if ($hooked.Count -gt 0) { Add-QaNote $c "seq-sun: $($hooked[0])" }
        }
    }
    foreach ($k in 'MenuMission', 'MenuSlot', 'MenuMap', 'MenuSaveNote', 'Map') { if ($r.ContainsKey($k)) { $s[$k] = $r[$k] } }
    return $s
}

$sunScenarios = @($sharedSun) + @(
    (New-QaSunEyesScenario 'sun-eyes-combat' 'combat'),
    (New-QaSunEyesScenario 'sun-eyes-hub' 'hub'),
    (New-QaSunEyesScenario 'sun-eyes-outdoor' 'outdoor')
)

# Before the launcher's end-to-end suite, which stays last.
$e2e = @($script:QaScenarios | Where-Object { $_.Kind -eq 'e2e' })
$script:QaScenarios = @($script:QaScenarios | Where-Object { $_.Kind -ne 'e2e' }) + $sunScenarios + $e2e
