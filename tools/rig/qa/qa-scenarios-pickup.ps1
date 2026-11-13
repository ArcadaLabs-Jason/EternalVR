# The camera of a pickup animation (docs/rig-findings/camera-animations.md section 6, ETERNALVR_PICKUP_CAMERA). The
# Praetor Suit token at e1m3's cp_03_shoot_gate is the pickup the rig reaches the normal way (the token-pickup
# scenario's spot): its sync animation ('interact/preator_suit_token/...') cuts the game's camera to its own start, turns
# it about 6 degrees down and 9 up over about 3 s. ETERNALVR_TEST_CAMERA_TRACE logs the game's camera, the player's
# view angles and the view the layer wrote in every game frame of a forced view. The simulator's head stays still, so
# the written view's pitch is the head's (0) plus what the layer adds. Dot-sourced by qa-suite.ps1 after
# qa-scenarios-fxsync.ps1: added to $script:QaScenarios before the launcher's end-to-end suite.
#
#   pickup-camera       the default (ETERNALVR_PICKUP_CAMERA unset): the pickup runs and the written view stays on the
#                       head (its pitch within 0.5 degrees all along), and nothing plays the camera (no 'pickup:' line
#                       of the layer's own; the bHaptics bridge logs 'bhaptics: pickup:')
#   pickup-camera-play  ETERNALVR_PICKUP_CAMERA=play: the written view turns with the game's camera (its pitch over 4
#                       degrees of range), never steps more than 4 degrees from one game frame to the next (the
#                       camera's cuts left out, the end eased; the token's steps are under 0.7 degrees, 4 leaves room
#                       for a frame a shot stalled), and is back on the head by the last trace line (120 game views
#                       after the forced view, longer than the 0.3 s ease at any frame rate up to about 400 a second)
#   pickup-nest-play    ETERNALVR_PICKUP_CAMERA=play at a gore nest (below): the game's camera cuts over 90 degrees of
#                       yaw, the written view keeps its heading (back within 0.5 degrees of its start), never steps
#                       more than 1.25 times the camera's largest step that is not a cut (the heart pull's, 6 degrees
#                       in one frame at the rig's frame rate: a bound that follows the frame rate), and its roll is
#                       played (down to -10 degrees or more, within 1.5 of the camera's a frame earlier at the end:
#                       the turn is played one game frame late)

# The trace line (camera_anim_hook.cpp traceView): the game's camera, the player's angles, the written view.
$script:QaCamTrace = 'camera: trace (\d+): game \((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\) angles \((-?[\d.]+) (-?[\d.]+)\) written \((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\)'

# Over the trace lines: the game camera's pitch range in the frames 5 degrees or more off the player's angles (the
# pickup's), the written pitch's range, its largest step between two lines and its last value.
function Get-QaPickupTrace($Ctx) {
    $r = [ordered]@{ Lines = 0; Turned = 0; GameMin = 999.0; GameMax = -999.0; WrittenMin = 999.0; WrittenMax = -999.0
                     MaxStep = 0.0; Last = 0.0 }
    $prev = $null
    foreach ($m in [regex]::Matches((Get-QaLogText $Ctx), $script:QaCamTrace)) {
        $g = $m.Groups
        $r.Lines++
        $game = [double]$g[2].Value; $angles = [double]$g[5].Value; $written = [double]$g[7].Value
        if ([Math]::Abs($game - $angles) -ge 5) {
            $r.Turned++
            $r.GameMin = [Math]::Min($r.GameMin, $game); $r.GameMax = [Math]::Max($r.GameMax, $game)
        }
        $r.WrittenMin = [Math]::Min($r.WrittenMin, $written); $r.WrittenMax = [Math]::Max($r.WrittenMax, $written)
        if ($null -ne $prev) { $r.MaxStep = [Math]::Max($r.MaxStep, [Math]::Abs($written - $prev)) }
        $prev = $written; $r.Last = $written
    }
    $r.GameRange = if ($r.Turned -gt 0) { $r.GameMax - $r.GameMin } else { 0.0 }
    $r.WrittenRange = if ($r.Lines -gt 0) { $r.WrittenMax - $r.WrittenMin } else { 0.0 }
    return $r
}

function Get-QaPickupTraceText($t) {
    return ('{0} line(s), {1} in the pickup; game pitch {2:N1} to {3:N1}; written pitch {4:N1} to {5:N1}, largest step {6:N2}, last {7:N2}' -f
        $t.Lines, $t.Turned, $t.GameMin, $t.GameMax, $t.WrittenMin, $t.WrittenMax, $t.MaxStep, $t.Last)
}

function New-QaPickupScenario([string]$Name, [string]$Proves, [string[]]$ExtraEnv, [scriptblock]$Asserts) {
    return @{
        Name = $Name; Kind = 'game'
        Proves = $Proves
        Map = 'game/sp/e1m3_cult/e1m3_cult -checkpoint cp_03_shoot_gate'
        Env = @('ETERNALVR_AIM=hand', 'ETERNALVR_TEST_CAMERA_TRACE=1', "ETERNALVR_DEBUG_COMMANDS=$script:QaTokenSchedule") + $ExtraEnv
        Input = @()
        Timeline = {
            param($c)
            Wait-QaSeconds $c 6   # the schedule's last step (7 s) has run: in the use trigger, facing the token
            $picked = $null
            foreach ($pitch in -15, -30, 0) {
                Write-QaInput $c @("right.aim = 0, $pitch"); Wait-QaSeconds $c 1
                Save-QaShot $c "before-$(-$pitch)"
                Write-QaInput $c @("right.aim = 0, $pitch", 'right.click = 1'); Wait-QaSeconds $c 0.3
                Write-QaInput $c @("right.aim = 0, $pitch")
                # The pickup's 3 s: a shot about every 0.4 s.
                for ($i = 1; $i -le 7; $i++) { Wait-QaSeconds $c 0.1; Save-QaShot $c "during-$(-$pitch)-$i" }
                Wait-QaSeconds $c 1
                if ((Get-QaLogText $c) -match "controllers: sync 'interact/") { $picked = $pitch; break }
            }
            if ($null -ne $picked) { Add-QaNote $c "Use at pitch $picked started the sync" }
            else { Add-QaNote $c 'no sync after Use at pitch -15, -30 and 0' }
            Wait-QaSeconds $c 3; Save-QaShot $c 'after'
            Write-QaInput $c @(); Wait-QaSeconds $c 2
        }
        Asserts = $Asserts
    }
}

# A gore nest (docs/rig-findings/camera-animations.md section 7): game_interact_gore_nest_3 at the token's checkpoint,
# reached by teleport and Use looking down at it. Its sync ('interact/gore_nest/gore_nest_sync') cuts the camera to
# the nest's own side (178 degrees from the teleport's heading), swings it about 30 degrees side to side, rolls it to
# -14.6 degrees and pulls it back fast (6 degrees in one frame at the rig's frame rate), then the game turns the
# player's angles to where the camera ended.
$script:QaNestSchedule = '3:god;g_setting_hud_auto_dismiss_tutorials 1;g_setting_tutorials 0|3.5:sync_printInteractionAndAnimationName 1|' +
    '5:teleport game_interact_gore_nest_3|5.5:where'
$script:QaCamTraceFull = 'camera: trace \d+: game \((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\) angles \([^)]*\) written \((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\) eye \([^)]*\)( forced)?'

# The longest run of forced trace lines (the pickup's) and the unforced lines after it (up to the next forced line or
# the log's end): the game's and the written yaw at the run's first line, the written yaw on the last of those lines,
# the largest written step (any angle, from the line before the run on), the game camera's largest step under 20
# degrees in the run (its motion, without the cuts; xr_math::CameraTurnTiming::hugeDegrees), the written roll's lowest
# value, and the gap between the written roll on the run's last line and the game's roll a line earlier (the turn is
# played a game frame late).
function Get-QaPickupEpisode($Ctx) {
    $rows = @(foreach ($m in [regex]::Matches((Get-QaLogText $Ctx), $script:QaCamTraceFull)) {
        $g = $m.Groups
        , @([double]$g[1].Value, [double]$g[2].Value, [double]$g[3].Value, [double]$g[4].Value, [double]$g[5].Value,
            [double]$g[6].Value, $g[7].Success)
    })
    $best = $null; $start = -1
    for ($i = 0; $i -le $rows.Count; $i++) {
        $forced = $i -lt $rows.Count -and $rows[$i][6]
        if ($forced -and $start -lt 0) { $start = $i }
        if (-not $forced -and $start -ge 0) {
            if (-not $best -or ($i - $start) -gt ($best[1] - $best[0])) { $best = @($start, $i) }
            $start = -1
        }
    }
    $r = [ordered]@{ Lines = 0; GameTurn = 0.0; WrittenBack = 999.0; MaxStep = 0.0; GameStep = 0.0; RollMin = 0.0; RollGap = 999.0 }
    if (-not $best) { return $r }
    $first = $best[0]; $end = $best[1]
    $next = $end; while ($next -lt $rows.Count -and -not $rows[$next][6]) { $next++ }
    $r.Lines = $end - $first
    $wrap = { param($d) (($d % 360) + 540) % 360 - 180 }
    $r.GameTurn = [Math]::Abs((& $wrap ($rows[$end - 1][1] - $rows[$first][1])))
    foreach ($row in $rows[$first..($end - 1)]) { $r.GameTurn = [Math]::Max($r.GameTurn, [Math]::Abs((& $wrap ($row[1] - $rows[$first][1])))) }
    $r.WrittenBack = [Math]::Abs((& $wrap ($rows[$next - 1][4] - $rows[$first][4])))
    for ($i = [Math]::Max($first, 1); $i -lt $next; $i++) {
        foreach ($k in 3, 4, 5) { $r.MaxStep = [Math]::Max($r.MaxStep, [Math]::Abs((& $wrap ($rows[$i][$k] - $rows[$i - 1][$k])))) }
        $r.RollMin = [Math]::Min($r.RollMin, $rows[$i][5])
    }
    for ($i = $first + 1; $i -lt $end; $i++) {
        $g = 0.0
        foreach ($k in 0, 1, 2) { $g = [Math]::Max($g, [Math]::Abs((& $wrap ($rows[$i][$k] - $rows[$i - 1][$k])))) }
        if ($g -lt 20) { $r.GameStep = [Math]::Max($r.GameStep, $g) }
    }
    $r.RollGap = [Math]::Abs($rows[$end - 1][5] - $rows[[Math]::Max($first, $end - 2)][2])
    return $r
}

$pickupScenarios = @(
    (New-QaPickupScenario 'pickup-camera' 'The default leaves a pickup camera''s turn out: the written view stays on the head while the game''s camera turns' @() {
        param($c)
        Test-QaPresent $c "controllers: sync 'interact/preator_suit_token"
        Test-QaAbsent $c '\] pickup: (ETERNALVR_PICKUP_CAMERA|camera turn)' -Regex
        $t = Get-QaPickupTrace $c
        $text = Get-QaPickupTraceText $t
        Add-QaResult $c 'trace: the game''s camera turned in the pickup' ($t.Turned -gt 0 -and $t.GameRange -ge 4) $text
        Add-QaResult $c 'trace: the written view stayed on the head (pitch within 0.5 deg)' ($t.Lines -gt 0 -and $t.WrittenRange -le 0.5) $text
    }),
    (New-QaPickupScenario 'pickup-camera-play' 'ETERNALVR_PICKUP_CAMERA=play plays a pickup camera''s motion on top of the head without a jump, and the view is back on the head after it' @('ETERNALVR_PICKUP_CAMERA=play') {
        param($c)
        Test-QaPresent $c "controllers: sync 'interact/preator_suit_token"
        Test-QaPresent $c 'pickup: ETERNALVR_PICKUP_CAMERA=play'
        Test-QaPresent $c 'pickup: camera turn \d+ starts' -Regex
        Test-QaPresent $c 'pickup: camera turn \d+ ends after' -Regex
        $t = Get-QaPickupTrace $c
        $text = Get-QaPickupTraceText $t
        Add-QaResult $c 'trace: the game''s camera turned in the pickup' ($t.Turned -gt 0 -and $t.GameRange -ge 4) $text
        Add-QaResult $c 'trace: the written view turned with it (pitch range over 4 deg)' ($t.WrittenRange -ge 4) $text
        Add-QaResult $c 'trace: no step over 4 deg between two game frames' ($t.Lines -gt 0 -and $t.MaxStep -le 4) $text
        Add-QaResult $c 'trace: the view back on the head at the end (within 0.5 deg)' ($t.Lines -gt 0 -and [Math]::Abs($t.Last) -le 0.5) $text
    }),
    @{
        Name = 'pickup-nest-play'; Kind = 'game'
        Proves = 'ETERNALVR_PICKUP_CAMERA=play at a gore nest: the cut to the nest''s side stays out, the fast heart pull is played, and the view keeps its heading through the game''s turn of the player at the end'
        Map = 'game/sp/e1m3_cult/e1m3_cult -checkpoint cp_03_shoot_gate'
        Env = @('ETERNALVR_AIM=hand', 'ETERNALVR_TEST_CAMERA_TRACE=1', 'ETERNALVR_PICKUP_CAMERA=play', "ETERNALVR_DEBUG_COMMANDS=$script:QaNestSchedule")
        Input = @()
        Timeline = {
            param($c)
            Wait-QaSeconds $c 6.5 # the teleport (5 s) has run: on the nest, facing away from its sync's side
            $synced = $false
            foreach ($pitch in -80, -60, -40) {
                Write-QaInput $c @("right.aim = 0, $pitch"); Wait-QaSeconds $c 1.2
                Write-QaInput $c @("right.aim = 0, $pitch", 'right.click = 1'); Wait-QaSeconds $c 0.3
                Write-QaInput $c @("right.aim = 0, $pitch")
                if (Wait-QaLog $c "controllers: sync 'interact/gore_nest/" 2) { $synced = $true; break }
            }
            if (-not $synced) { Add-QaNote $c 'no gore nest sync after Use at pitch -80, -60 and -40' }
            for ($i = 1; $i -le 4; $i++) { Wait-QaSeconds $c 0.5; Save-QaShot $c "during-$i" }
            Wait-QaSeconds $c 3; Save-QaShot $c 'after'
            Write-QaInput $c @(); Wait-QaSeconds $c 2
        }
        Asserts = {
            param($c)
            Test-QaPresent $c "controllers: sync 'interact/gore_nest/gore_nest_sync"
            Test-QaPresent $c 'pickup: camera turn \d+ starts .*; the view keeps the body yaw' -Regex
            Test-QaPresent $c 'pickup: the aim turned back'
            $e = Get-QaPickupEpisode $c
            $text = '{0} forced line(s); game yaw turned {1:N1}, its largest step under 20 deg {6:N2}; written yaw {2:N2} off its start at the end, largest step {3:N2}, roll down to {4:N1}, {5:N2} off the game''s roll at the end' -f
                $e.Lines, $e.GameTurn, $e.WrittenBack, $e.MaxStep, $e.RollMin, $e.RollGap, $e.GameStep
            Add-QaResult $c 'trace: the game''s camera cut to the nest''s side (yaw over 90 deg)' ($e.Lines -gt 50 -and $e.GameTurn -ge 90) $text
            # The cut stays out and the game's turn of the player's angles at the end does not turn the view (it did by the
            # cut's 178 degrees before the view kept its heading); the view steps no more than the camera's own motion does
            # (the heart pull's 6 degrees in a frame at 60 frames a second, 12 at 30), never a cut or two steps at once.
            Add-QaResult $c 'trace: the view keeps its heading (back within 0.5 deg of its start)' ($e.Lines -gt 50 -and $e.WrittenBack -le 0.5) $text
            Add-QaResult $c 'trace: no written step over 1.25 times the camera''s largest step under 20 deg' ($e.Lines -gt 50 -and $e.GameStep -gt 0 -and $e.MaxStep -le 1.25 * $e.GameStep) $text
            # The roll is played and the heart pull with it: left out as a cut it kept the view 6 degrees off the camera's roll.
            Add-QaResult $c 'trace: the roll played (down to -10 deg or more) and back with the camera''s (within 1.5 deg of it a frame earlier)' ($e.RollMin -le -10 -and $e.RollGap -le 1.5) $text
        }
    }
)

# Before the launcher's end-to-end suite, which stays last.
$e2e = @($script:QaScenarios | Where-Object { $_.Kind -eq 'e2e' })
$script:QaScenarios = @($script:QaScenarios | Where-Object { $_.Kind -ne 'e2e' }) + $pickupScenarios + $e2e
