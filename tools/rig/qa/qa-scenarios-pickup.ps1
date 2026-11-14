# The camera of a pickup animation (docs/rig-findings/camera-animations.md section 6, ETERNALVR_PICKUP_CAMERA). The
# Praetor Suit token at e1m3's cp_03_shoot_gate is the pickup the rig reaches the normal way (the token-pickup
# scenario's spot): its sync animation ('interact/preator_suit_token/...') cuts the game's camera to its own start, turns
# it about 6 degrees down and 9 up over about 3 s. ETERNALVR_TEST_CAMERA_TRACE logs the game's camera, the player's
# view angles and the view the layer wrote in every game frame of a forced view. The simulator's head stays still, so
# the written view's pitch is the head's (0) plus what the layer adds; the checks take the written view less the head's
# angles the trace gives, which is the written view itself with the head still. Dot-sourced by qa-suite.ps1 after
# qa-scenarios-fxsync.ps1: added to $script:QaScenarios before the launcher's end-to-end suite.
#
#   pickup-camera       the default (ETERNALVR_PICKUP_CAMERA unset): the pickup runs and the written view stays on the
#                       head (its pitch within 0.5 degrees all along), and nothing plays the camera (no 'pickup:
#                       camera turn' line; Still view logs the pickup's start and end as 'pickup: pickup N ...', and
#                       the bHaptics bridge logs 'bhaptics: pickup:')
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
#   pickup-nest         the default (Still view) at the same gore nest: the game's camera cuts over 90 degrees of
#                       yaw; the written yaw is on the cut's side within 3 game views of it and in one step (Still's
#                       hold keeps a jump of the camera's heading for 2 game views at most, xr_math::CameraHeadingHold),
#                       then on the camera's heading on every line to the game's set at its end (the swing
#                       followed, no hold left on), no other written step over 1.25 times the camera's largest yaw step under 20
#                       degrees, and the game's turn of the player's angles at the end turns no view (the view on the
#                       camera's last heading after it). "On the heading": within that largest step, a bound that
#                       follows the frame rate
#   <name>-head, <name>-pad, <name>-view  each of the four under head aim with the motion controllers, in gamepad mode
#                       and under view aim with the motion controllers (by name only, the head sweeping; New-QaAimVariant
#                       below): the same checks on the written view less the head's angles; the token's also check that
#                       the view keeps its heading through the pickup (the written yaw less the head's within 1 degree),
#                       as the plain ones do. pickup-nest-play-view checks the yaw as pickup-nest does (view aim keeps no
#                       heading: nothing writes the player's angles) and the roll played

# The trace line (camera_anim_hook.cpp traceView): the game's camera, the player's angles, the written view, and (once
# head aim gave the line its aim part) the head's pitch and yaw.
$script:QaCamTraceHead = '[^;\r\n]*(?:; body -?[\d.]+ head \((-?[\d.]+) (-?[\d.]+)\))?'
$script:QaCamTrace = 'camera: trace (\d+): game \((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\) angles \((-?[\d.]+) (-?[\d.]+)\) written \((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\)' + $script:QaCamTraceHead

# A trace group's angle, or 0 when the line has none (the head before head aim's first line).
function Get-QaGroupValue($Group) { if ($Group.Success) { return [double]$Group.Value } else { return 0.0 } }

# Over the trace lines: the game camera's pitch range in the frames 5 degrees or more off the player's angles (the
# pickup's), the written pitch's range, its largest step between two lines and its last value, all less the head's
# pitch (with the simulator's head still, the written pitch itself).
function Get-QaPickupTrace($Ctx) {
    $r = [ordered]@{ Lines = 0; Turned = 0; GameMin = 999.0; GameMax = -999.0; WrittenMin = 999.0; WrittenMax = -999.0
                     MaxStep = 0.0; Last = 0.0 }
    $prev = $null
    foreach ($m in [regex]::Matches((Get-QaLogText $Ctx), $script:QaCamTrace)) {
        $g = $m.Groups
        $r.Lines++
        $game = [double]$g[2].Value; $angles = [double]$g[5].Value
        $written = [double]$g[7].Value - (Get-QaGroupValue $g[10])
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

# Use: the weapon hand's stick click with the hand at $Aim (input file lines), or in gamepad mode (the scenario's Pad)
# the pad's right stick click.
function Invoke-QaUse($Ctx, [string[]]$Aim) {
    if ($Ctx.Scenario.Pad) { Invoke-QaPadPress $Ctx 'rs' 0.3; return }
    Write-QaInput $Ctx (@($Aim) + 'right.click = 1'); Wait-QaSeconds $Ctx 0.3
    Write-QaInput $Ctx $Aim
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
                Invoke-QaUse $c @("right.aim = 0, $pitch")
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
$script:QaNestTimeline = {
    param($c)
    Wait-QaSeconds $c 6.5 # the teleport (5 s) has run: on the nest, facing away from its sync's side
    $synced = $false
    foreach ($pitch in -80, -60, -40) {
        Write-QaInput $c @("right.aim = 0, $pitch"); Wait-QaSeconds $c 1.2
        Invoke-QaUse $c @("right.aim = 0, $pitch")
        if (Wait-QaLog $c "controllers: sync 'interact/gore_nest/" 2) { $synced = $true; break }
    }
    if (-not $synced) { Add-QaNote $c 'no gore nest sync after Use at pitch -80, -60 and -40' }
    for ($i = 1; $i -le 4; $i++) { Wait-QaSeconds $c 0.5; Save-QaShot $c "during-$i" }
    Wait-QaSeconds $c 3; Save-QaShot $c 'after'
    Write-QaInput $c @(); Wait-QaSeconds $c 2
}
$script:QaCamTraceFull = 'camera: trace \d+: game \((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\) angles \([^)]*\) written \((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\) eye \([^)]*\)( forced)?' + $script:QaCamTraceHead

# The longest run of forced trace lines (the pickup's) and the unforced lines after it (up to the next forced line or
# the log's end): the game's and the written yaw at the run's first line, the written yaw on the last of those lines,
# the largest written step (any angle, from the line before the run on), the game camera's largest step under 20
# degrees in the run (its motion, without the cuts; xr_math::CameraTurnTiming::hugeDegrees), the written roll's lowest
# value, and the gap between the written roll on the run's last line and the game's roll a line earlier (the turn is
# played a game frame late). The written pitch and yaw are taken less the head's (with the simulator's head still, the
# written view itself).
function Get-QaPickupEpisode($Ctx) {
    $rows = @(foreach ($m in [regex]::Matches((Get-QaLogText $Ctx), $script:QaCamTraceFull)) {
        $g = $m.Groups
        , @([double]$g[1].Value, [double]$g[2].Value, [double]$g[3].Value, ([double]$g[4].Value - (Get-QaGroupValue $g[8])),
            ([double]$g[5].Value - (Get-QaGroupValue $g[9])), [double]$g[6].Value, $g[7].Success)
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
    $r = [ordered]@{ Lines = 0; GameTurn = 0.0; WrittenBack = 999.0; MaxStep = 0.0; GameStep = 0.0; RollMin = 0.0; RollGap = 999.0
                     Heading = 999.0; HeadingLater = 999.0 }
    if (-not $best) { return $r }
    $first = $best[0]; $end = $best[1]
    $next = $end; while ($next -lt $rows.Count -and -not $rows[$next][6]) { $next++ }
    $r.Lines = $end - $first
    $wrap = { param($d) (($d % 360) + 540) % 360 - 180 }
    # Heading: the range of the written yaw (less the head's) from the run's first line to the last line after it;
    # HeadingLater: from the run's fifth line on (after a cut of the camera's heading at the pickup's start, which
    # Still view shows once, held two game views at most).
    $range = { param($from) $low = 0.0; $high = 0.0
        for ($i = $from; $i -lt $next; $i++) { $d = & $wrap ($rows[$i][4] - $rows[$from][4]); $low = [Math]::Min($low, $d); $high = [Math]::Max($high, $d) }
        $high - $low }
    $r.Heading = & $range $first
    if ($first + 4 -lt $next) { $r.HeadingLater = & $range ($first + 4) }
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

# The token's camera turns its pitch on its own heading: the view keeps its heading through the pickup and after it
# (the written yaw, less the head's, from the pickup's first forced line to the last line after it: within 1 degree, and
# back within 0.5 of its start at the end). With Still view a player not facing the token's heading (under head aim the
# view angles are the head's) sees the camera's cut to it once at the start (`Still`: checked from the run's fifth
# line). With Camera moves such a cut's tail of a degree or so in the next frame is motion and played (1.3 under head
# aim on the rig), then eased out: within 2 degrees.
function Test-QaPickupHeading($Ctx, [switch]$Still) {
    $e = Get-QaPickupEpisode $Ctx
    $range = if ($Still) { $e.HeadingLater } else { $e.Heading }
    $bound = if ($Still) { 1 } else { 2 }
    Add-QaResult $Ctx "trace: the view kept its heading through the pickup and after (the written yaw less the head's within $bound deg$(if ($Still) { ', after a cut at its start' }), back within 0.5 at the end)" ($e.Lines -gt 50 -and $range -le $bound -and ($Still -or $e.WrittenBack -le 0.5)) ('{0} forced line(s); the written yaw (less the head''s) over a range of {1:N2} deg ({2:N2} from the start), {3:N2} off its start at the end' -f $e.Lines, $range, $e.Heading, $e.WrittenBack)
}

# Still view at the gore nest (pickup-nest): the trace lines with the player's angles, over the same run as
# Get-QaPickupEpisode. The cut: the first camera yaw step of 20 degrees or more in the run (with the ones right after
# it); the heading: the written yaw less the camera's on the line before it, which the view keeps on the camera's.
# Shown: game views from the cut's first to the first line whose written yaw is on the camera's heading (within the
# camera's largest yaw step under 20 degrees in the run, Tol); Partial: how far the written yaw moved from the line
# before the cut on the lines before that one (a cut shown in two pieces moves it). OffMax: the written yaw's largest
# distance from the camera's heading from the shown line to the line before the set (below; the run's last without
# one), OffEnd on that line. MaxStep: the written view's largest step (any angle) from the line before the run on, but
# the shown line's. The set: the first line after the shown one where the player's angles turn 20 degrees or more (the
# game's SetViewAngles as the pickup ends); SetStep: the written view's largest step from it on; EndOff: the written
# yaw on the last line against the camera's heading on the line before the set. Tilt: the written pitch's and roll's largest distance from 0 (Still writes none of the
# camera's) over the run. Holds: Still's hold lines in the log. The written pitch and yaw are taken less the head's (a
# head that moves under head aim; with the simulator's head still, the written view itself).
$script:QaCamTraceAngles = 'camera: trace \d+: game \((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\) angles \((?:(-?[\d.]+) (-?[\d.]+)|none)\) written \((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\) eye \([^)]*\)( forced)?' + $script:QaCamTraceHead
function Get-QaNestStill($Ctx) {
    $text = Get-QaLogText $Ctx
    $rows = @(foreach ($m in [regex]::Matches($text, $script:QaCamTraceAngles)) {
        $g = $m.Groups
        $ay = if ($g[5].Success) { [double]$g[5].Value } else { [double]::NaN }
        , @([double]$g[1].Value, [double]$g[2].Value, [double]$g[3].Value, $ay, ([double]$g[6].Value - (Get-QaGroupValue $g[10])),
            ([double]$g[7].Value - (Get-QaGroupValue $g[11])), [double]$g[8].Value, $g[9].Success)
    })
    $r = [ordered]@{ Lines = 0; Cut = 0.0; Tol = 0.0; Shown = -1; Partial = 999.0; OffMax = 999.0; OffEnd = 999.0
                     MaxStep = 999.0; Set = $false; SetStep = 999.0; EndOff = 999.0; Tilt = 999.0
                     Holds = [regex]::Matches($text, "pickup: a pickup's camera jumped").Count }
    $best = $null; $start = -1
    for ($i = 0; $i -le $rows.Count; $i++) {
        $forced = $i -lt $rows.Count -and $rows[$i][7]
        if ($forced -and $start -lt 0) { $start = $i }
        if (-not $forced -and $start -ge 0) {
            if (-not $best -or ($i - $start) -gt ($best[1] - $best[0])) { $best = @($start, $i) }
            $start = -1
        }
    }
    if (-not $best) { return $r }
    $first = $best[0]; $end = $best[1]
    $next = $end; while ($next -lt $rows.Count -and -not $rows[$next][7]) { $next++ }
    $r.Lines = $end - $first
    $wrap = { param($d) (($d % 360) + 540) % 360 - 180 }
    $yawStep = { param($i) [Math]::Abs((& $wrap ($rows[$i][1] - $rows[$i - 1][1]))) }
    for ($i = $first + 1; $i -lt $end; $i++) { if ((& $yawStep $i) -lt 20) { $r.Tol = [Math]::Max($r.Tol, (& $yawStep $i)) } }
    $cut = -1
    for ($i = [Math]::Max($first, 1); $i -lt $end; $i++) { if ((& $yawStep $i) -ge 20) { $cut = $i; break } }
    if ($cut -lt 0) { return $r }
    $cutEnd = $cut; while ($cutEnd + 1 -lt $end -and (& $yawStep ($cutEnd + 1)) -ge 20) { $cutEnd++ }
    $r.Cut = & $wrap ($rows[$cutEnd][1] - $rows[$cut - 1][1])
    $heading = & $wrap ($rows[$cut - 1][5] - $rows[$cut - 1][1])
    $off = { param($i, $j) [Math]::Abs((& $wrap ($rows[$i][5] - $rows[$j][1] - $heading))) }
    $shown = -1
    for ($i = $cut; $i -lt $end; $i++) { if ((& $off $i $i) -le $r.Tol) { $shown = $i; break } }
    if ($shown -lt 0) { return $r }
    $r.Shown = $shown - $cut
    $r.Partial = 0.0
    for ($i = $cut; $i -lt $shown; $i++) { $r.Partial = [Math]::Max($r.Partial, [Math]::Abs((& $wrap ($rows[$i][5] - $rows[$cut - 1][5])))) }
    $writtenStep = { param($i) $s = 0.0; foreach ($k in 4, 5, 6) { $s = [Math]::Max($s, [Math]::Abs((& $wrap ($rows[$i][$k] - $rows[$i - 1][$k])))) }; $s }
    $r.MaxStep = 0.0
    for ($i = [Math]::Max($first, 1); $i -lt $next; $i++) { if ($i -ne $shown) { $r.MaxStep = [Math]::Max($r.MaxStep, (& $writtenStep $i)) } }
    $setAt = $end # the line of the game's set, or the run's end without one
    for ($i = $shown + 1; $i -lt $next; $i++) {
        $a = & $wrap ($rows[$i][3] - $rows[$i - 1][3])
        if (-not [double]::IsNaN($a) -and [Math]::Abs($a) -ge 20) {
            $r.Set = $true; $r.SetStep = 0.0; $setAt = [Math]::Min($i, $end)
            for ($j = $i; $j -lt $next; $j++) { $r.SetStep = [Math]::Max($r.SetStep, (& $writtenStep $j)) }
            break
        }
    }
    # On the camera's heading up to the set: from there the game's view is the player's own again (under head aim it
    # holds the head's yaw at once, under hand aim the camera's last until the forced view ends).
    $r.OffMax = 0.0
    for ($i = $shown; $i -lt $setAt; $i++) { $r.OffMax = [Math]::Max($r.OffMax, (& $off $i $i)) }
    $r.OffEnd = & $off ($setAt - 1) ($setAt - 1)
    $r.EndOff = & $off ($next - 1) ($setAt - 1)
    $r.Tilt = 0.0
    for ($i = $first; $i -lt $next; $i++) { $r.Tilt = [Math]::Max($r.Tilt, [Math]::Max([Math]::Abs($rows[$i][4]), [Math]::Abs($rows[$i][6]))) }
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
        Test-QaPickupHeading $c -Still
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
        Test-QaPickupHeading $c
    }),
    @{
        Name = 'pickup-nest-play'; Kind = 'game'; HeadTilt = -60
        Proves = 'ETERNALVR_PICKUP_CAMERA=play at a gore nest: the cut to the nest''s side stays out, the fast heart pull is played, and the view keeps its heading through the game''s turn of the player at the end'
        Map = 'game/sp/e1m3_cult/e1m3_cult -checkpoint cp_03_shoot_gate'
        Env = @('ETERNALVR_AIM=hand', 'ETERNALVR_TEST_CAMERA_TRACE=1', 'ETERNALVR_PICKUP_CAMERA=play', "ETERNALVR_DEBUG_COMMANDS=$script:QaNestSchedule")
        Input = @()
        Timeline = $script:QaNestTimeline
        Asserts = {
            param($c)
            Test-QaPresent $c "controllers: sync 'interact/gore_nest/gore_nest_sync"
            $e = Get-QaPickupEpisode $c
            $text = '{0} forced line(s); game yaw turned {1:N1}, its largest step under 20 deg {6:N2}; written yaw {2:N2} off its start at the end, largest step {3:N2}, roll down to {4:N1}, {5:N2} off the game''s roll at the end' -f
                $e.Lines, $e.GameTurn, $e.WrittenBack, $e.MaxStep, $e.RollMin, $e.RollGap, $e.GameStep
            Add-QaResult $c 'trace: the game''s camera cut to the nest''s side (yaw over 90 deg)' ($e.Lines -gt 50 -and $e.GameTurn -ge 90) $text
            if ($c.Scenario.Aim -eq 'view') {
                # View aim: nothing writes the player's angles, so no heading is kept (the view is built on the game's
                # yaw, which takes the camera's): the yaw as Still view shows it (the cut once, in one step, then the
                # camera's heading, the game's set at the end turning no view), the camera's pitch and roll played on it.
                Test-QaPresent $c 'pickup: camera turn \d+ starts .*; no heading held' -Regex
                Test-QaAbsent $c 'pickup: the aim turned back'
                $s = Get-QaNestStill $c
                $bound = 1.25 * [Math]::Max($s.Tol, $e.GameStep)
                $still = 'the camera cut {0:N1} deg of yaw; the view on the cut''s side {1} game view(s) after it (moved {2:N2} before that), then at most {3:N2} off the camera''s heading; largest other written step {4:N2} against {5:N2}; angles set at the end: {6}, largest written step from there {7:N2}, the last line {8:N2} off the camera''s last heading' -f
                    $s.Cut, $s.Shown, $s.Partial, $s.OffMax, $s.MaxStep, $bound, $s.Set, $s.SetStep, $s.EndOff
                Add-QaResult $c 'trace: the cut shown once, in one step within 3 game views, then the camera''s heading on every line to the game''s set (within 1 deg)' ($s.Shown -ge 0 -and $s.Shown -le 3 -and $s.Partial -le $s.Tol -and $s.OffMax -le 1 -and $s.OffEnd -le 1) $still
                Add-QaResult $c 'trace: no other written step over 1.25 times the camera''s largest step under 20 deg' ($s.Shown -ge 0 -and $s.Tol -gt 0 -and $s.MaxStep -le $bound) $still
                Add-QaResult $c 'trace: the game''s turn of the player''s angles at the end turns no view' ($s.Set -and $s.SetStep -le $bound -and $s.EndOff -le 1) $still
                Add-QaResult $c 'trace: the roll played (down to -10 deg or more) and back with the camera''s (within 1.5 deg of it a frame earlier)' ($e.RollMin -le -10 -and $e.RollGap -le 1.5) $text
                return
            }
            Test-QaPresent $c 'pickup: camera turn \d+ starts .*; the view keeps the body yaw' -Regex
            Test-QaPresent $c 'pickup: the aim turned back'
            # The cut stays out and the game's turn of the player's angles at the end does not turn the view (it did by the
            # cut's 178 degrees before the view kept its heading); the view steps no more than the camera's own motion does
            # (the heart pull's 6 degrees in a frame at 60 frames a second, 12 at 30), never a cut or two steps at once.
            Add-QaResult $c 'trace: the view keeps its heading (back within 0.5 deg of its start)' ($e.Lines -gt 50 -and $e.WrittenBack -le 0.5) $text
            Add-QaResult $c 'trace: no written step over 1.25 times the camera''s largest step under 20 deg' ($e.Lines -gt 50 -and $e.GameStep -gt 0 -and $e.MaxStep -le 1.25 * $e.GameStep) $text
            # The roll is played and the heart pull with it: left out as a cut it kept the view 6 degrees off the camera's roll.
            Add-QaResult $c 'trace: the roll played (down to -10 deg or more) and back with the camera''s (within 1.5 deg of it a frame earlier)' ($e.RollMin -le -10 -and $e.RollGap -le 1.5) $text
        }
    },
    @{
        Name = 'pickup-nest'; Kind = 'game'; HeadTilt = -60
        Proves = 'Still view (the default) at a gore nest: the view turns to the nest''s side once, in one step within 3 game views of the camera''s cut, follows the camera''s swing, and the game''s turn of the player''s angles at the end turns no view'
        Map = 'game/sp/e1m3_cult/e1m3_cult -checkpoint cp_03_shoot_gate'
        Env = @('ETERNALVR_AIM=hand', 'ETERNALVR_TEST_CAMERA_TRACE=1', "ETERNALVR_DEBUG_COMMANDS=$script:QaNestSchedule")
        Input = @()
        Timeline = $script:QaNestTimeline
        Asserts = {
            param($c)
            Test-QaPresent $c "controllers: sync 'interact/gore_nest/gore_nest_sync"
            Test-QaAbsent $c '\] pickup: (ETERNALVR_PICKUP_CAMERA|camera turn)' -Regex
            $e = Get-QaNestStill $c
            $text = '{0} forced line(s); the camera cut {1:N1} deg of yaw, its largest yaw step under 20 deg {2:N2}; the view on the cut''s side {3} game view(s) after it (moved {4:N2} before that), then at most {5:N2} off the camera''s heading ({6:N2} on the run''s last line); largest other written step {7:N2}; angles set at the end: {8}, largest written step from there {9:N2}, the last line {10:N2} off the camera''s last heading; written pitch and roll at most {12:N2} from the head''s; {11} hold line(s)' -f
                $e.Lines, $e.Cut, $e.Tol, $e.Shown, $e.Partial, $e.OffMax, $e.OffEnd, $e.MaxStep, $e.Set, $e.SetStep, $e.EndOff, $e.Holds, $e.Tilt
            $ok = $e.Lines -gt 50 -and $e.Tol -gt 0
            # On the camera's heading: within 1 degree (the view takes the camera's yaw itself, 0.00 off on the rig), not
            # within Tol, the camera's largest step, which one hitch frame can make several degrees.
            $on = 1.0
            Add-QaResult $c 'trace: the game''s camera cut to the nest''s side (yaw over 90 deg)' ($ok -and [Math]::Abs($e.Cut) -ge 90) $text
            # Still's hold keeps a jump of the camera's heading for 2 game views at most: the cut is shown that late, whole.
            # With the head still (hand aim) the hold must have run; a head that moves under head aim moves the player's
            # view angles, and a view whose angles moved starts no hold (the cut then shown at once).
            if ($c.Scenario.Aim) {
                Add-QaResult $c 'trace: the view was on the cut''s side within 3 game views of it, in one step' ($ok -and $e.Shown -ge 0 -and $e.Shown -le 3 -and $e.Partial -le $e.Tol) $text
            } else {
                Add-QaResult $c 'trace: the hold ran and the view was on the cut''s side within 3 game views of it, in one step' ($ok -and $e.Holds -ge 1 -and $e.Shown -ge 1 -and $e.Shown -le 3 -and $e.Partial -le $e.Tol) $text
            }
            Add-QaResult $c 'trace: then on the camera''s heading on every line to the game''s set at the end (the swing followed, no hold left on; within 1 deg)' ($ok -and $e.Shown -ge 0 -and $e.OffMax -le $on -and $e.OffEnd -le $on) $text
            Add-QaResult $c 'trace: no written step over 1.25 times the camera''s largest yaw step under 20 deg but the cut''s' ($ok -and $e.Shown -ge 0 -and $e.MaxStep -le 1.25 * $e.Tol) $text
            Add-QaResult $c 'trace: no pitch or roll of the camera written (within 0.5 deg of the head''s)' ($ok -and $e.Tilt -le 0.5) $text
            Add-QaResult $c 'trace: the game''s turn of the player''s angles at the end turns no view (the view on the camera''s last heading after it)' ($ok -and $e.Set -and $e.SetStep -le 1.25 * $e.Tol -and $e.EndOff -le $on) $text
        }
    }
)

# Head aim, gamepad mode and view aim (README "Scenarios"): every scenario above once more under head aim with the
# motion controllers (<name>-head: ETERNALVR_AIM=head, Use the weapon hand's stick click), in gamepad mode (<name>-pad:
# ETERNALVR_CONTROLLERS=0 and the virtual pad, Use its right stick click) and under view aim with the motion controllers
# (<name>-view: ETERNALVR_AIM=view, the game's own aim, the head tracked on top of it), by name only. The
# simulator's head sweeps 8 degrees each way every 3 s all along (ETERNALVR_TEST_HEAD_SWAY), held tilted by the
# scenario's HeadTilt (the gore nest's Use needs a head looking down at it): under head aim the player's view angles
# follow the head, and a still head would hide that. The checks are the scenario's own, on the written view less the
# head's angles.
$script:QaHeadSwayYaw = 8
# A variant's checks: its scenario's own, then (the pickups of this file; the menu ones check it themselves) that the
# head's sweep reached the trace: the trace lines' head yaw over a range of 4 degrees or more (its group parsed).
$script:QaAimAsserts = {
    param($c)
    & $c.Scenario.BaseAsserts $c
    if (-not $c.Scenario.Pickup) {
        $yaws = ([regex]::Matches((Get-QaLogText $c), 'camera: (?:sync )?trace \d+: .*?; body -?[\d.]+ head \(-?[\d.]+ (-?[\d.]+)\)') | ForEach-Object { [double]$_.Groups[1].Value } | Measure-Object -Minimum -Maximum)
        $range = if ($yaws.Count) { $yaws.Maximum - $yaws.Minimum } else { 0.0 }
        Add-QaResult $c 'trace: the head swept (its yaw over a range of 4 deg or more in the trace)' ($range -ge 4) ('{0:N1} deg over {1} line(s)' -f $range, $yaws.Count)
    }
}
function New-QaAimVariant($Base, [ValidateSet('head', 'pad', 'view')][string]$Aim) {
    $v = $Base.Clone()
    $v.BaseAsserts = $Base.Asserts
    $v.Asserts = $script:QaAimAsserts
    $v.Name = "$($Base.Name)-$Aim"
    $v.OnlyByName = $true
    $v.Aim = $Aim
    $tilt = if ($Base.HeadTilt) { $Base.HeadTilt } else { 0 }
    $aimEnv = if ($Aim -eq 'view') { 'ETERNALVR_AIM=view' } else { 'ETERNALVR_AIM=head' }
    $v.Env = @($Base.Env | Where-Object { $_ -notlike 'ETERNALVR_AIM=*' }) + @($aimEnv, "ETERNALVR_TEST_HEAD_SWAY=$script:QaHeadSwayYaw,0,3,0,$tilt")
    if ($Aim -eq 'view') {
        $v.Proves = "$($Base.Proves) (view aim with the motion controllers: the game's own aim, the head tracked on top of it; the head sweeping)"
    } elseif ($Aim -eq 'pad') {
        $v.Env += 'ETERNALVR_CONTROLLERS=0'
        $v.Pad = $true
        $v.Args = @($Base.Args | Where-Object { $_ }) + '+in_joystick 1'
        $v.Proves = "$($Base.Proves) (gamepad mode: the motion controllers off, a virtual pad, head aim; the head sweeping)"
    } else {
        $v.Proves = "$($Base.Proves) (head aim with the motion controllers; the head sweeping)"
    }
    return $v
}
$pickupScenarios += @(foreach ($aim in 'head', 'pad', 'view') { foreach ($s in @($pickupScenarios)) { New-QaAimVariant $s $aim } })

# Before the launcher's end-to-end suite, which stays last.
$e2e = @($script:QaScenarios | Where-Object { $_.Kind -eq 'e2e' })
$script:QaScenarios = @($script:QaScenarios | Where-Object { $_.Kind -ne 'e2e' }) + $pickupScenarios + $e2e
