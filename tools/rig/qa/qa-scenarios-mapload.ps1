# Map loads in a row (docs/rig-findings/map-load-name-read.md). Dot-sourced by qa-suite.ps1 after
# qa-scenarios-bench.ps1 (whose launcher environment it uses); by name only (a soak, not a check of every merge).
#
#   map-load-loop   bench-combat's start (Cultist Base, checkpoint cp_03_shoot_gate, the launcher's default settings),
#                   then the same map loaded again from the debug schedule every $script:QaMapLoadEvery s,
#                   EVR_QA_MAPLOAD_COUNT times (default 30; 0: only the launch's own load). Checks every load
#                   reached the multiplayer guard's map load hook and that no crash note (a handled access violation
#                   included) was logged. Notes how many crash notes each place took, so a soak of an older build
#                   counts its faults.

$script:QaMapLoadMap = 'game/sp/e1m3_cult/e1m3_cult -checkpoint cp_03_shoot_gate'
$script:QaMapLoadEvery = 20
$script:QaMapLoadCount = if ($env:EVR_QA_MAPLOAD_COUNT -match '^\d+$' -and [int]$env:EVR_QA_MAPLOAD_COUNT -ge 0) {
    [int]$env:EVR_QA_MAPLOAD_COUNT
} else { 30 }

$mapLoadSteps = @("3:$script:QaBenchTutorials")
for ($i = 1; $i -le $script:QaMapLoadCount; $i++) {
    $mapLoadSteps += "$($i * $script:QaMapLoadEvery):map $script:QaMapLoadMap"
}

$mapLoadScenarios = @(
    @{
        Name = 'map-load-loop'; Kind = 'game'; OnlyByName = $true
        Map = $script:QaMapLoadMap
        Proves = "The map loaded $($script:QaMapLoadCount + 1) times in one process (bench-combat's start, then the debug schedule's map command every $($script:QaMapLoadEvery) s): every load reaches the map load hook and no crash note is logged (the map name was once copied past its page)"
        Env = @($script:QaBenchLauncherEnv) + @("ETERNALVR_DEBUG_COMMANDS=$($mapLoadSteps -join '|')")
        Input = @('right.aim = 0, -5')
        Timeline = {
            param($c)
            $want = $script:QaMapLoadCount + 1
            $deadline = (Get-Date).AddSeconds($script:QaMapLoadCount * $script:QaMapLoadEvery + 90)
            while ((Get-Date) -lt $deadline -and -not (Test-QaStopRequested)) {
                $loads = [regex]::Matches((Get-QaLogText $c), "mp guard: map load 'game/sp/e1m3_cult/e1m3_cult'").Count
                if ($loads -ge $want) { break }
                Wait-QaSeconds $c 2
            }
            # The last load's first frames.
            Wait-QaSeconds $c 8
        }
        Asserts = {
            param($c)
            Test-QaCount $c "mp guard: map load 'game/sp/e1m3_cult/e1m3_cult' (single-player)" ($script:QaMapLoadCount + 1)
            Test-QaAbsent $c 'crash note'
            $places = @(Get-QaMatches $c 'crash note' | ForEach-Object { if ($_ -match ' at (\S+);') { $Matches[1] } })
            if ($places.Count) {
                $counts = $places | Group-Object | ForEach-Object { "$($_.Name) $($_.Count)x" }
                Add-QaNote $c "crash notes by place: $($counts -join ', ')"
            }
        }
    }
)

# Before the launcher's end-to-end suite, which stays last.
$e2e = @($script:QaScenarios | Where-Object { $_.Kind -eq 'e2e' })
$script:QaScenarios = @($script:QaScenarios | Where-Object { $_.Kind -ne 'e2e' }) + $mapLoadScenarios + $e2e
