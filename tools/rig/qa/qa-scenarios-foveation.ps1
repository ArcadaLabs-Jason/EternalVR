# Foveated rendering across a map loaded again: the game's Load Checkpoint loads the map again, and a report had
# the reduced-rate edges of the two eyes trade places after one. Dot-sourced by qa-suite.ps1 after
# qa-scenarios-guard.ps1, whose order it keeps: added to $script:QaScenarios before the launcher's end-to-end
# suite. Uses Test-QaEyeTestHalves and Test-QaPassFrames (qa-scenarios.ps1).
#
#   foveation-eyes-reload  the eye test with DLSS Quality per eye, measured over the stretch before the pause menu
#                          and a map load of the same map, and again over the stretch after it

# Seconds on the schedule's clock: the pause menu opens at $script:QaReloadMenuAt, the schedule loads the map again at
# $script:QaReloadAt (Load Checkpoint is chosen from that menu in the game). Each stretch is long enough for
# $script:QaEyeMinPairs captures every $script:QaEyeCaptureEvery pairs (49 pairs in 40 s on the rig).
$script:QaReloadMenuAt = 45
$script:QaReloadAt = 47
$script:QaReloadSettle = 8
$script:QaReloadAfter = 45
# A checkpoint in play (e1m2's start opens a menu after its load, which the eye test cannot measure).
$script:QaReloadMap = 'game/sp/e1m3_cult/e1m3_cult -checkpoint cp_03_shoot_gate'

$foveationScenarios = @(
    @{
        Name = 'foveation-eyes-reload'; Kind = 'game'
        Map = $script:QaReloadMap
        Proves = "Each eye keeps its own rate image after a map load of the same map from the pause menu (the eye test, DLSS Quality per eye): before and after it, eye L's left half and eye R's right half of the captures are coarse, the other halves not"
        Env = @('ETERNALVR_VRS_TEST=eyetest', 'ETERNALVR_STEREO_DLSS=1', 'ETERNALVR_STEREO_DLSS_QUALITY=quality',
                "ETERNALVR_DEBUG_COMMANDS=3:g_setting_hud_auto_dismiss_tutorials 1;g_setting_tutorials 0|$($script:QaReloadAt):map $script:QaReloadMap")
        Args = @('+r_antialiasing 2')
        EyeCaptures = $script:QaEyeCaptureEvery
        Input = $script:QaRestPose
        Timeline = {
            param($c)
            Wait-QaSeconds $c ($script:QaReloadMenuAt - 0.5)
            $c.ReloadBefore = Get-Date
            Write-QaInput $c ($script:QaRestPose + 'left.menu = 1'); Wait-QaSeconds $c 0.3
            Write-QaInput $c $script:QaRestPose
            if (-not (Wait-QaLog $c 'debug-commands: [\d.]+ s: map ' 30)) { Add-QaNote $c 'the schedule''s map load was not logged'; return }
            # The map's second load, then the render thread's first frames of it (eye tags start again).
            if (-not (Wait-QaLog $c 's: map [^\r\n]*[\s\S]*mp guard: map load ''game/sp[\s\S]*eye tags start at' 90)) {
                Add-QaNote $c 'the map load after the schedule''s map command was not logged'; return
            }
            Wait-QaSeconds $c $script:QaReloadSettle
            $c.ReloadAfter = Get-Date
            Wait-QaSeconds $c $script:QaReloadAfter
            Save-QaShot $c 'end'
        }
        Asserts = {
            param($c)
            Test-QaPresent $c "eye test: eye L's left half and eye R's right half at 4x4"
            Test-QaPresent $c "eye R's DLSS feature for the game's feature"
            Test-QaCount $c "mp guard: map load 'game/sp/e1m3_cult/e1m3_cult' (single-player)" 2
            if ($c.ReloadBefore -and $c.ReloadAfter) {
                Test-QaEyeTestHalves $c $null $c.ReloadBefore 'before the map load'
                Test-QaEyeTestHalves $c $c.ReloadAfter $null 'after the map load'
            } else {
                Add-QaResult $c 'the map was loaded again on the schedule' $false 'see the notes'
            }
            Test-QaPassFrames $c
        }
    }
)

# Before the launcher's end-to-end suite, which stays last.
$e2e = @($script:QaScenarios | Where-Object { $_.Kind -eq 'e2e' })
$script:QaScenarios = @($script:QaScenarios | Where-Object { $_.Kind -ne 'e2e' }) + $foveationScenarios + $e2e
