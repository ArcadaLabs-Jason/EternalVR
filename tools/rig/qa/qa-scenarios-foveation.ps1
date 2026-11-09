# Foveated rendering across a map loaded again: the game's Load Checkpoint loads the map again, and a report had
# the reduced-rate edges of the two eyes trade places after one. Dot-sourced by qa-suite.ps1 after
# qa-scenarios-guard.ps1, whose order it keeps: added to $script:QaScenarios before the launcher's end-to-end
# suite. Uses Test-QaEyeTestHalves and Test-QaPassFrames (qa-scenarios.ps1).
#
#   foveation-eyes-reload  the eye test with DLSS Quality per eye, measured over the stretch before the pause menu
#                          and a map load of the same map, and again over the stretch after it

# Seconds on the schedule's clock: the pause menu opens at $script:QaReloadMenuAt, the schedule loads the map again at
# $script:QaReloadAt (Load Checkpoint is chosen from that menu in the game). Each stretch is long enough for
# $script:QaEyeMinPairs captures every $script:QaEyeCaptureEvery pairs (49 pairs in 40 s on the rig). The timeline
# starts only once launch-ht.ps1 returns, which waits for the game's audio session: up to 104 s after the launch on
# the rig, while the schedule's clock can start 25 s after it, so the pause comes at 110 s (45 s missed it once).
$script:QaReloadMenuAt = 110
$script:QaReloadAt = 112
$script:QaReloadSettle = 8
$script:QaReloadAfter = 45
# A checkpoint in play (e1m2's start opens a menu after its load, which the eye test cannot measure).
$script:QaReloadMap = 'game/sp/e1m3_cult/e1m3_cult -checkpoint cp_03_shoot_gate'

# Waits until $Seconds on the schedule's clock: the layer's log start plus the log time of the line that starts the
# clock. The timeline itself starts some seconds into the schedule (4 to 19 s on the rig), so counting from it put
# the pause after the map load. $false when that moment has already passed or the log has no clock yet.
function Wait-QaScheduleSeconds($Ctx, [double]$Seconds) {
    $text = Get-QaLogText $Ctx
    $start = [regex]::Match($text, 'log start (\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d+) local')
    $clock = [regex]::Match($text, '(?m)^\[\s*([\d.]+)\][^\r\n]*the schedule''s clock starts')
    if (-not $start.Success -or -not $clock.Success) { return $false }
    $at = [datetime]::ParseExact($start.Groups[1].Value, 'yyyy-MM-dd HH:mm:ss.fff', $null).AddSeconds(
        [double]::Parse($clock.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture) + $Seconds)
    $left = ($at - (Get-Date)).TotalSeconds
    if ($left -lt 0) { return $false }
    Wait-QaSeconds $Ctx $left
    return $true
}

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
            if (-not (Wait-QaScheduleSeconds $c ($script:QaReloadMenuAt - 0.5))) {
                Add-QaNote $c 'the timeline started after the pause was due on the schedule''s clock'; return
            }
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
            # The map is loaded from the pause menu: the pause comes before the schedule's map command.
            $text = Get-QaLogText $c
            $pause = [regex]::Match($text, 'controllers: action pause')
            $load = [regex]::Match($text, 'debug-commands: [\d.]+ s: map ')
            Add-QaResult $c 'the pause menu was up before the map load' ($pause.Success -and $load.Success -and $pause.Index -lt $load.Index) `
                $(if (-not $pause.Success) { 'no pause' } elseif (-not $load.Success) { 'no map load' } elseif ($pause.Index -gt $load.Index) { 'the pause came after the map load' } else { '' })
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
