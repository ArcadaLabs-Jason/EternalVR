# Motion the game makes and the comfort vignette (features/comfort/launch_pulse.hpp, ride.hpp; vkcore/game_motion.hpp):
# the vignette's pulse for a jump pad and a booster, rides on a lift and a tram, with the platforming before the pad
# as the negative control, and the rides that carry no one or play as cutscenes. All by name only (measurements on
# given maps, about a minute or two each). Dot-sourced by qa-suite.ps1 after qa-scenarios-takeover.ps1: added to
# $script:QaScenarios before the launcher's end-to-end suite.
#
#   launch-pad
#       e1m2 cp_09_first_signal, the vignette Light, god: from 12 s two double jumps, two dashes and a walk with the
#       stick (none of them a launch or a ride), then at 34 s and 50 s the player put 1.4 m over
#       trenches_trigger_bounce_pad_2 (docs/rig-findings/bhaptics-launch.md section 5). The launch hooks in for the
#       vignette (bHaptics off), `launch: jump pad 'trenches_trigger_bounce_pad_2'` lines for the visits only,
#       `vignette: launch N (...) held X s: on the ground again`, the 10 s vignette lines counting launch frames,
#       and no ride.
#   ride-lift
#       e2m3 cp_01a_post_intro_anim: the player starts on the Phobos elevator (phobos_func_mover_phobos_elev, 160 m
#       down in 25 s) inside its trigger, which sends it down. One `game: ride N starts` and its end line, the ride
#       15 to 40 s long at 3 to 10 m/s at its fastest, and the 10 s vignette lines counting ride frames.
#   ride-cult-tram
#       e1m3 cp_end: the player put on the Cultist Base's tram (movers_func_mover_128) and its skull switch's targets
#       activated from the schedule (what the switch's use does): the tram leaves (500 m in 8 s); a ride's start line
#       and the vignette's ride frames (the mission ends before the tram stops).
#   motion-negative
#       e1m2 cp_09_first_signal: from the trench's spawn, running 3 s in each of 8 headings (into the trench's walls),
#       then pushing the stick diagonally in the same 8 (into corners), then running with the weapon wheel open (its
#       slow motion; the right stick down). No `game: ride` line; blocked steps in the trace (0.5 m/s or more of own
#       speed, the body under 0.5 m since the line before: on the rig a wall or corner at 1 to 2 m/s of the game's
#       velocity, the wheel's slow motion at 4.7 m/s), none carried at 1 m/s or more; the wheel's forced view.
#   photo-mode-held
#       e1m2's start with the game's Photo mode allowed in play (`g_allowPhotoMode 1` from the debug schedule, as a map
#       load that allows photo mode sets it): the comfort set holds it (`held at run time` lists
#       g_allowPhotoMode 0) and writes it back to 0 (`cvars: g_allowPhotoMode 1 -> 0`); the player's own Photo mode
#       setting is never written (no `cvars: g_setting_photomode` line).
#   photo-mode-held-mono
#       The same in head-tracked mono (ETERNALVR_MODE=mono, the render size auto, as the launcher's Mono passes it):
#       mono's set (the comfort set but the underwater warp) holds g_allowPhotoMode 0 beside the window set and writes
#       it back; the profile hooks are in (head-tracked mono holds view_skipShakes too).
#   comfort-held-mono
#       Head-tracked mono as the launcher's Mono passes it (ETERNALVR_MODE=mono, the render size auto), e1m2's start, a
#       map load at 8 s (the game saves the profile): the comfort set but the underwater warp held at run time
#       (`held at run time` lists it, view_skipShakes 1 and pm_noBob 1 among it), the game's own values written over at
#       the first present (`cvars: pm_noBob 0 -> 1`, `cvars: view_damageBlur 1 -> 0`: the harness's command line has
#       neither, and on the rig the launcher's did not hold them either, docs/rig-findings/profile-leak.md "Mono"),
#       r_waterPostProcess never written, the profile hooks in and the game's profile saves seen by them; the suite's
#       profile check after the run (no held value saved) covers the rest.
#   launch-booster
#       e1m2 cp_06c_post_crossroads_post_battery: capitol_target_relay_750 (what the level does) switches on the
#       game's only booster, capitol_trigger_boost_1 (an idTrigger_SonicBoom, 75 m/s, gravity off), and the
#       schedule puts the player into it in the air at 10 s and 20 s. A `launch: booster 'capitol_trigger_boost_1'`
#       line for each visit and none before, each pulse held 0.3 to 4 s and ended by the ground (on the rig 0.30
#       to 0.31 s: the blast lands the player 17 m on), launch frames, no jump pad line and no ride.
#   ride-hunter-tram
#       e1m4 cp_02_post_train_fight, the player on the Doom Hunter Base train's front car: the coupling's relay
#       (train_target_relay_7, what breaking the coupling does) sends the cars behind it off (their mover,
#       train_func_mover_train_disconnect_1, 900 m in 40 s). The player's car is no mover, so no ride, no launch and
#       no trace step on the ground carried at 0.5 m/s or more after it: the tram's ride is scenery and effects
#       past a car that stands, which the rule cannot see (docs/rig-findings/camera-takeovers.md row 20).
#   ride-argent-stream, ride-spear-creature, ride-swamp-titan
#       Rides the game plays as cutscenes (on the flat screen by default; ETERNALVR_SKIP_CINEMATICS=0 so they play),
#       so the ride rule never runs in them: a `game: cutscene starts` soon after the step that starts the ride, and
#       no ride or launch line in the run. ride-argent-stream: e3m2_hell_b cp_09_towertop_combatend, the pressure
#       plate, both chains (`trigger` on their melee targets; a cutscene each), then the player put into the portal
#       (titan_chains_portal_trigger): the jump is a cutscene within 2 s, then the mission's summary.
#       ride-spear-creature: e5m1_spear cp_12_stone_imp_intro, the player put into the torch's trigger
#       (village_trigger_trigger_8): the torch scene and the flight above the clouds start within 2 s.
#       ride-swamp-titan: e4m2_swamp CP_10_BOSS_post_combat, the boss's end relay, then Use 2.5 m from the
#       platform's use proxy (titan_level_raise_volume_use_proxy): the Titan's lift starts within 15 s of the step.
#   launch-hammer
#       e5m2_earth from its start, whose inventory has the Sentinel Hammer with its meter empty: a press of its button
#       (the left stick click), a double jump and a double jump with a press (no launch), then the player put on four
#       of the map's hammer charge pickups 20 s apart, a slam standing on the first and third and after a double
#       jump on the second and fourth. One `vignette: launch N: the Sentinel Hammer's slam, from the ground` (or
#       `in the air`) within 0.4 s of each charged press (the held hammer's new slam start time, a game frame after
#       it comes up: on the rig 0.03 s after the press), none before the first pickup, every pulse held 0.3 to 2 s
#       and ended by the ground, launch frames, no jump pad or booster line and no ride.

$script:QaMotionEnv = @('ETERNALVR_VIGNETTE=light', 'ETERNALVR_GAME_MOTION_TRACE=1')
$script:QaMotionQuiet = 'god;g_setting_hud_auto_dismiss_tutorials 1;g_setting_tutorials 0'
$script:QaMotionRideStart = 'game: ride (\d+) starts: the player carried at ([\d.]+) m/s'
$script:QaMotionRideEnd = 'game: ride (\d+) ends after ([\d.]+) s \(([^)]*)\); fastest ([\d.]+) m/s'
$script:QaMotionVignette = 'vignette: light, shown (\d+) of \d+ frame\(s\).*?launch (\d+) frame\(s\), ride (\d+) frame\(s\)'

# The log's lines matching $Pattern as objects: T (log seconds) and M (the match).
function Get-QaMotionEvents($Ctx, [string]$Pattern) {
    $out = New-Object System.Collections.ArrayList
    foreach ($m in [regex]::Matches((Get-QaLogText $Ctx), '^\[\s*([\d.]+)\] \[\s*\d+\] .*?' + $Pattern, 'Multiline')) {
        [void]$out.Add([pscustomobject]@{ T = [double]$m.Groups[1].Value; M = $m })
    }
    return $out
}

# The 10 s vignette lines' launch and ride frames, summed.
function Get-QaMotionFrames($Ctx) {
    $launch = 0; $ride = 0; $lines = 0
    foreach ($e in @(Get-QaMotionEvents $Ctx $script:QaMotionVignette)) {
        $lines++; $launch += [int]$e.M.Groups[3].Value; $ride += [int]$e.M.Groups[4].Value
    }
    return [pscustomobject]@{ Lines = $lines; Launch = $launch; Ride = $ride }
}

# One ride from start to end: its start and end lines, how long and how fast, and the ride frames.
function Test-QaMotionRide($Ctx, [double]$MinSeconds, [double]$MaxSeconds, [double]$MinSpeed, [double]$MaxSpeed) {
    $starts = @(Get-QaMotionEvents $Ctx $script:QaMotionRideStart)
    $ends = @(Get-QaMotionEvents $Ctx $script:QaMotionRideEnd)
    $long = @($ends | Where-Object { [double]$_.M.Groups[3].Value -ge $MinSeconds })
    Add-QaResult $Ctx 'a ride starts and ends' ($starts.Count -ge 1 -and $ends.Count -ge 1) `
        ('{0} start(s), {1} end(s): {2}' -f $starts.Count, $ends.Count, (($ends | ForEach-Object { 'ride {0} {1} s ({2}), fastest {3} m/s' -f $_.M.Groups[2].Value, $_.M.Groups[3].Value, $_.M.Groups[4].Value, $_.M.Groups[5].Value }) -join '; '))
    $r = $long | Select-Object -First 1
    $ok = $r -and [double]$r.M.Groups[3].Value -le $MaxSeconds -and [double]$r.M.Groups[5].Value -ge $MinSpeed -and [double]$r.M.Groups[5].Value -le $MaxSpeed
    Add-QaResult $Ctx ('the ride lasts {0} to {1} s at {2} to {3} m/s at its fastest' -f $MinSeconds, $MaxSeconds, $MinSpeed, $MaxSpeed) ([bool]$ok) $(if ($r) { $r.M.Value } else { 'no ride that long' })
    $f = Get-QaMotionFrames $Ctx
    Add-QaResult $Ctx 'the 10 s vignette lines count ride frames' ($f.Ride -gt 0) ('{0} line(s), {1} ride frame(s), {2} launch frame(s)' -f $f.Lines, $f.Ride, $f.Launch)
}

# motion-negative: the spawn in the trench, a heading every 3 s (8 straight runs, then 8 diagonal), then the wheel run.
$script:QaMotionNegSchedule = "3:$script:QaMotionQuiet|6:setviewpos -39.4 -579.1 -4.95 0|9:setviewpos -39.4 -579.1 -4.95 45|12:setviewpos -39.4 -579.1 -4.95 90|15:setviewpos -39.4 -579.1 -4.95 135|18:setviewpos -39.4 -579.1 -4.95 180|21:setviewpos -39.4 -579.1 -4.95 225|24:setviewpos -39.4 -579.1 -4.95 270|27:setviewpos -39.4 -579.1 -4.95 315|30:setviewpos -39.4 -579.1 -4.95 0|33:setviewpos -39.4 -579.1 -4.95 45|36:setviewpos -39.4 -579.1 -4.95 90|39:setviewpos -39.4 -579.1 -4.95 135|42:setviewpos -39.4 -579.1 -4.95 180|45:setviewpos -39.4 -579.1 -4.95 225|48:setviewpos -39.4 -579.1 -4.95 270|51:setviewpos -39.4 -579.1 -4.95 315|54:setviewpos -39.4 -579.1 -4.95 0"
# launch-booster: the booster's relay (it switches on capitol_trigger_boost_1 and the pad under it), then the player
# put into the booster in the air twice (it lands the player at its destination 17 m on, outside it).
$script:QaMotionBoostSchedule = "3:$script:QaMotionQuiet|4:getviewpos|6:activatetargets capitol_target_relay_750|10:setviewpos 206.4 316.05 -23.0 0|11.5:getviewpos|20:setviewpos 206.4 316.05 -23.0 0|21.5:getviewpos"

# A ride the game plays as a cutscene: the first `game: cutscene starts` within $MaxSeconds of the schedule's step
# that starts it (or puts the player where Use starts it), and no ride or launch line in the whole run.
function Test-QaMotionCutscene($Ctx, [string]$StepPattern, [double]$MaxSeconds = 2.0) {
    $step = @(Get-QaMotionEvents $Ctx $StepPattern) | Select-Object -First 1
    $cut = if ($step) { @(Get-QaMotionEvents $Ctx 'game: cutscene starts \(([^)]*)\)' | Where-Object { $_.T -ge $step.T }) | Select-Object -First 1 }
    Add-QaResult $Ctx "a cutscene starts within $MaxSeconds s of the step" ([bool]$step -and [bool]$cut -and ($cut.T - $step.T) -le $MaxSeconds) $(if ($cut) { '{0:N2} s after the step: {1}' -f ($cut.T - $step.T), $cut.M.Value } elseif ($step) { 'no cutscene after the step' } else { 'the step is not in the log' })
    Test-QaAbsent $Ctx 'game: ride'
    Test-QaAbsent $Ctx 'vignette: launch'
}

# launch-hammer: e5m2_earth's start, then the player put on four of the map's hammer charge pickups
# (propitem/ammo/hammer, 125 of the meter's 120) 20 s apart.
$script:QaMotionHammerPickups = @('237.5 -849.0 -143.0 0', '165.1 -708.6 -139.0 0', '33.3 -653.5 -138.0 0', '-31.0 -794.4 -136.5 0')
$script:QaMotionHammerSchedule = "3:$script:QaMotionQuiet|4:getviewpos|" + ((0..3 | ForEach-Object { '{0}:setviewpos {1}|{2}:getviewpos' -f (16 + $_ * 20), $script:QaMotionHammerPickups[$_], (18 + $_ * 20) }) -join '|')

# A button's press (0.2 s) and a double jump (A twice, 0.4 s apart), from the pose $Pose.
function Invoke-QaMotionPress($Ctx, [string[]]$Pose, [string]$Button) {
    Write-QaInput $Ctx ($Pose + "$Button = 1"); Wait-QaSeconds $Ctx 0.2; Write-QaInput $Ctx $Pose
}
function Invoke-QaMotionDoubleJump($Ctx, [string[]]$Pose) {
    Invoke-QaMotionPress $Ctx $Pose 'right.primary'; Wait-QaSeconds $Ctx 0.2
    Invoke-QaMotionPress $Ctx $Pose 'right.primary'; Wait-QaSeconds $Ctx 0.3
}

$script:QaMotionPadSchedule ="3:$script:QaMotionQuiet|4:getviewpos|34:setviewpos -20.9 -552.5 -3.6 0|35.5:getviewpos|37:setviewpos -39.4 -579.1 -4.95 0|50:setviewpos -20.9 -552.5 -3.6 0|51.5:getviewpos"

$motionScenarios = @(
    @{
        Name = 'launch-pad'; Kind = 'game'; OnlyByName = $true
        Proves = 'A jump pad brings up the vignette''s pulse (Light, bHaptics off): the launch hooks in for the vignette, one launch line per pad visit, each pulse held until the feet are down; jumps, double jumps, dashes and a walk before it are no launch and no ride'
        Map = 'game/sp/e1m2_battle/e1m2_battle -checkpoint cp_09_first_signal'
        Env = $script:QaMotionEnv + @("ETERNALVR_DEBUG_COMMANDS=$script:QaMotionPadSchedule")
        Input = $script:QaRestPose
        Timeline = {
            param($c)
            Wait-QaSeconds $c 12
            foreach ($press in 'right.primary = 1', 'right.primary = 1', 'right.secondary = 1', 'right.secondary = 1') {
                Write-QaInput $c ($script:QaRestPose + $press); Wait-QaSeconds $c 0.15
                Write-QaInput $c $script:QaRestPose; Wait-QaSeconds $c 0.25
                if ($press -eq 'right.primary = 1') { Write-QaInput $c ($script:QaRestPose + $press); Wait-QaSeconds $c 0.15; Write-QaInput $c $script:QaRestPose }
                Wait-QaSeconds $c 2
            }
            Write-QaInput $c ($script:QaRestPose + 'left.stick = 0, 1'); Wait-QaSeconds $c 3
            Write-QaInput $c ($script:QaRestPose + 'left.stick = 1, 0'); Wait-QaSeconds $c 2
            Write-QaInput $c $script:QaRestPose
            Wait-QaLog $c "launch: jump pad 'trenches_trigger_bounce_pad_2'" 30 | Out-Null
            Save-QaXrShot $c 'pad'
            Wait-QaSeconds $c 25
        }
        Asserts = {
            param($c)
            Test-QaPresent $c 'launch: jump pad hook at RVA'
            Test-QaPresent $c 'vignette: the launch hooks are in'
            Test-QaPresent $c $script:QaInputRead -Regex
            # Each visit throws the player 10 m straight up, and the fall lands on the pad again (another throw
            # every 2 s) until the schedule moves the player away: 2 launches or more, all of that pad, none
            # before the first visit (the jumps, double jumps and dashes).
            $launches = @(Get-QaMotionEvents $c "launch: jump pad '([^']+)'")
            $other = @($launches | Where-Object { $_.M.Groups[2].Value -ne 'trenches_trigger_bounce_pad_2' })
            $visit = @(Get-QaMotionEvents $c 'debug-commands: 34.0 s: setviewpos') | Select-Object -First 1
            $early = @($launches | Where-Object { $visit -and $_.T -lt $visit.T })
            Add-QaResult $c 'launch lines for the pad visits only (2 or more, all of trenches_trigger_bounce_pad_2, none before the first visit)' ($launches.Count -ge 2 -and $other.Count -eq 0 -and $visit -and $early.Count -eq 0) (($launches | ForEach-Object { '{0:N1} s {1}' -f $_.T, $_.M.Groups[2].Value }) -join '; ')
            # Held until the feet are down, or for the throw's time (2 x 21 / 20.3 = 2.07 s) when the fall lands on
            # the pad and it throws again before the ground is seen; or carried on by the next throw.
            $held = @(Get-QaMotionEvents $c 'vignette: launch (\d+) \(jump pad [^)]*\) (held ([\d.]+) s: (on the ground again|its limit)|carried on by the next after ([\d.]+) s)')
            $short = @($held | Where-Object { $s = if ($_.M.Groups[4].Value) { [double]$_.M.Groups[4].Value } else { [double]$_.M.Groups[6].Value }; $s -lt 0.3 -or $s -gt 4.0 })
            Add-QaResult $c 'every pulse held for the flight (0.3 to 4 s), ended by the ground, its limit or the next throw' ($held.Count -ge $launches.Count - 1 -and $held.Count -gt 0 -and $short.Count -eq 0) (($held | ForEach-Object { $_.M.Value -replace '^.*vignette: ', '' }) -join '; ')
            $f = Get-QaMotionFrames $c
            Add-QaResult $c 'the 10 s vignette lines count launch frames' ($f.Launch -gt 0) ('{0} line(s), {1} launch frame(s), {2} ride frame(s)' -f $f.Lines, $f.Launch, $f.Ride)
            Test-QaAbsent $c 'game: ride'
        }
    },
    @{
        Name = 'ride-lift'; Kind = 'game'; OnlyByName = $true
        Proves = 'A long elevator is a ride: e2m3''s Phobos elevator (160 m in 25 s) gives one ride''s start and end lines, 15 to 40 s at 3 to 10 m/s, and the vignette''s ride frames'
        Map = 'game/sp/e2m3_core/e2m3_core -checkpoint cp_01a_post_intro_anim'
        Env = $script:QaMotionEnv + @("ETERNALVR_DEBUG_COMMANDS=3:$script:QaMotionQuiet|4:getviewpos|40:getviewpos")
        Input = $script:QaRestPose
        Timeline = {
            param($c)
            if (-not (Wait-QaLog $c 'game: ride \d+ starts' 45)) { Add-QaNote $c 'no ride within 45 s'; return }
            Wait-QaSeconds $c 8; Save-QaXrShot $c 'riding'
            if (-not (Wait-QaLog $c 'game: ride \d+ ends' 60)) { Add-QaNote $c 'the ride did not end within 60 s' }
            Wait-QaSeconds $c 11
        }
        Asserts = { param($c) Test-QaMotionRide $c 15 40 3 10; Test-QaAbsent $c 'vignette: launch' }
    },
    @{
        Name = 'ride-cult-tram'; Kind = 'game'; OnlyByName = $true
        Proves = 'The Cultist Base''s tram at the mission''s end is a ride: the player put on it, its skull switch''s targets activated (the use does that), a ride''s start line and the vignette''s ride frames (the mission may end before the tram stops)'
        Map = 'game/sp/e1m3_cult/e1m3_cult -checkpoint cp_end'
        Env = $script:QaMotionEnv + @("ETERNALVR_DEBUG_COMMANDS=3:$script:QaMotionQuiet|5:teleportposition 0.05 385 8 90|6:getviewpos|9:activatetargets game_interact_use_panel_cultist_skull_switch_5|12:getviewpos|16:getviewpos")
        Input = $script:QaRestPose
        Timeline = {
            param($c)
            if (-not (Wait-QaLog $c 'game: ride \d+ starts' 40)) { Add-QaNote $c 'no ride within 40 s'; return }
            Wait-QaSeconds $c 3; Save-QaXrShot $c 'riding'
            Wait-QaLog $c 'game: ride \d+ ends' 30 | Out-Null
            Wait-QaSeconds $c 11
        }
        Asserts = {
            param($c)
            $starts = @(Get-QaMotionEvents $c $script:QaMotionRideStart)
            Add-QaResult $c 'a ride starts' ($starts.Count -ge 1) (($starts | ForEach-Object { $_.M.Value }) -join '; ')
            $f = Get-QaMotionFrames $c
            Add-QaResult $c 'the 10 s vignette lines count ride frames' ($f.Ride -gt 0) ('{0} line(s), {1} ride frame(s)' -f $f.Lines, $f.Ride)
        }
    },
    @{
        Name = 'motion-negative'; Kind = 'game'; OnlyByName = $true
        Proves = 'Own motion the body does not follow is no ride: running from e1m2 cp_09_first_signal''s trench in 8 headings (into its walls), then pushing diagonally in the same 8 (into corners), then running with the weapon wheel open (its slow motion): no ride line, and blocked steps in the trace (own velocity, the body hardly moving) never carried'
        Map = 'game/sp/e1m2_battle/e1m2_battle -checkpoint cp_09_first_signal'
        Env = $script:QaMotionEnv + @("ETERNALVR_DEBUG_COMMANDS=$script:QaMotionNegSchedule")
        Input = $script:QaRestPose
        Timeline = {
            param($c)
            Wait-QaSeconds $c 6
            Write-QaInput $c ($script:QaRestPose + 'left.stick = 0, 1'); Wait-QaSeconds $c 24
            Write-QaInput $c ($script:QaRestPose + 'left.stick = 0.7, 0.7'); Wait-QaSeconds $c 24
            Write-QaInput $c ($script:QaRestPose + 'left.stick = 0, 1' + 'right.stick = 0, -1'); Wait-QaSeconds $c 5
            Save-QaXrShot $c 'wheel'
            Write-QaInput $c $script:QaRestPose; Wait-QaSeconds $c 12
        }
        Asserts = {
            param($c)
            Test-QaPresent $c $script:QaInputRead -Regex
            Test-QaAbsent $c 'game: ride'
            Test-QaPresent $c 'controllers: forced view starts (weapon wheel'
            # Blocked steps: on the ground with 0.5 m/s or more of own velocity while the body moved under 0.5 m since
            # the trace line before (0.4 s at most): a wall or a corner (the game's velocity drops to 1 or 2 m/s
            # there) or the wheel's slow motion (its velocity stays, the body hardly moves). None may read as
            # carried.
            $lines = @(Get-QaMotionEvents $c 'game motion trace: ground (\d) .*?; origin (-?[\d.]+) (-?[\d.]+) (-?[\d.]+); velocity .*?\(([\d.]+)\); carried ([\d.]+)')
            $blocked = 0; $worst = 0.0; $fastest = 0.0
            for ($i = 1; $i -lt $lines.Count; $i++) {
                $a = $lines[$i - 1].M; $b = $lines[$i].M
                if ($b.Groups[2].Value -ne '1') { continue }
                $dx = [double]$b.Groups[3].Value - [double]$a.Groups[3].Value
                $dy = [double]$b.Groups[4].Value - [double]$a.Groups[4].Value
                if ([double]$b.Groups[6].Value -ge 0.5 -and [Math]::Sqrt($dx * $dx + $dy * $dy) -lt 0.5 -and ($lines[$i].T - $lines[$i - 1].T) -lt 0.4) {
                    $blocked++
                    $worst = [Math]::Max($worst, [double]$b.Groups[7].Value)
                    $fastest = [Math]::Max($fastest, [double]$b.Groups[6].Value)
                }
            }
            Add-QaResult $c 'blocked steps in the trace (3 or more), none carried at 1 m/s or more' ($blocked -ge 3 -and $worst -lt 1.0) ("$blocked step(s), own speed up to {0:N2} m/s, carried up to {1:N2} m/s" -f $fastest, $worst)
        }
    },
    @{
        Name = 'photo-mode-held'; Kind = 'game'; OnlyByName = $true
        Proves = 'The game''s Photo mode stays off in VR: the comfort set holds g_allowPhotoMode 0 and writes it back when the game allows photo mode in play, and leaves the player''s own Photo mode setting alone'
        Env = @('ETERNALVR_DEBUG_COMMANDS=3:god|8:g_allowPhotoMode 1')
        Timeline = { param($c) Wait-QaSeconds $c 14 }
        Asserts = {
            param($c)
            Test-QaPresent $c 'cvars: held at run time: .*g_allowPhotoMode 0' -Regex
            Test-QaPresent $c 'debug-commands: .*g_allowPhotoMode 1' -Regex
            Test-QaPresent $c 'cvars: g_allowPhotoMode 1 -> 0 (reads 0)'
            Test-QaAbsent $c 'cvars: g_setting_photomode'
        }
    },
    @{
        Name = 'photo-mode-held-mono'; Kind = 'game'; OnlyByName = $true
        Proves = 'The game''s Photo mode stays off in head-tracked mono too: mono''s set (the comfort set but the underwater warp) holds g_allowPhotoMode 0 beside the window set and writes it back when the game allows photo mode in play; the player''s own setting and the profile left alone'
        Env = @('ETERNALVR_MODE=mono', 'ETERNALVR_RENDER_SIZE=auto', 'ETERNALVR_DEBUG_COMMANDS=3:god|8:g_allowPhotoMode 1')
        Timeline = { param($c) Wait-QaSeconds $c 14 }
        Asserts = {
            param($c)
            Test-QaPresent $c "cvars: head-tracked mono: only the window's mode and size (the render size) and the comfort set but the underwater warp (16 cvars) held"
            Test-QaPresent $c 'cvars: held at run time: r_fullscreen 0, r_windowWidth \d+, r_windowHeight \d+, r_hdrDisplay 0, .*g_allowPhotoMode 0$' -Regex
            Test-QaPresent $c 'debug-commands: .*g_allowPhotoMode 1' -Regex
            Test-QaPresent $c 'cvars: g_allowPhotoMode 1 -> 0 (reads 0)'
            Test-QaAbsent $c 'cvars: g_setting_photomode'
            Test-QaAbsent $c 'seq: Route S on'
            Test-QaPresent $c 'profile: profile cvar serializer hooked'
        }
    },
    @{
        Name = 'comfort-held-mono'; Kind = 'game'; OnlyByName = $true
        Proves = 'Head-tracked mono holds the comfort set at run time as stereo does (camera shake, view bob, the damage tint and blur, the vignette and the rest; not the underwater warp): the game''s own values written over from the first present and held through a map load, with the profile hooks keeping the player''s own camera shake setting in profile.bin'
        Env = @('ETERNALVR_MODE=mono', 'ETERNALVR_RENDER_SIZE=auto', 'ETERNALVR_DEBUG_COMMANDS=3:god|8:map game/sp/e1m2_battle/e1m2_battle')
        Timeline = {
            param($c)
            Wait-QaLog $c 'debug-commands: 8\.0 s: map' 40 | Out-Null
            Wait-QaSeconds $c 25
            Save-QaXrShot $c 'after-load'
        }
        Asserts = {
            param($c)
            Test-QaPresent $c "cvars: head-tracked mono: only the window's mode and size (the render size) and the comfort set but the underwater warp (16 cvars) held"
            Test-QaPresent $c 'cvars: held at run time: r_fullscreen 0, r_windowWidth \d+, r_windowHeight \d+, r_hdrDisplay 0, r_motionblur 0, r_dof 0, r_chromaticAberration 0, r_vignette 0, pm_noBob 1, view_skipKicks 1, view_skipShakes 1, hands_fovScale 1, meatHook_playerViewOverrideMode 1, view_skipDamageEffect 1, view_showPlayerDamageViewEffect 0, view_damageBlur 0, g_skipViewEffects 1, r_blurRadialScale 0, g_allowPhotoMode 0$' -Regex
            Test-QaPresent $c 'cvars: pm_noBob 0 -> 1 (reads 1); held in VR'
            Test-QaPresent $c 'cvars: view_damageBlur 1 -> 0 (reads 0); held in VR'
            Test-QaPresent $c 'debug-commands: 8.0 s: map game/sp/e1m2_battle/e1m2_battle'
            Test-QaAbsent $c 'cvars: r_waterPostProcess'
            Test-QaAbsent $c 'seq: Route S on'
            Test-QaPresent $c 'profile: profile cvar serializer hooked at RVA'
            Test-QaPresent $c 'profile: the game saved the profile'
        }
    },
    @{
        Name = 'launch-booster'; Kind = 'game'; OnlyByName = $true
        Proves = 'A booster brings up the vignette''s pulse: e1m2''s capitol_trigger_boost_1 (75 m/s) switched on by its relay, the player put into it twice: a booster line for each visit and none before, every pulse held until the feet are down, launch frames, no jump pad line and no ride'
        Map = 'game/sp/e1m2_battle/e1m2_battle -checkpoint cp_06c_post_crossroads_post_battery'
        Env = $script:QaMotionEnv + @("ETERNALVR_DEBUG_COMMANDS=$script:QaMotionBoostSchedule")
        Input = $script:QaRestPose
        Timeline = {
            param($c)
            if (-not (Wait-QaLog $c "launch: booster 'capitol_trigger_boost_1'" 40)) { Add-QaNote $c 'no booster line within 40 s' }
            Save-QaXrShot $c 'boost'
            Wait-QaLog $c 'debug-commands: 20\.0 s: setviewpos' 20 | Out-Null
            Wait-QaSeconds $c 12
        }
        Asserts = {
            param($c)
            Test-QaPresent $c 'vignette: the launch hooks are in'
            $boosts = @(Get-QaMotionEvents $c "launch: booster '([^']+)'")
            $other = @($boosts | Where-Object { $_.M.Groups[2].Value -ne 'capitol_trigger_boost_1' })
            $visit = @(Get-QaMotionEvents $c 'debug-commands: 10\.0 s: setviewpos') | Select-Object -First 1
            $early = @($boosts | Where-Object { $visit -and $_.T -lt $visit.T })
            Add-QaResult $c 'booster lines for the two visits (2 or more, all of capitol_trigger_boost_1, none before the first visit)' ($boosts.Count -ge 2 -and $other.Count -eq 0 -and $visit -and $early.Count -eq 0) (($boosts | ForEach-Object { '{0:N1} s {1}' -f $_.T, $_.M.Groups[2].Value }) -join '; ')
            # The blast carries the player 17 m at 75 m/s with the game's gravity off: on the ground at the
            # destination about 0.3 s later, so each pulse holds its minimum (0.3 s) or a little more.
            $held = @(Get-QaMotionEvents $c 'vignette: launch (\d+) \(booster [^)]*\) held ([\d.]+) s: (on the ground again|its limit)')
            $bad = @($held | Where-Object { [double]$_.M.Groups[3].Value -lt 0.3 -or [double]$_.M.Groups[3].Value -gt 4.0 })
            Add-QaResult $c 'every booster pulse held 0.3 to 4 s, ended by the ground or its limit' ($held.Count -ge $boosts.Count -and $held.Count -gt 0 -and $bad.Count -eq 0) (($held | ForEach-Object { $_.M.Value -replace '^.*vignette: ', '' }) -join '; ')
            $f = Get-QaMotionFrames $c
            Add-QaResult $c 'the 10 s vignette lines count launch frames' ($f.Launch -gt 0) ('{0} line(s), {1} launch frame(s), {2} ride frame(s)' -f $f.Lines, $f.Launch, $f.Ride)
            Test-QaAbsent $c "launch: jump pad '"
            Test-QaAbsent $c 'game: ride'
        }
    },
    @{
        Name = 'ride-hunter-tram'; Kind = 'game'; OnlyByName = $true
        Proves = 'The Doom Hunter Base tram carries no one: from e1m4''s front car (cp_02_post_train_fight) the coupling''s relay sends the rear cars away (the train''s mover, 900 m); the player''s car stands, so no ride and no launch line, and the trace never shows the body carried'
        Map = 'game/sp/e1m4_boss/e1m4_boss -checkpoint cp_02_post_train_fight'
        Env = $script:QaMotionEnv + @("ETERNALVR_DEBUG_COMMANDS=3:$script:QaMotionQuiet|4:getviewpos|8:activatetargets train_target_relay_7|20:getviewpos|40:getviewpos")
        Input = $script:QaRestPose
        Timeline = {
            param($c)
            Wait-QaLog $c 'debug-commands: 8\.0 s: activatetargets train_target_relay_7' 40 | Out-Null
            Wait-QaSeconds $c 25; Save-QaXrShot $c 'rear-gone'
            Wait-QaSeconds $c 15
        }
        Asserts = {
            param($c)
            Test-QaPresent $c 'debug-commands: 8.0 s: activatetargets train_target_relay_7'
            Test-QaAbsent $c 'game: ride'
            Test-QaAbsent $c 'vignette: launch'
            $step = @(Get-QaMotionEvents $c 'debug-commands: 8\.0 s: activatetargets') | Select-Object -First 1
            $carried = @(Get-QaMotionEvents $c 'game motion trace: ground 1 .*; carried ([\d.]+)' | Where-Object { $step -and $_.T -gt $step.T -and [double]$_.M.Groups[2].Value -ge 0.5 })
            Add-QaResult $c 'no trace step on the ground carried at 0.5 m/s or more after the rear cars leave' ([bool]$step -and $carried.Count -eq 0) ('{0} such step(s)' -f $carried.Count)
        }
    },
    @{
        Name = 'ride-argent-stream'; Kind = 'game'; OnlyByName = $true
        Proves = 'The jump into the Argent stream (e3m2_hell_b''s end) is a cutscene, so no ride: the pressure plate and both chains (their cutscenes), then the player put into the portal: a cutscene within 2 s, then the mission''s end; no ride and no launch line'
        Map = 'game/sp/e3m2_hell_b/e3m2_hell_b -checkpoint cp_09_towertop_combatend'
        Env = $script:QaMotionEnv + @('ETERNALVR_SKIP_CINEMATICS=0', "ETERNALVR_DEBUG_COMMANDS=3:$script:QaMotionQuiet|6:teleport game_trigger_plate_587207834|12:trigger titan_chains_target_melee_left|18:trigger titan_chains_target_melee_right|30:teleport titan_chains_portal_trigger")
        Input = $script:QaRestPose
        Timeline = {
            param($c)
            Wait-QaLog $c 'debug-commands: 30\.0 s: teleport titan_chains_portal_trigger' 60 | Out-Null
            Wait-QaSeconds $c 1.5; Save-QaXrShot $c 'jump'
            Wait-QaSeconds $c 10; Save-QaXrShot $c 'after'
        }
        Asserts = { param($c) Test-QaMotionCutscene $c 'debug-commands: 30\.0 s: teleport titan_chains_portal_trigger' }
    },
    @{
        Name = 'ride-spear-creature'; Kind = 'game'; OnlyByName = $true
        Proves = 'The World Spear''s creature ride is a cutscene, so no ride: the player put into the torch''s trigger (village_trigger_trigger_8) starts the torch scene and the flight above the clouds on the screen within 2 s; no ride and no launch line'
        Map = 'game/dlc2/e5m1_spear/e5m1_spear -checkpoint cp_12_stone_imp_intro'
        Env = $script:QaMotionEnv + @('ETERNALVR_SKIP_CINEMATICS=0', "ETERNALVR_DEBUG_COMMANDS=3:$script:QaMotionQuiet|6:teleport village_trigger_trigger_8")
        Input = $script:QaRestPose
        Timeline = {
            param($c)
            Wait-QaLog $c 'debug-commands: 6\.0 s: teleport village_trigger_trigger_8' 40 | Out-Null
            Wait-QaSeconds $c 30; Save-QaXrShot $c 'scene'
            Wait-QaSeconds $c 12
        }
        Asserts = { param($c) Test-QaMotionCutscene $c 'debug-commands: 6\.0 s: teleport village_trigger_trigger_8' }
    },
    @{
        Name = 'ride-swamp-titan'; Kind = 'game'; OnlyByName = $true
        Proves = 'The Titan carrying the platform (the Blood Swamps'' end) is a cutscene, so no ride: after the boss (CP_10_BOSS_post_combat, its relay), Use 2.5 m from the platform''s use proxy starts it on the screen; no ride and no launch line'
        Map = 'game/dlc/e4m2_swamp/e4m2_swamp -checkpoint CP_10_BOSS_post_combat'
        Env = $script:QaMotionEnv + @('ETERNALVR_SKIP_CINEMATICS=0', "ETERNALVR_DEBUG_COMMANDS=3:$script:QaMotionQuiet|5:activatetargets titan_level_raise_target_relay_activate_interact_1|7:setviewpos -34.5 286.8 8.8 0")
        Input = $script:QaRestPose
        Timeline = {
            param($c)
            Wait-QaLog $c 'debug-commands: 7\.0 s: setviewpos' 40 | Out-Null
            # Use (the weapon hand's stick click) facing the proxy, a few pitches, until the cutscene starts.
            $started = $false
            foreach ($pitch in -20, 0, 20, -20, 0, 20) {
                Write-QaInput $c (@("right.aim = 0, $pitch", 'left.aim = 0, 30', 'left.position = -0.2, -0.45, -0.25', 'right.position = 0.2, -0.45, -0.25', 'right.click = 1')); Wait-QaSeconds $c 0.3
                Write-QaInput $c $script:QaRestPose
                if (Wait-QaLog $c 'game: cutscene starts' 2) { $started = $true; break }
            }
            if (-not $started) { Add-QaNote $c 'no cutscene after six Use presses' }
            Wait-QaSeconds $c 15; Save-QaXrShot $c 'scene'
            Wait-QaSeconds $c 12
        }
        Asserts = { param($c) Test-QaMotionCutscene $c 'debug-commands: 7\.0 s: setviewpos' 15 }
    },
    @{
        Name = 'launch-hammer'; Kind = 'game'; OnlyByName = $true
        Proves = 'The Sentinel Hammer''s slam brings up the vignette''s pulse at the press: e5m2_earth''s start gives the hammer with its meter empty (presses and double jumps then are no launch), then on four of the map''s hammer charge pickups a slam standing or after a double jump: one slam line within 0.4 s of each press, from the ground or in the air as it was, each pulse held 0.3 to 2 s until the feet are down, launch frames, no jump pad or booster line and no ride'
        Map = 'game/dlc2/e5m2_earth/e5m2_earth'
        Env = $script:QaMotionEnv + @("ETERNALVR_DEBUG_COMMANDS=$script:QaMotionHammerSchedule")
        Input = $script:QaRestPose
        Timeline = {
            param($c)
            $p = $script:QaRestPose
            # The meter empty: a standing press, a double jump, and a double jump with a press.
            Wait-QaSeconds $c 1; Invoke-QaMotionPress $c $p 'left.click'; Wait-QaSeconds $c 2
            Invoke-QaMotionDoubleJump $c $p; Wait-QaSeconds $c 2
            Invoke-QaMotionDoubleJump $c $p; Invoke-QaMotionPress $c $p 'left.click'
            for ($k = 0; $k -lt 4; $k++) {
                if (-not (Wait-QaLog $c ('debug-commands: {0}\.0 s: setviewpos' -f (16 + $k * 20)) 120)) { Add-QaNote $c "pickup $k : not placed"; continue }
                Wait-QaSeconds $c 2.5
                if ($k % 2 -eq 1) { Invoke-QaMotionDoubleJump $c $p }
                Invoke-QaMotionPress $c $p 'left.click'
                if ($k -eq 0) { Wait-QaSeconds $c 0.3; Save-QaXrShot $c 'slam' }
            }
            Wait-QaSeconds $c 12
        }
        Asserts = {
            param($c)
            Test-QaPresent $c 'vignette: the launch hooks are in'
            $first = @(Get-QaMotionEvents $c 'debug-commands: 16\.0 s: setviewpos') | Select-Object -First 1
            $presses = @(Get-QaMotionEvents $c 'controllers: action crucible' | Where-Object { $first -and $_.T -gt $first.T })
            $slams = @(Get-QaMotionEvents $c 'vignette: launch (\d+): the Sentinel Hammer''s slam, (from the ground|in the air)')
            $early = @($slams | Where-Object { -not $first -or $_.T -lt $first.T })
            Add-QaResult $c 'no slam line with the meter empty (before the first pickup)' ($first -and $early.Count -eq 0) ('{0} slam line(s) before {1}' -f $early.Count, $(if ($first) { '{0:N1} s' -f $first.T } else { 'no placement' }))
            # One slam line per charged press, within 0.4 s of it (the hammer comes up about 30 ms after the press;
            # the landing, which an earlier reading started the pulse at, is 0.5 to 0.8 s later).
            $text = @(); $ok = $presses.Count -eq 4 -and $slams.Count -eq 4; $want = @('from the ground', 'in the air', 'from the ground', 'in the air')
            for ($i = 0; $i -lt $presses.Count; $i++) {
                $s = @($slams | Where-Object { $_.T -ge $presses[$i].T -and $_.T -le $presses[$i].T + 0.4 })
                if ($s.Count -ne 1 -or ($i -lt 4 -and $s[0].M.Groups[3].Value -ne $want[$i])) { $ok = $false }
                $text += 'press {0} at {1:N2} s: {2}' -f ($i + 1), $presses[$i].T, $(if ($s.Count) { '{0:N2} s on, {1}' -f ($s[0].T - $presses[$i].T), $s[0].M.Groups[3].Value } else { 'no slam line' })
            }
            Add-QaResult $c 'four charged presses, each with one slam line within 0.4 s, standing from the ground and after a double jump in the air' $ok ($text -join '; ')
            $held = @(Get-QaMotionEvents $c 'vignette: launch (\d+) \(the Sentinel Hammer''s slam\) held ([\d.]+) s: (on the ground again|its limit)')
            $bad = @($held | Where-Object { [double]$_.M.Groups[3].Value -lt 0.3 -or [double]$_.M.Groups[3].Value -gt 2.0 -or $_.M.Groups[4].Value -ne 'on the ground again' })
            Add-QaResult $c 'every slam pulse held 0.3 to 2 s, ended by the ground' ($held.Count -eq $slams.Count -and $held.Count -gt 0 -and $bad.Count -eq 0) (($held | ForEach-Object { $_.M.Value -replace '^.*vignette: ', '' }) -join '; ')
            $f = Get-QaMotionFrames $c
            Add-QaResult $c 'the 10 s vignette lines count launch frames' ($f.Launch -gt 0) ('{0} line(s), {1} launch frame(s), {2} ride frame(s)' -f $f.Lines, $f.Launch, $f.Ride)
            Test-QaAbsent $c "launch: jump pad '"
            Test-QaAbsent $c "launch: booster '"
            Test-QaAbsent $c 'game: ride'
        }
    }
)

# Before the launcher's end-to-end suite, which stays last.
$e2e = @($script:QaScenarios | Where-Object { $_.Kind -eq 'e2e' })
$script:QaScenarios = @($script:QaScenarios | Where-Object { $_.Kind -ne 'e2e' }) + $motionScenarios + $e2e
