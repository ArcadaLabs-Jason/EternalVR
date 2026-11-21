# The camera takeovers the other scenario files leave out (docs/rig-findings/camera-takeovers.md, ROADMAP M7): glory
# kills in each of the four views, the player's death, a cutscene in both Cutscene views, swimming, climbable walls. Each checks the
# takeover's entry and exit lines and, from the camera trace (ETERNALVR_TEST_CAMERA_TRACE), what the view did: the
# general rule (the game camera's pitch and roll never reach the headset; the written view's pitch and roll are the
# head's) and the view's own rule. The simulator's head sways (ETERNALVR_TEST_HEAD_SWAY) so a view that only takes the
# head is told from one that holds still. All by name only (measurements of the takeovers), but death-camera, which
# every plain run takes (about 90 s; it caught the death camera's spin on the default path).
# Dot-sourced by qa-suite.ps1 after qa-scenarios-depth.ps1: added to $script:QaScenarios before the launcher's
# end-to-end suite.
#
#   glory-follow, glory-steady, glory-fade, glory-screen
#       e1m1's barge under hand aim (the launcher's default) with god, ETERNALVR_GLORY_KILLS unset (follow, the default
#       view, with the vignette on) or set: the barge's first encounter started from the debug schedule (its
#       checkpoint layer, the view at the barge's door), its three zombies brought in turn in front of the player
#       (`nextActiveAI;ai_teleportToPlayer trace`), staggered by shots (`ai_alwaysStagger 1`; `ai_sync_onlyDieFromGK
#       1`: only a glory kill kills) and glory-killed by a melee (the weapon hand's stick click), then the map loaded
#       again for two more (e1m3's cp_03_shoot_gate, where the other scenarios start, holds no live demon). Each: 5
#       kills or more, each `controllers: sync 'syncmelee/...' (a kill)` followed by `glory: kill N starts
#       (<view>...)` and its `glory: kill N ends` (none taken as ended at the time limit). Follow and steady: every
#       kill ended and has 20 trace lines or more. follow: in every kill's trace lines the written pitch and roll are
#       the head's (within 0.5 deg) while the kill's camera turns (pitch or yaw over 3 deg in some kill), and the body
#       yaw is the camera's on every line but the kill's first (within 2 deg), also while the camera is within 15 deg
#       of the player's view angles (positive control: the build before comfort::killCameraDrivesView, 45 of 221
#       lines off, the view jumping up to 25 deg between the two in one game frame); the vignette's two 10 s lines
#       after each kill's start count 30 game camera frames or more and the vignette at its full level. steady: the
#       written view less the head's angles stays where it was at the kill's start (its yaw, pitch and roll within 0.5
#       deg: the render rotation's change is the head's alone), the head moving meanwhile (its yaw over 1 deg in a
#       kill), and the view held its place. fade: the fade layer made, one `room: fade full ... after a glory kill
#       started (shown as a fade)` within 0.5 s of each kill's start. screen: each kill's start followed within 0.5 s
#       by an `xr: screen run N starts` line whose run ends within 1 s of the kill's end, the stereo pair run
#       started, no trace line of a head-tracked view inside a kill.
#   glory-follow-head, glory-steady-head, glory-fade-head, glory-screen-head
#       The same four under head aim, the head swaying round 20 degrees down so the teleported demon lands in reach and
#       the shots hit it; follow takes the body as the camera's yaw less one offset per kill (the head yaw the game's
#       aim holds).
#   death-camera
#       e1m3 cp_03_shoot_gate, the schedule's `kill` (as crosshair-hold), ETERNALVR_TEST_CAMERA_TRACE=all: exactly one
#       `game: the player died (death 1)` and then, after A on the death screen (its Load Checkpoint), `game: a living
#       player is back X s after death 1`; from the death to the player back the head-tracked views keep the head's
#       pitch and roll (within 0.5 deg), whatever the death camera does, and up to the reload's loading screen the
#       body turns 40 deg or less a game frame while the death camera went 80 deg down or more (it looks almost
#       straight down, rolling; positive control: the build before xr_math::yawOnly's change, 100 deg in a frame).
#   cutscene-screen, cutscene-around
#       e1m1's intro without ETERNALVR_SKIP_CINEMATICS (the suite's launches skip cutscenes otherwise), Cutscene view
#       on the flat screen (the default) or around the player (ETERNALVR_CUTSCENES=immersive): `game: cutscene starts
#       (<view>)` and `game: cutscene ends; the player's view is back`, no skip key held. screen: an `xr: screen run N
#       starts` within 1 s of the cutscene's start whose run ends within 1 s of its end, the stereo pair run started.
#       around: no screen run inside the cutscene, the cuts' re-base at its start and end (`cutscene: start, yaw
#       re-based`, `cutscene: end`), and in its trace lines the written pitch and roll are the head's (within 0.5
#       deg) while the cutscene camera's pitch moves over 3 deg.
#   swim
#       Atlantica (`e4m1_rig`) under hand aim, the player teleported into the water (`teleport bubbles_underwater_1`)
#       with the hand held 20 degrees down and 30 to the side: `controllers: swimming: the view follows the head` and
#       then `controllers: out of the water: the weapon hand aims again`; while swimming, the game's view yaw follows
#       the head's sway (over 4 deg of range in the trace) and the written pitch is the head's (within 0.5 deg).
#   climb-hand, climb-head
#       Exultia's climbable rock by its first signal (e1m2 `cp_09_first_signal`, `hell_func_dynamic_4057`, a half shell)
#       under hand or head aim, the head swaying, god. The game starts a climb on the melee button with the view's ray
#       on a climbable face (camera-takeovers.md row 8). Standing at the shell's axis facing its inner face: no press;
#       the stick click with the weapon hand turned 70 degrees off the wall (no climb under hand aim, a climb under
#       head aim); hand: the click with the hand at the wall. Then in the air 6 m out facing its outer face, stick clicks
#       from 1 s before the placement until a climb starts. A standing climb is held 5 s, then A jumps off (the game
#       jumps through the inner face); one from the air is held 3 s, the right stick turns the view 0.6 s, then A
#       jumps off. Checks: exactly the expected climbs per try, each `climb: climb N starts` with its `ends` line and
#       the gate's `controllers: forced view starts (climbable wall; ...)` within 0.1 s; held on the wall (trace) the
#       written pitch and roll are the head's (within 0.5 deg), the game's view yaw follows the head's sway (over 4
#       deg) and the body does not turn; within 1 s of each start and end the written view less the head's angles
#       steps 0.5 deg or less a game frame (no flick); the stick turned the view over 90 deg on the wall; each jump
#       off the wall heads where the view looked as the climb ended (within 10 deg, the game motion trace's first
#       velocity of 5 m/s or more). Positive control: `ETERNALVR_CLIMB_LOOK=0` (the game keeps the view on the wall)
#       fails the sway check.
#   climb-dash, climb-dash-head
#       As climb-hand and climb-head with the dash for the grab (`give ability_dash` first; the game's default
#       `dash_autoAttachWallClimb`: a dash along the view with the view's ray on a climbable face grabs it). Hand aim:
#       B with the weapon hand at the inner face, then with the hand 25 degrees off and the move stick forward; head
#       aim: B with the hand 70 degrees off and the stick forward; then in the air at the outer face, B and the stick
#       forward as soon after the placement as the input gets there (timed from the schedule's `where` 1 s before it by
#       the log's own clock; on the rig a dash 0.36 s or more after the placement came 1.5 m lower and missed the
#       face). The same checks as climb-hand.

# The camera trace line (camera_anim_hook.cpp traceView), `trace` (a forced view's and the 120 game views after it) or
# `sync trace` (any other, with ETERNALVR_TEST_CAMERA_TRACE=all), with the line's time in the log.
$script:QaTqTrace = '^\[\s*([\d.]+)\] \[\s*\d+\] camera: (?:sync )?trace \d+: game \((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\) angles \((?:(-?[\d.]+) (-?[\d.]+)|none)\) written \((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\) eye \([^)]*\)( forced)?(?:; body (-?[\d.]+) head \((-?[\d.]+) (-?[\d.]+)\))?'
# The head's sway for every scenario here: 8 degrees of yaw and 4 of pitch each way every 3 s.
$script:QaTqSway = 'ETERNALVR_TEST_HEAD_SWAY=8,4,3'

function Get-QaTqWrap([double]$Degrees) { return (($Degrees % 360) + 540) % 360 - 180 }

# The trace lines as objects: T (log seconds), the game camera's P Y R, the player's view angles AP AY (NaN for
# `none`), the written view's WP WY WR, Forced, and the body yaw and the head's HP HY (0 before the line has its aim
# part).
function Get-QaTqTrace($Ctx) {
    $rows = New-Object System.Collections.ArrayList
    foreach ($m in [regex]::Matches((Get-QaLogText $Ctx), $script:QaTqTrace, 'Multiline')) {
        $g = $m.Groups
        $v = { param($i) if ($g[$i].Success) { [double]$g[$i].Value } else { 0.0 } }
        $n = { param($i) if ($g[$i].Success) { [double]$g[$i].Value } else { [double]::NaN } }
        [void]$rows.Add([pscustomobject]@{ T = [double]$g[1].Value; P = & $v 2; Y = & $v 3; R = & $v 4; AP = & $n 5; AY = & $n 6
            WP = & $v 7; WY = & $v 8; WR = & $v 9; Forced = $g[10].Success; Body = & $v 11; HP = & $v 12; HY = & $v 13 })
    }
    return $rows
}

# The angle (degrees) between the game camera's forward and the player's view angles' forward on a trace line (NaN
# without the player's angles): over 15 the camera drives the view (xr_math::cameraDrivesView).
function Get-QaTqCameraOff($Row) {
    if ([double]::IsNaN($Row.AP)) { return [double]::NaN }
    $d = [Math]::PI / 180
    $p1 = [double]$Row.P * $d; $y1 = [double]$Row.Y * $d; $p2 = [double]$Row.AP * $d; $y2 = [double]$Row.AY * $d
    $dot = [Math]::Cos($p1) * [Math]::Cos($p2) * [Math]::Cos($y1 - $y2) + [Math]::Sin($p1) * [Math]::Sin($p2)
    return [Math]::Acos([Math]::Max(-1.0, [Math]::Min(1.0, $dot))) / $d
}

# The log lines matching $Pattern with their time: objects T, M (the match).
function Get-QaTqEvents($Ctx, [string]$Pattern) {
    $out = New-Object System.Collections.ArrayList
    foreach ($m in [regex]::Matches((Get-QaLogText $Ctx), '^\[\s*([\d.]+)\] \[\s*\d+\] .*?' + $Pattern, 'Multiline')) {
        [void]$out.Add([pscustomobject]@{ T = [double]$m.Groups[1].Value; M = $m })
    }
    return $out
}

# Episodes from their start and end lines: `<what> N starts` / `<what> N ends` (or any pattern with the number in
# group 2): objects N, Start, End (-1 without an end line), Text (the start line).
function Get-QaTqEpisodes($Ctx, [string]$StartPattern, [string]$EndPattern) {
    $starts = @(Get-QaTqEvents $Ctx $StartPattern)
    $ends = @(Get-QaTqEvents $Ctx $EndPattern)
    $out = New-Object System.Collections.ArrayList
    foreach ($s in $starts) {
        $n = $s.M.Groups[2].Value
        $e = $ends | Where-Object { $_.M.Groups[2].Value -eq $n -and $_.T -ge $s.T } | Select-Object -First 1
        [void]$out.Add([pscustomobject]@{ N = [int]$n; Start = $s.T; End = $(if ($e) { $e.T } else { -1.0 }); Text = $s.M.Value })
    }
    return $out
}

# The trace lines from $From to $To (log seconds).
function Select-QaTqSpan($Rows, [double]$From, [double]$To) { return @($Rows | Where-Object { $_.T -ge $From -and $_.T -le $To }) }

# Over trace lines: the largest distance of the written pitch and roll from the head's (the head's roll is 0 with
# the sway), and the ranges of the game camera's pitch, yaw and roll and of the written view less the head's.
function Measure-QaTqSpan($Rows) {
    $Rows = @($Rows | Where-Object { $_ }) # an empty span arrives as $null
    $r =[ordered]@{ Lines = @($Rows).Count; Tilt = 0.0; GameP = 0.0; GameY = 0.0; GameR = 0.0; KeptY = 0.0; KeptP = 0.0; KeptR = 0.0; HeadY = 0.0; BodyTurn = 0.0 }
    if ($r.Lines -eq 0) { return $r }
    $first = $Rows[0]
    $lo = @{}; $hi = @{}
    $note = { param($k, $d) if (-not $lo.ContainsKey($k) -or $d -lt $lo[$k]) { $lo[$k] = $d }; if (-not $hi.ContainsKey($k) -or $d -gt $hi[$k]) { $hi[$k] = $d } }
    foreach ($x in $Rows) {
        $r.Tilt = [Math]::Max($r.Tilt, [Math]::Max([Math]::Abs($x.WP - $x.HP), [Math]::Abs($x.WR)))
        & $note 'gp' $x.P; & $note 'gr' $x.R
        & $note 'gy' (Get-QaTqWrap ($x.Y - $first.Y))
        & $note 'ky' (Get-QaTqWrap ($x.WY - $x.HY - ($first.WY - $first.HY)))
        & $note 'kp' ($x.WP - $x.HP - ($first.WP - $first.HP))
        & $note 'kr' ($x.WR - $first.WR)
        & $note 'hy' $x.HY
        $r.BodyTurn = [Math]::Max($r.BodyTurn, [Math]::Abs((Get-QaTqWrap ($x.Body - $first.Body))))
    }
    $r.GameP = $hi.gp - $lo.gp; $r.GameY = $hi.gy - $lo.gy; $r.GameR = $hi.gr - $lo.gr
    $r.KeptY = $hi.ky - $lo.ky; $r.KeptP = $hi.kp - $lo.kp; $r.KeptR = $hi.kr - $lo.kr; $r.HeadY = $hi.hy - $lo.hy
    return $r
}

function Format-QaTqSpan($m) {
    return ('{0} line(s): written pitch/roll at most {1:N2} deg from the head''s; game camera ranges pitch {2:N1} yaw {3:N1} roll {4:N1}; the view less the head moved yaw {5:N2} pitch {6:N2} roll {7:N2}; head yaw range {8:N1}; body turned up to {9:N1}' -f
        $m.Lines, $m.Tilt, $m.GameP, $m.GameY, $m.GameR, $m.KeptY, $m.KeptP, $m.KeptR, $m.HeadY, $m.BodyTurn)
}

# ---- Glory kills ------------------------------------------------------------------------------------------------

$script:QaGloryKills = 5
$script:QaGloryRest = 'right.position = 0.2, -0.3, -0.35'
# e1m1's barge, where the first encounter's three zombies come at the player (the perf scene of the rig's runs):
# the checkpoint's layer, the view at the barge's door, the encounter started. Each of the three is teleported in
# front of the player in turn, then the map is loaded again (its intro skipped) for two more (with three spare
# teleports: in run tq6 the second wave gave one kill in three tries).
# Only a glory kill kills them (ai_sync_onlyDieFromGK 1) and any hit staggers them (ai_alwaysStagger 1).
$script:QaGloryWave = '{0}:god;g_setting_hud_auto_dismiss_tutorials 1;g_setting_tutorials 0;ai_alwaysStagger 1;ai_sync_onlyDieFromGK 1|{1}:activateCheckPoint checkpoint_target_change_layer_cp_01|{2}:setviewpos 36 -1590 18.75 90|{3}:activatetargets barge_trigger_facing_first_door'
$script:QaGloryTeleports = @(35, 51, 67, 145, 161, 177, 193, 209)
$script:QaGloryTeleport = '{0}:nextActiveAI;ai_teleportToPlayer trace'
$script:QaGlorySchedule = (@(($script:QaGloryWave -f 20, 21, 24, 25)) + @($script:QaGloryTeleports[0..2] | ForEach-Object { $script:QaGloryTeleport -f $_ }) +
    '85:map game/sp/e1m1_intro/e1m1_intro' + @(($script:QaGloryWave -f 130, 131, 134, 135)) +
    @($script:QaGloryTeleports[3..7] | ForEach-Object { $script:QaGloryTeleport -f $_ })) -join '|'

# Each teleport: shots at the demon, then a melee, until a glory kill's sync starts (four tries), until five kills.
$script:QaGloryTimeline = {
    param($c)
    $rest = $script:QaGloryRest
    Write-QaInput $c @('right.aim = 0, -25', $rest)
    $k = 0
    foreach ($at in $script:QaGloryTeleports) {
        if (([regex]::Matches((Get-QaLogText $c), "glory: kill \d+ starts")).Count -ge $script:QaGloryKills) { break }
        $k++
        if (-not (Wait-QaLog $c ('debug-commands: {0}\.0 s: nextActiveAI' -f $at) 120)) { Add-QaNote $c "teleport $k : the one at $at s was not logged"; continue }
        Wait-QaSeconds $c 1
        $before = ([regex]::Matches((Get-QaLogText $c), "controllers: sync 'syncmelee/")).Count
        $got = $false
        for ($t = 1; $t -le 4 -and -not $got; $t++) {
            foreach ($p in -10, -18) {
                Write-QaInput $c @("right.aim = 0, $p", $rest, 'right.trigger = 1'); Wait-QaSeconds $c 0.15
                Write-QaInput $c @("right.aim = 0, $p", $rest); Wait-QaSeconds $c 0.6
            }
            Write-QaInput $c @('right.aim = 0, -15', $rest, 'right.click = 1'); Wait-QaSeconds $c 0.3
            Write-QaInput $c @('right.aim = 0, -15', $rest)
            Wait-QaSeconds $c 0.5
            if ($k -eq 1 -and $t -eq 1) { Save-QaXrShot $c 'kill1' }
            Wait-QaSeconds $c 1
            $got = ([regex]::Matches((Get-QaLogText $c), "controllers: sync 'syncmelee/")).Count -gt $before
        }
        if (-not $got) { Add-QaNote $c "teleport $k : no glory kill after 4 tries" }
        Write-QaInput $c @('right.aim = 0, -25', $rest)
        Wait-QaSeconds $c 3
    }
    # The last kill's 10 s lines (the vignette's, the stats) before the stop.
    Wait-QaSeconds $c 11
    Save-QaXrShot $c 'after'
}

# The kills (objects N, Start, End), checked as every view shares: each syncmelee sync's line (a kill) followed by
# the episode's start in this view, and its end.
function Test-QaGloryEpisodes($Ctx, [string]$View) {
    $starts = @(Get-QaTqEvents $Ctx "glory: kill (\d+) starts \($View")
    $ends = @(Get-QaTqEvents $Ctx 'glory: kill (\d+) ends; the view''s body turned')
    $syncs = @(Get-QaTqEvents $Ctx "controllers: sync 'syncmelee/[^']+' \(a kill\)")
    $kills = New-Object System.Collections.ArrayList
    $unmatched = 0
    foreach ($s in $starts) {
        $n = $s.M.Groups[2].Value
        $e = $ends | Where-Object { $_.M.Groups[2].Value -eq $n -and $_.T -ge $s.T } | Select-Object -First 1
        $sync = $syncs | Where-Object { $_.T -le $s.T + 0.05 -and $_.T -ge $s.T - 0.5 } | Select-Object -First 1
        if (-not $e -or -not $sync) { $unmatched++ }
        [void]$kills.Add([pscustomobject]@{ N = [int]$n; Start = $s.T; End = $(if ($e) { $e.T } else { -1.0 }) })
    }
    Add-QaResult $Ctx "$script:QaGloryKills glory kills or more: each syncmelee sync (a kill) followed by glory: kill N starts ($View) and its end" `
        ($kills.Count -ge $script:QaGloryKills -and $unmatched -eq 0) ('{0} kill(s), {1} syncmelee sync line(s), {2} without its sync or end; {3}' -f $kills.Count, $syncs.Count, $unmatched,
            ((@($kills) | ForEach-Object { '{0}: {1:N2}-{2:N2} s' -f $_.N, $_.Start, $_.End }) -join ', '))
    Test-QaAbsent $Ctx 'glory: kill \d+ still running after' -Regex
    return $kills
}

# Every kill traced: an end line and 20 trace lines or more between its start and end (the aggregate checks below
# would pass on a kill with none).
$script:QaGloryMinLines = 20
function Test-QaGloryTraced($Ctx, $Kills, $Rows) {
    $text = @($Kills | ForEach-Object { 'kill {0}: {1}' -f $_.N, $(if ($_.End -lt 0) { 'no end' } else { "$(@(Select-QaTqSpan $Rows $_.Start $_.End).Count) line(s)" }) })
    $short = @($Kills | Where-Object { $_.End -lt 0 -or @(Select-QaTqSpan $Rows $_.Start $_.End).Count -lt $script:QaGloryMinLines }).Count
    Add-QaResult $Ctx "trace: every kill ended and has $script:QaGloryMinLines trace lines or more" ($Kills.Count -ge $script:QaGloryKills -and $short -eq 0) ($text -join ', ')
}

function New-QaGloryScenario([string]$View, [string]$Proves, [string[]]$ExtraEnv, [scriptblock]$Checks) {
    return @{
        Name = "glory-$View"; Kind = 'game'; OnlyByName = $true; GloryView = $View; Aim = 'hand'
        Proves = $Proves
        Map = 'game/sp/e1m1_intro/e1m1_intro'
        Env = @('ETERNALVR_AIM=hand', 'ETERNALVR_TEST_CAMERA_TRACE=1', $script:QaTqSway, "ETERNALVR_DEBUG_COMMANDS=$script:QaGlorySchedule") + $ExtraEnv
        Input = @('right.aim = 0, -25', $script:QaGloryRest)
        Timeline = $script:QaGloryTimeline
        GloryChecks = $Checks
        Asserts = {
            param($c)
            $kills = Test-QaGloryEpisodes $c $c.Scenario.GloryView
            & $c.Scenario.GloryChecks $c $kills
        }
    }
}

$takeoverScenarios = @(
    (New-QaGloryScenario 'follow' 'Glory kills with follow the camera (the default): 5 kills each log their start and end, the view faces the kill''s camera and its pitch and roll stay the head''s; the vignette darkens during them' @('ETERNALVR_VIGNETTE=light') {
        param($c, $kills)
        $rows = Get-QaTqTrace $c
        Test-QaGloryTraced $c $kills $rows
        $tilt = 0.0; $turn = 0.0; $lines = 0; $wrong = 0; $driven = 0; $step = 0.0; $text = @()
        foreach ($k in $kills) {
            if ($k.End -lt 0) { continue }
            $span = Select-QaTqSpan $rows $k.Start $k.End
            $m = Measure-QaTqSpan $span
            $lines += $m.Lines
            $tilt = [Math]::Max($tilt, $m.Tilt); $turn = [Math]::Max($turn, [Math]::Max($m.GameP, $m.GameY))
            # The body: the camera's yaw on every line of the kill but its first (the camera may take the view a
            # game frame after the sync), also while the camera is 15 degrees or less off the player's view angles
            # (comfort::killCameraDrivesView; before, the view jumped between the two there). Under hand aim the
            # camera's yaw itself; under head aim less the head yaw the game's aim holds (xr_math::drivenBodyYaw),
            # one offset for the whole kill (nothing is written while the camera drives).
            $after = @($span | Select-Object -Skip 1)
            $offset = if ($c.Scenario.Aim -eq 'head' -and $after.Count) { Get-QaTqWrap ($after[0].Body - $after[0].Y) } else { 0.0 }
            foreach ($x in $after) {
                $off = Get-QaTqCameraOff $x
                if (-not [double]::IsNaN($off) -and $off -le 15) { $driven++ }
                if ([Math]::Abs((Get-QaTqWrap ($x.Body - $x.Y - $offset))) -gt 2) { $wrong++ }
            }
            for ($i = 1; $i -lt $span.Count; $i++) {
                $step = [Math]::Max($step, [Math]::Abs((Get-QaTqWrap (($span[$i].WY - $span[$i].HY) - ($span[$i - 1].WY - $span[$i - 1].HY)))))
            }
            $text += "kill $($k.N): $(Format-QaTqSpan $m)"
        }
        $detail = $text -join '; '
        Add-QaResult $c 'trace: in the kills the written pitch and roll are the head''s (within 0.5 deg) while the kill''s camera turns (over 3 deg)' ($lines -ge 20 -and $tilt -le 0.5 -and $turn -gt 3) $detail
        Add-QaResult $c 'trace: the body yaw is the kill camera''s on every line of each kill but its first (within 2 deg), also within 15 deg of the player''s view angles' ($lines -ge 20 -and $driven -ge 1 -and $wrong -eq 0) "$wrong of $lines line(s) off ($driven line(s) with the camera within 15 deg of the player's angles); $detail"
        Add-QaNote $c ('the written yaw less the head''s: largest step between two game frames in a kill {0:N1} deg' -f $step)
        # The vignette's 10 s line after each kill's start: the kill's frames counted as the game moving the camera,
        # the vignette shown up to its full level (no stick moved in the run).
        $vig = @(Get-QaTqEvents $c 'vignette: light, shown (\d+) of \d+ frame\(s\), max level (\d+) of (\d+);.*?game camera (\d+) frame\(s\)')
        $dark = 0; $vt = @()
        foreach ($k in $kills) {
            # The two lines after its start (a kill can straddle one).
            $v = @($vig | Where-Object { $_.T -gt $k.Start -and $_.T -le $k.Start + 20.5 } | Select-Object -First 2)
            $frames = 0; $shown = 0; $full = $false
            foreach ($x in $v) {
                $g = $x.M.Groups
                $frames += [int]$g[5].Value; $shown += [int]$g[2].Value
                $full = $full -or ([int]$g[3].Value -eq [int]$g[4].Value -and [int]$g[5].Value -gt 0)
            }
            $vt += 'kill {0}: {1} game camera frame(s), {2} shown, full level {3}' -f $k.N, $frames, $shown, $full
            if ($frames -ge 30 -and $shown -gt 0 -and $full) { $dark++ }
        }
        Add-QaResult $c 'vignette: the 10 s lines after each kill''s start count 30 game camera frames or more and the vignette shown at its full level' ($kills.Count -ge $script:QaGloryKills -and $dark -eq $kills.Count) ($vt -join '; ')
    }),
    (New-QaGloryScenario 'steady' 'Glory kills with the steady view: 5 kills each log their start and end, and the view''s rotation changes only with the head''s during them' @('ETERNALVR_GLORY_KILLS=steady') {
        param($c, $kills)
        Test-QaPresent $c 'glory: ETERNALVR_GLORY_KILLS=steady'
        $rows = Get-QaTqTrace $c
        Test-QaGloryTraced $c $kills $rows
        $kept = 0.0; $head = 0.0; $lines = 0; $text = @()
        foreach ($k in $kills) {
            if ($k.End -lt 0) { continue }
            $m = Measure-QaTqSpan (Select-QaTqSpan $rows $k.Start $k.End)
            $lines += $m.Lines
            $kept = [Math]::Max($kept, [Math]::Max($m.KeptY, [Math]::Max($m.KeptP, [Math]::Max($m.KeptR, $m.Tilt))))
            $head = [Math]::Max($head, $m.HeadY)
            $text += "kill $($k.N): $(Format-QaTqSpan $m)"
        }
        Add-QaResult $c 'trace: in each kill the written view less the head''s angles stays as at its start (yaw, pitch, roll within 0.5 deg): the rotation changes by the head''s alone' ($lines -ge 20 -and $kept -le 0.5) ($text -join '; ')
        Add-QaResult $c 'trace: the head moved during the kills (its yaw over 1 deg in a kill)' ($head -gt 1) ('{0:N2} deg' -f $head)
        $held = @(Get-QaMatches $c 'glory: kill \d+: the view held its place' -Regex).Count
        Add-QaResult $c "the view held its place in every kill ($script:QaGloryKills lines or more)" ($held -ge $script:QaGloryKills) "$held line(s)"
    }),
    (New-QaGloryScenario 'fade' 'Glory kills shown as a fade: 5 kills each log their start and end, and the view fades out at each' @('ETERNALVR_GLORY_KILLS=fade') {
        param($c, $kills)
        Test-QaPresent $c 'glory: ETERNALVR_GLORY_KILLS=fade'
        Test-QaPresent $c 'room: fade layer ready'
        $fades = @(Get-QaTqEvents $c 'room: fade full (\d+) ms after a glory kill started \(shown as a fade\)')
        $hit = @($kills | Where-Object { $s = $_.Start; @($fades | Where-Object { $_.T -ge $s -and $_.T -le $s + 0.5 }).Count -ge 1 }).Count
        Add-QaResult $c 'a fade full within 0.5 s of each kill''s start' ($kills.Count -ge $script:QaGloryKills -and $hit -eq $kills.Count) ('{0} of {1} kill(s); fade lines: {2}' -f $hit, $kills.Count, (($fades | ForEach-Object { $_.M.Groups[2].Value + ' ms' }) -join ', '))
    }),
    (New-QaGloryScenario 'screen' 'Glory kills on the flat screen: 5 kills each log their start and end, and each plays on the screen as its own screen run' @('ETERNALVR_GLORY_KILLS=screen') {
        param($c, $kills)
        Test-QaPresent $c 'glory: ETERNALVR_GLORY_KILLS=screen'
        Test-QaPresent $c 'cinema3D: pair run started'
        $runs = @(Get-QaTqEpisodes $c 'xr: screen run (\d+) starts' 'xr: screen run (\d+) ends after')
        $rows = Get-QaTqTrace $c
        $ok = 0; $inside = 0; $text = @()
        foreach ($k in $kills) {
            $r = $runs | Where-Object { $_.Start -ge $k.Start -and $_.Start -le $k.Start + 0.5 } | Select-Object -First 1
            if ($r -and $k.End -ge 0 -and $r.End -ge 0 -and [Math]::Abs($r.End - $k.End) -le 1) { $ok++ }
            if ($k.End -ge 0) { $inside += @(Select-QaTqSpan $rows ($k.Start + 0.1) ($k.End - 0.1)).Count }
            $text += 'kill {0} {1:N2}-{2:N2} s: run {3}' -f $k.N, $k.Start, $k.End, $(if ($r) { '{0} {1:N2}-{2:N2} s' -f $r.N, $r.Start, $r.End } else { 'none' })
        }
        Add-QaResult $c 'each kill on the screen: a screen run from within 0.5 s of its start to within 1 s of its end' ($kills.Count -ge $script:QaGloryKills -and $ok -eq $kills.Count) ($text -join '; ')
        Add-QaResult $c 'trace: no head-tracked view written inside a kill' ($inside -eq 0) "$inside line(s)"
    })
)

# Head aim (ETERNALVR_AIM=head; hand aim is the launcher's default): the four views once more, the head swaying 8
# degrees of yaw and 8 of pitch round 20 down (the schedule's teleport puts the demon where the view looks, 3 to 10 m
# ahead, and the shots go where the head looks).
$takeoverScenarios += @(foreach ($base in @($takeoverScenarios | Where-Object { $_.GloryView })) {
    $v = $base.Clone()
    $v.Name = "$($base.Name)-head"
    $v.Aim = 'head'
    $v.Env = @($base.Env | ForEach-Object { if ($_ -eq 'ETERNALVR_AIM=hand') { 'ETERNALVR_AIM=head' } elseif ($_ -eq $script:QaTqSway) { 'ETERNALVR_TEST_HEAD_SWAY=8,8,3,0,-20' } else { $_ } })
    $v.Proves = "$($base.Proves) (head aim, the head swaying round 20 degrees down)"
    $v
})

# ---- The player's death -----------------------------------------------------------------------------------------

$script:QaDeathSchedule = '3:g_setting_hud_auto_dismiss_tutorials 1;g_setting_tutorials 0|15:kill'
$takeoverScenarios += @{
    Name = 'death-camera'; Kind = 'game'
    Proves = 'The player''s death logs its start and the living player''s return, and the death camera''s pitch and roll never reach the written view'
    Map = 'game/sp/e1m3_cult/e1m3_cult -checkpoint cp_03_shoot_gate'
    Env = @('ETERNALVR_AIM=hand', 'ETERNALVR_TEST_CAMERA_TRACE=all', $script:QaTqSway, "ETERNALVR_DEBUG_COMMANDS=$script:QaDeathSchedule")
    Input = $script:QaRestPose
    Timeline = {
        param($c)
        if (Wait-QaLog $c 'debug-commands: [\d.]+ s: kill' 40) {
            $at = 0.0
            foreach ($t in 0.5, 1.5, 3, 6) { Wait-QaSeconds $c ($t - $at); $at = $t; Save-QaShot $c "death-$($t)s"; Save-QaXrShot $c "death-$($t)s" }
        } else { Add-QaNote $c 'the schedule''s kill was not logged' }
        # The death screen waits for its Load Checkpoint (its cursor, a popup over the game: A sends Space).
        $back = $false
        for ($i = 1; $i -le 3 -and -not $back; $i++) {
            Write-QaInput $c ($script:QaRestPose + 'right.primary = 1'); Wait-QaSeconds $c 0.3
            Write-QaInput $c $script:QaRestPose
            $back = Wait-QaLog $c 'game: a living player is back' 12
        }
        if (-not $back) { Add-QaNote $c 'no living player back after three presses of A on the death screen' }
        Wait-QaSeconds $c 4; Save-QaShot $c 'back'
    }
    Asserts = {
        param($c)
        Test-QaCount $c 'game: the player died (death 1): the death camera is the game''s, the view keeps the head''s pitch and roll' 1
        Test-QaAbsent $c 'game: the player died (death 2)'
        $died = @(Get-QaTqEvents $c 'game: the player died \(death 1\)') | Select-Object -First 1
        $back = @(Get-QaTqEvents $c 'game: a living player is back ([\d.]+) s after death 1') | Select-Object -First 1
        Add-QaResult $c 'game: a living player is back X s after death 1, after the death' ($died -and $back -and $back.T -gt $died.T) $(if ($back) { $back.M.Value -replace '^.*?game: ', '' } else { 'no line' })
        if ($died -and $back) {
            $span = Select-QaTqSpan (Get-QaTqTrace $c) $died.T $back.T
            $m = Measure-QaTqSpan $span
            Add-QaResult $c 'trace: from the death to the player back the written pitch and roll are the head''s (within 0.5 deg)' ($m.Lines -ge 20 -and $m.Tilt -le 0.5) (Format-QaTqSpan $m)
            # The death camera looks almost straight down, rolling: its forward's heading swings round while its
            # image hardly turns, and the body took it (100 degrees in a game frame before xr_math::yawOnly took
            # the heading from the view's up there; after, 13 at most with the headings' angles blended and 25.5 to 25.9
            # with their vectors, b7ea3c06, as the camera rises through 80 degrees and the heading turns from the
            # up's to the forward's; 40 leaves room for a frame that took twice as long).
            # Up to the reload's loading screen (on the screen): the first view after it faces the checkpoint's start.
            $load = @(Get-QaTqEvents $c 'xr: screen run \d+ starts') | Where-Object { $_.T -gt $died.T -and $_.T -lt $back.T } | Select-Object -First 1
            $until = if ($load) { $load.T } else { $back.T }
            $camera = @($span | Where-Object { $_.T -lt $until })
            $step = 0.0
            for ($i = 1; $i -lt $camera.Count; $i++) { $step = [Math]::Max($step, [Math]::Abs((Get-QaTqWrap ($camera[$i].Body - $camera[$i - 1].Body)))) }
            Add-QaResult $c 'trace: the body yaw turns 40 deg or less between two game frames from the death to the reload''s loading screen' ($camera.Count -ge 20 -and $step -le 40) ('largest step {0:N1} deg over {1} line(s) to {2:N2} s' -f $step, $camera.Count, $until)
            # The step check means something only if the death camera went near the vertical (89 degrees down on
            # the rig; xr_math::yawOnly turns to the view's up from 75): the game's pitch (down positive) reached 80.
            $down = if ($camera.Count) { ($camera | Measure-Object -Property P -Maximum).Maximum } else { -999.0 }
            Add-QaResult $c 'trace: the death camera looked 80 deg down or more (the case the step check is for)' ($down -ge 80) ('{0:N1} deg at most' -f $down)
            if ($span.Count) { Add-QaNote $c ('the death camera: {0} head-tracked view(s) from {1:N2} s to {2:N2} s of the {3:N2} s to the player back' -f $span.Count, $span[0].T, $span[-1].T, ($back.T - $died.T)) }
        }
    }
}

# ---- A cutscene -------------------------------------------------------------------------------------------------

# e1m1's intro, played (the suite's launches skip cutscenes with ETERNALVR_SKIP_CINEMATICS=1 otherwise).
$script:QaCutsceneTimeline = {
    param($c)
    if (-not (Wait-QaLog $c 'game: cutscene starts' 120)) { Add-QaNote $c 'no cutscene within 120 s'; return }
    for ($i = 1; $i -le 4; $i++) { Wait-QaSeconds $c 6; Save-QaShot $c "cutscene-$i"; Save-QaXrShot $c "cutscene-$i" }
    if (-not (Wait-QaLog $c 'game: cutscene ends' 240)) { Add-QaNote $c 'the cutscene did not end within 240 s'; return }
    Wait-QaSeconds $c 5; Save-QaShot $c 'after'; Save-QaXrShot $c 'after'
}

function Get-QaCutscene($Ctx) {
    $s = @(Get-QaTqEvents $Ctx 'game: cutscene starts') | Select-Object -First 1
    $e = if ($s) { @(Get-QaTqEvents $Ctx 'game: cutscene ends; the player''s view is back') | Where-Object { $_.T -gt $s.T } | Select-Object -First 1 } else { $null }
    return [pscustomobject]@{ Start = $(if ($s) { $s.T } else { -1.0 }); End = $(if ($e) { $e.T } else { -1.0 }); Text = $(if ($s) { $s.M.Value -replace '^.*?game: ', '' } else { '' }) }
}

function New-QaCutsceneScenario([string]$View, [string]$Proves, [string[]]$ExtraEnv, [scriptblock]$Checks) {
    return @{
        Name = "cutscene-$View"; Kind = 'game'; OnlyByName = $true
        Proves = $Proves
        Map = 'game/sp/e1m1_intro/e1m1_intro'
        Env = @('ETERNALVR_SKIP_CINEMATICS=0', 'ETERNALVR_AIM=hand', 'ETERNALVR_TEST_CAMERA_TRACE=1', $script:QaTqSway) + $ExtraEnv
        Input = $script:QaRestPose
        Timeline = $script:QaCutsceneTimeline
        CutsceneChecks = $Checks
        Asserts = {
            param($c)
            $cs = Get-QaCutscene $c
            Add-QaResult $c 'game: cutscene starts, then game: cutscene ends; the player''s view is back' ($cs.Start -ge 0 -and $cs.End -gt $cs.Start) ('{0}: {1:N2} s to {2:N2} s' -f $cs.Text, $cs.Start, $cs.End)
            Test-QaAbsent $c 'game: holding the skip key'
            & $c.Scenario.CutsceneChecks $c $cs
        }
    }
}

$takeoverScenarios += @(
    (New-QaCutsceneScenario 'screen' 'e1m1''s intro played with Cutscene view on the flat screen (the default): the cutscene''s start and end, and its frames on the screen as one screen run' @() {
        param($c, $cs)
        Test-QaPresent $c 'game: cutscene starts (on the flat screen)'
        Test-QaPresent $c 'cinema3D: pair run started'
        $runs = @(Get-QaTqEpisodes $c 'xr: screen run (\d+) starts' 'xr: screen run (\d+) ends after')
        $r = $runs | Where-Object { $_.Start -ge $cs.Start - 0.1 -and $_.Start -le $cs.Start + 1 } | Select-Object -First 1
        Add-QaResult $c 'a screen run from within 1 s of the cutscene''s start to within 1 s of its end' ($cs.End -gt 0 -and $r -and $r.End -ge 0 -and [Math]::Abs($r.End - $cs.End) -le 1) `
            ('cutscene {0:N2}-{1:N2} s; runs: {2}' -f $cs.Start, $cs.End, (($runs | ForEach-Object { '{0} {1:N2}-{2:N2} s' -f $_.N, $_.Start, $_.End }) -join ', '))
    }),
    (New-QaCutsceneScenario 'around' 'e1m1''s intro played with Cutscene view around the player: the cutscene''s start and end, its cuts re-based, no screen run, and the camera''s pitch and roll never reach the written view' @('ETERNALVR_CUTSCENES=immersive') {
        param($c, $cs)
        Test-QaPresent $c 'game: cutscene starts (around the player; each cut re-based to where the head looks)'
        Test-QaPresent $c 'cutscene: start, yaw re-based by'
        Test-QaPresent $c 'cutscene: end, the cuts'' re-base of'
        $runs = @(Get-QaTqEpisodes $c 'xr: screen run (\d+) starts' 'xr: screen run (\d+) ends after' | Where-Object { $cs.End -gt 0 -and $_.Start -gt $cs.Start + 0.5 -and $_.Start -lt $cs.End - 0.5 })
        Add-QaResult $c 'no screen run inside the cutscene' ($cs.End -gt 0 -and $runs.Count -eq 0) "$($runs.Count) run(s)"
        if ($cs.End -gt 0) {
            $m = Measure-QaTqSpan (Select-QaTqSpan (Get-QaTqTrace $c) $cs.Start $cs.End)
            Add-QaResult $c 'trace: in the cutscene the written pitch and roll are the head''s (within 0.5 deg) while the camera''s pitch moves over 3 deg' ($m.Lines -ge 100 -and $m.Tilt -le 0.5 -and $m.GameP -gt 3) (Format-QaTqSpan $m)
        }
    })
)

# ---- Swimming ---------------------------------------------------------------------------------------------------

$script:QaSwimSchedule = '3:god;g_setting_hud_auto_dismiss_tutorials 1;g_setting_tutorials 0|6:teleport bubbles_underwater_1'
$script:QaSwimHand = @('right.aim = 30, -20', 'right.position = 0.2, -0.3, -0.35')
$takeoverScenarios += @{
    Name = 'swim'; Kind = 'game'; OnlyByName = $true
    Proves = 'Swimming under hand aim: the swim''s start and end lines, the view follows the head under water and its pitch stays the head''s'
    Map = 'game/dlc/e4m1_rig/e4m1_rig'
    Env = @('ETERNALVR_AIM=hand', 'ETERNALVR_TEST_CAMERA_TRACE=all', $script:QaTqSway, "ETERNALVR_DEBUG_COMMANDS=$script:QaSwimSchedule")
    Input = $script:QaSwimHand
    Timeline = {
        param($c)
        if (-not (Wait-QaLog $c 'controllers: swimming: the view follows the head' 40)) { Add-QaNote $c 'no swimming line within 40 s'; return }
        Wait-QaSeconds $c 2; Save-QaShot $c 'swimming'; Save-QaXrShot $c 'swimming'
        if (-not (Wait-QaLog $c 'controllers: out of the water' 40)) { Add-QaNote $c 'still in the water 40 s later' }
        Wait-QaSeconds $c 3; Save-QaShot $c 'after'
    }
    Asserts = {
        param($c)
        $s = @(Get-QaTqEvents $c 'controllers: swimming: the view follows the head') | Select-Object -First 1
        $e = if ($s) { @(Get-QaTqEvents $c 'controllers: out of the water: the weapon hand aims again') | Where-Object { $_.T -gt $s.T } | Select-Object -First 1 } else { $null }
        Add-QaResult $c 'controllers: swimming: the view follows the head, then controllers: out of the water: the weapon hand aims again' ($s -and $e) $(if ($s -and $e) { '{0:N2}-{1:N2} s' -f $s.T, $e.T } else { 'unmatched' })
        if ($s -and $e) {
            $m = Measure-QaTqSpan (Select-QaTqSpan (Get-QaTqTrace $c) ($s.T + 0.2) ($e.T - 0.2))
            Add-QaResult $c 'trace: while swimming the game''s view yaw follows the head''s sway (over 4 deg of range) and the written pitch and roll are the head''s (within 0.5 deg)' ($m.Lines -ge 50 -and $m.GameY -gt 4 -and $m.Tilt -le 0.5) (Format-QaTqSpan $m)
        }
    }
}

# ---- Climbable walls --------------------------------------------------------------------------------------------

# Exultia's climbable rock by its first signal (e1m2 cp_09_first_signal), hell_func_dynamic_4057: a half shell of
# about 4 m round (-52.8, -615.6), facing 52.5 degrees. The game starts a climb when the melee button goes down while
# the view's ray hits a climbable face within 7.5 m (camera-takeovers.md row 8). Two places: 'inner', standing at the
# shell's axis facing its inner face about 4 m away (52.5 degrees), and 'outer', in the air 6 m out from the axis
# facing its outer face (232.5 degrees), where the player starts to fall into the pit at once.
$script:QaClimbPlaces = @{ inner = '-52.80 -615.60 -11.9 52.5'; outer = '-48.54 -610.05 -12.0 232.5' }
$script:QaClimbPer = 16
$script:QaClimbMotion = '^\[\s*([\d.]+)\] \[\s*\d+\] game motion trace: .*?; velocity (-?[\d.]+) (-?[\d.]+) (-?[\d.]+) \((-?[\d.]+)\)'

function Get-QaClimbPose([string]$Aim) {
    return @("right.aim = $Aim", 'right.position = 0.2, -0.3, -0.35', 'left.aim = 0, 30', 'left.position = -0.2, -0.45, -0.25')
}

function Get-QaClimbStarts($Ctx) { return ([regex]::Matches((Get-QaLogText $Ctx), 'climb: climb \d+ starts')).Count }

# Each try (Face, Hand, Do, Climb: whether a climb is expected), with the weapon hand at the wall for the placement:
#   'none'   inner: no press;
#   'melee'  inner: the hand turned to Hand (yaw, pitch from the body) 1 s after the placement, then a stick click;
#            a climb is held 5 s, then A jumps off (looking at the inner face: the game jumps through it);
#   'air'    outer: stick clicks every 0.2 s from before the placement until a climb starts or 0.8 s after it; a
#            climb is held 3 s, the right stick turns the view 0.6 s (about 140 degrees), then A jumps off.
#   'dash'   inner: as 'melee' with B (dash) for the stick click, the move stick held at Stick (if any) with it;
#   'dash-air' outer: B with the move stick forward as soon after the placement as the input gets there (the
#            placement's fall soon takes the player below the face); held and jumped off as 'air'.
$script:QaClimbTimeline = {
    param($c)
    $t = 6
    $k = 0
    foreach ($p in $c.Scenario.ClimbTries) {
        $k++
        $pose = Get-QaClimbPose '0, 0'
        Write-QaInput $c $pose
        $pat = 'debug-commands: {0}\.0 s: setviewpos' -f $t
        $t += $script:QaClimbPer
        if ($p.Do -eq 'dash-air') {
            # Timed from the schedule's `where` 1 s before the placement by the log's own clock (the log is polled
            # only every 0.5 s): B meant 0.05 s after the placement reached the game 0.08 to 0.13 s after it on the
            # rig. Dashes up to 0.28 s after the placement grabbed the outer face under hand and head aim; under hand
            # aim ones 0.36 and 0.64 s after it (the player 1.5 m or more lower, timed by the log's poll) did not.
            $mark = 'debug-commands: {0}\.0 s: where' -f ($t - $script:QaClimbPer - 1)
            if (-not (Wait-QaLog $c $mark 90)) { Add-QaNote $c "try $k : the mark before the placement was not logged"; continue }
            $before = Get-QaClimbStarts $c
            $markAt = Get-QaLogLineTime (Get-QaLogText $c) $mark
            if ($null -eq $markAt) { Add-QaNote $c "try $k : the mark's time was not read"; continue }
            $left = ($markAt.AddSeconds(1.05) - (Get-Date)).TotalSeconds
            if ($left -gt 0) { Wait-QaSeconds $c $left } else { Add-QaNote $c ('try {0} : B {1:N2} s late' -f $k, -$left) }
            Write-QaInput $c ($pose + 'right.secondary = 1' + 'left.stick = 0, 1'); Wait-QaSeconds $c 0.15; Write-QaInput $c $pose
            Wait-QaSeconds $c 1.0
            if ((Get-QaClimbStarts $c) -eq $before) { Add-QaNote $c "try $k : no climb from the dash in the air"; continue }
            if ($k -eq $c.Scenario.ClimbShot) { Save-QaXrShot $c 'on-wall' }
            Wait-QaSeconds $c 2.0
            Write-QaInput $c ($pose + 'right.stick = 1, 0'); Wait-QaSeconds $c 0.6; Write-QaInput $c $pose; Wait-QaSeconds $c 0.3
            Write-QaInput $c ($pose + 'right.primary = 1'); Wait-QaSeconds $c 0.2; Write-QaInput $c $pose
            continue
        }
        if ($p.Do -eq 'air') {
            # From the schedule's `where` 1 s before the placement.
            if (-not (Wait-QaLog $c ('debug-commands: {0}\.0 s: where' -f ($t - $script:QaClimbPer - 1)) 90)) { Add-QaNote $c "try $k : the mark before the placement was not logged"; continue }
            $before = Get-QaClimbStarts $c
            $end = (Get-Date).AddSeconds(90); $stop = $null
            while ((Get-Date) -lt $end) {
                Write-QaInput $c ($pose + 'right.click = 1'); Wait-QaSeconds $c 0.1
                Write-QaInput $c $pose; Wait-QaSeconds $c 0.1
                if ((Get-QaClimbStarts $c) -gt $before) { break }
                if (-not $stop -and (Get-QaLogText $c) -match $pat) { $stop = (Get-Date).AddSeconds(0.8) }
                if ($stop -and (Get-Date) -gt $stop) { break }
            }
            if ((Get-QaClimbStarts $c) -eq $before) { Add-QaNote $c "try $k : no climb from the air"; continue }
            Wait-QaSeconds $c 1.0
            if ($k -eq $c.Scenario.ClimbShot) { Save-QaXrShot $c 'on-wall' }
            Wait-QaSeconds $c 2.0
            Write-QaInput $c ($pose + 'right.stick = 1, 0'); Wait-QaSeconds $c 0.6; Write-QaInput $c $pose; Wait-QaSeconds $c 0.3
            Write-QaInput $c ($pose + 'right.primary = 1'); Wait-QaSeconds $c 0.2; Write-QaInput $c $pose
            continue
        }
        if (-not (Wait-QaLog $c $pat 90)) { Add-QaNote $c "try $k : the placement was not logged"; continue }
        Wait-QaSeconds $c 1.0
        $pose = Get-QaClimbPose $p.Hand
        Write-QaInput $c $pose; Wait-QaSeconds $c 1.0
        if ($p.Do -eq 'melee') { Write-QaInput $c ($pose + 'right.click = 1'); Wait-QaSeconds $c 0.3; Write-QaInput $c $pose }
        $hold = 5.0
        if ($p.Do -eq 'dash') {
            $stick = if ($p.Stick) { @("left.stick = $($p.Stick)") } else { @() }
            Write-QaInput $c ($pose + 'right.secondary = 1' + $stick); Wait-QaSeconds $c 0.15; Write-QaInput $c $pose
            if ($k -eq $c.Scenario.ClimbShot) { Wait-QaSeconds $c 1.0; Save-QaXrShot $c 'on-wall'; $hold = 4.0 }
        }
        Wait-QaSeconds $c $hold
        Write-QaInput $c ($pose + 'right.primary = 1'); Wait-QaSeconds $c 0.2; Write-QaInput $c $pose
    }
    Wait-QaSeconds $c 11
}

function New-QaClimbScenario([string]$Aim, [string]$Proves, $Tries, [string]$Name = "climb-$Aim", [string]$Extra = '') {
    $steps = @("3:god;g_setting_hud_auto_dismiss_tutorials 1;g_setting_tutorials 0;ai_freezeMovement 1$Extra")
    for ($i = 0; $i -lt $Tries.Count; $i++) {
        $at = 6 + $i * $script:QaClimbPer
        if ($Tries[$i].Do -in 'air', 'dash-air') { $steps += '{0}:where' -f ($at - 1) }
        $steps += '{0}:setviewpos {1}' -f $at, $script:QaClimbPlaces[$Tries[$i].Face]
    }
    $shot = 1 + [Array]::IndexOf(@($Tries | ForEach-Object { $_.Do -in 'air', 'dash-air' }), $true)
    if ($shot -eq 0) { $shot = 1 + [Array]::IndexOf(@($Tries | ForEach-Object { $_.Do -eq 'dash' }), $true) }
    return @{
        Name = $Name; Kind = 'game'; OnlyByName = $true
        Proves = $Proves
        Map = 'game/sp/e1m2_battle/e1m2_battle -checkpoint cp_09_first_signal'
        Env = @("ETERNALVR_AIM=$Aim", 'ETERNALVR_TEST_CAMERA_TRACE=all', 'ETERNALVR_GAME_MOTION_TRACE=1', $script:QaTqSway, "ETERNALVR_DEBUG_COMMANDS=$($steps -join '|')")
        Input = Get-QaClimbPose '0, 0'
        ClimbTries = $Tries; ClimbShot = $shot
        Timeline = $script:QaClimbTimeline
        Asserts = {
            param($c)
            Test-QaPresent $c 'climb: wall-climb step hook at RVA'
            $tries = @($c.Scenario.ClimbTries)
            $places = @(Get-QaTqEvents $c 'debug-commands: [\d.]+ s: setviewpos')
            $climbs = @(Get-QaTqEpisodes $c 'climb: climb (\d+) starts' 'climb: climb (\d+) ends after')
            $gates = @(Get-QaTqEvents $c 'controllers: forced view starts \(climbable wall; inhibit 0x[0-9A-F]+\): shots and viewmodel leave the game alone; hand aim aims with the head')
            # Per try (from its placement to the next): the climbs expected, each with its end and the gate's line.
            $ok = $places.Count -eq $tries.Count; $text = @(); $airClimbs = @()
            for ($i = 0; $i -lt $tries.Count -and $i -lt $places.Count; $i++) {
                $from = $places[$i].T; $to = if ($i + 1 -lt $places.Count) { $places[$i + 1].T } else { [double]::MaxValue }
                $in = @($climbs | Where-Object { $_.Start -ge $from -and $_.Start -lt $to })
                $gated = @($in | Where-Object { $s = $_.Start; $_.End -ge 0 -and @($gates | Where-Object { [Math]::Abs($_.T - $s) -le 0.1 }).Count -ge 1 })
                $want = if ($tries[$i].Climb) { 1 } else { 0 }
                if ($in.Count -ne $want -or $gated.Count -ne $want) { $ok = $false }
                if ($tries[$i].Do -in 'air', 'dash-air') { $airClimbs += @($in | ForEach-Object { $_.N }) }
                $text += 'try {0} ({1} {2}, hand {3}): {4} climb(s){5}' -f ($i + 1), $tries[$i].Face, $tries[$i].Do, $tries[$i].Hand, $in.Count, $(if ($in.Count) { ' ' + (($in | ForEach-Object { '{0:N2}-{1:N2} s' -f $_.Start, $_.End }) -join ', ') } else { '' })
            }
            Add-QaResult $c 'climbs exactly where expected: each try''s climb N starts / ends, with the gate''s forced view starts (climbable wall; ...) within 0.1 s of its start; none where the try expects none' $ok ($text -join '; ')
            $rows = Get-QaTqTrace $c
            $teleports = @(Get-QaTqEvents $c 'game: teleport \d+: the player moved')
            $motion = @([regex]::Matches((Get-QaLogText $c), $script:QaClimbMotion, 'Multiline') | ForEach-Object {
                [pscustomobject]@{ T = [double]$_.Groups[1].Value; VX = [double]$_.Groups[2].Value; VY = [double]$_.Groups[3].Value } })
            $wall = $true; $flick = 0.0; $aimStep = 0.0; $jump = $true; $turned = $true; $wt = @(); $jt = @(); $tt = @()
            foreach ($e in @($climbs | Where-Object { $_.End -ge 0 })) {
                # Held still: the first 2.8 s on the wall (the outer tries turn with the stick after 3 s).
                $m = Measure-QaTqSpan (Select-QaTqSpan $rows ($e.Start + 0.2) ([Math]::Min($e.End - 0.2, $e.Start + 2.8)))
                if ($m.Lines -lt 100 -or $m.Tilt -gt 0.5 -or $m.GameY -le 4 -or $m.BodyTurn -gt 0.5) { $wall = $false }
                $wt += 'climb {0}: {1}' -f $e.N, (Format-QaTqSpan $m)
                # From 1 s before the start (after the try's placement: the debug teleport's game frame turns the view)
                # to 1 s after it, and from 1 s before the end (0.25 s for a climb from the air, after its stick turn) to
                # 1 s after.
                $from = $e.Start - 1
                $placed = @($places | Where-Object { $_.T -le $e.Start } | Select-Object -Last 1)
                if ($placed.Count) {
                    $tp = @($teleports | Where-Object { $_.T -ge $placed[0].T -and $_.T -le $e.Start } | Select-Object -Last 1)
                    $from = [Math]::Max($from, $(if ($tp.Count) { $tp[0].T + 0.005 } else { $placed[0].T + 0.1 }))
                }
                $air = $airClimbs -contains $e.N
                foreach ($w in @(@($from, ($e.Start + 1)), @(($e.End - $(if ($air) { 0.25 } else { 1 })), ($e.End + 1)))) {
                    $span = @(Select-QaTqSpan $rows $w[0] $w[1])
                    for ($i = 1; $i -lt $span.Count; $i++) {
                        $flick = [Math]::Max($flick, [Math]::Abs((Get-QaTqWrap (($span[$i].WY - $span[$i].HY) - ($span[$i - 1].WY - $span[$i - 1].HY)))))
                        $flick = [Math]::Max($flick, [Math]::Abs($span[$i].WP - $span[$i].HP))
                        if (-not [double]::IsNaN($span[$i].AY) -and -not [double]::IsNaN($span[$i - 1].AY)) { $aimStep = [Math]::Max($aimStep, [Math]::Abs((Get-QaTqWrap ($span[$i].AY - $span[$i - 1].AY)))) }
                    }
                }
                # A climb from the air: the stick turned the view on the wall (over 90 deg).
                if ($air) {
                    $turn = Measure-QaTqSpan (Select-QaTqSpan $rows ($e.Start + 2.8) $e.End)
                    if ($turn.BodyTurn -le 90) { $turned = $false }
                    $tt += 'climb {0}: the body turned {1:N1} deg on the wall' -f $e.N, $turn.BodyTurn
                }
                # The jump off the wall: the player's first horizontal velocity of 5 m/s or more after the end goes where
                # the view looked as the climb ended (the written yaw, the head's on the wall).
                $last = @(Select-QaTqSpan $rows ($e.End - 0.1) $e.End) | Select-Object -Last 1
                $v = $motion | Where-Object { $_.T -ge $e.End -and $_.T -le $e.End + 1 -and [Math]::Sqrt($_.VX * $_.VX + $_.VY * $_.VY) -ge 5 } | Select-Object -First 1
                if (-not $last -or -not $v) { $jump = $false; $jt += 'climb {0}: no view or velocity' -f $e.N; continue }
                $heading = [Math]::Atan2($v.VY, $v.VX) * 180 / [Math]::PI
                $off = Get-QaTqWrap ($heading - $last.WY)
                if ([Math]::Abs($off) -gt 10) { $jump = $false }
                $jt += 'climb {0}: off the wall at {1:N1} deg, the view at {2:N1} ({3:N1} off)' -f $e.N, $heading, $last.WY, $off
            }
            $n = @($climbs | Where-Object { $_.End -ge 0 }).Count
            $airTries = @($tries | Where-Object { $_.Do -in 'air', 'dash-air' }).Count
            Add-QaResult $c 'trace: held on the wall the written pitch and roll are the head''s (within 0.5 deg), the game''s view yaw follows the head''s sway (over 4 deg of range) and the body does not turn (0.5 deg)' ($n -ge 1 -and $wall) ($wt -join '; ')
            Add-QaResult $c 'trace: no flick at a climb''s start or end: the written view less the head''s angles steps 0.5 deg or less a game frame within 1 s of each' ($n -ge 1 -and $flick -le 0.5) ('{0:N2} deg at most; the game''s own view yaw stepped up to {1:N1} deg there (the aim handed between the weapon hand and the head)' -f $flick, $aimStep)
            Add-QaResult $c 'trace: the right stick turned the view on the wall (over 90 deg) in each climb from the air' ($tt.Count -eq $airTries -and $turned) ($tt -join '; ')
            Add-QaResult $c 'game motion: each jump off the wall goes where the view looked (within 10 deg)' ($n -ge 1 -and $jump) ($jt -join '; ')
        }
    }
}

$takeoverScenarios += @(
    (New-QaClimbScenario 'hand' 'Climbable walls under hand aim: a climb starts only on the melee button with the weapon hand''s ray on the wall, standing or from the air, logs its start and end, the view on the wall is the head''s with no flick, the stick turns it there, and the jump off goes where the view looks' @(
        @{ Face = 'inner'; Hand = '0, 0'; Do = 'none'; Climb = $false },
        @{ Face = 'inner'; Hand = '70, 0'; Do = 'melee'; Climb = $false },
        @{ Face = 'inner'; Hand = '0, 0'; Do = 'melee'; Climb = $true },
        @{ Face = 'outer'; Hand = '0, 0'; Do = 'air'; Climb = $true })),
    (New-QaClimbScenario 'head' 'Climbable walls under head aim: the melee button climbs where the head looks, wherever the weapon hand points, with the same lines and view checks as climb-hand' @(
        @{ Face = 'inner'; Hand = '0, 0'; Do = 'none'; Climb = $false },
        @{ Face = 'inner'; Hand = '70, 0'; Do = 'melee'; Climb = $true },
        @{ Face = 'outer'; Hand = '0, 0'; Do = 'air'; Climb = $true })),
    (New-QaClimbScenario 'hand' 'Climbable walls by a dash under hand aim (the dash given first: the checkpoint loads without it): B with the weapon hand at the wall grabs it with the game''s default dash_autoAttachWallClimb, standing, with the hand 25 degrees off and the move stick forward, and from the air; the same lines and view checks as climb-hand' @(
        @{ Face = 'inner'; Hand = '0, 0'; Do = 'dash'; Climb = $true },
        @{ Face = 'inner'; Hand = '25, 0'; Do = 'dash'; Stick = '0, 1'; Climb = $true },
        @{ Face = 'outer'; Hand = '0, 0'; Do = 'dash-air'; Climb = $true }) 'climb-dash' ';give ability_dash'),
    (New-QaClimbScenario 'head' 'Climbable walls by a dash under head aim (the dash given first): as climb-dash' @(
        @{ Face = 'inner'; Hand = '70, 0'; Do = 'dash'; Stick = '0, 1'; Climb = $true },
        @{ Face = 'outer'; Hand = '0, 0'; Do = 'dash-air'; Climb = $true }) 'climb-dash-head' ';give ability_dash')
)

# Before the launcher's end-to-end suite, which stays last.
$e2e = @($script:QaScenarios | Where-Object { $_.Kind -eq 'e2e' })
$script:QaScenarios = @($script:QaScenarios | Where-Object { $_.Kind -ne 'e2e' }) + $takeoverScenarios + $e2e
