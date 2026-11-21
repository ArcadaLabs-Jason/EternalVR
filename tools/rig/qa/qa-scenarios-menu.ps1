# A level started the way a player starts one (qa-menu.ps1, tools/rig/qa/README.md "Menu-started scenarios"): the
# title screen, Campaign, Game Slot 1 (the 100% save), Mission Select and a mission, all with the scripted pointer,
# instead of the `+map` load every other scenario uses. Dot-sourced by qa-suite.ps1 after qa-scenarios-depth.ps1:
# put before every other scenario, so in a whole suite it runs while the first slot still holds the 100% save the
# suite starts from (a `+map` run autosaves into that slot; qa-suite.ps1 puts the cloud files back first when it does
# not).
#
#   menu-mission  Mission Select's Cultist Base (e1m3) from the title screen: every step of the path within its time,
#                 the map loaded by the menus (after Mission Select's screen, no `+map` on the command line), head aim on
#                 after that load, and 15 s into play: the headset's frames head-tracked (the last 10 s `xr:` line's
#                 head-tracked count grew, its on-the-screen count by no more than a twentieth of that: no cinema-screen
#                 fallback), the frames table's last 3 s all head-tracked with the HUD quad shown (`ui_shown` 1), eye
#                 views in stereo, and no ERROR, exception or crash line
#   menu-crystal  run only when -Only names it (OnlyByName): Game Slot 2's Continue Game, which must hold a save of
#                 Exultia (e1m2) from before its Sentinel Crystal (the rig's slot 2 does; a save copied into a slot
#                 the profile lists otherwise is 'Corrupt save data' for the game, so the scenario takes the slot as
#                 it is and fails on another map's load), the crystal reached by teleport, snap turns until it is
#                 ahead and Use under hand aim, its upgrade menu up as a popup over the game, an upgrade unlocked by
#                 holding the trigger on it, and the crystal's sync ('interact/argent_cell/use_sync') played; from the
#                 ETERNALVR_TEST_CAMERA_TRACE=1 lines: no turn of the view over 5 degrees in one game frame, the view
#                 on the camera's heading through the sync (the crush in front; with Still view within 1 degree, and no
#                 pitch or roll written), the menu's heading after it
#   menu-crystal-play, menu-crystal-behind, menu-crystal-behind-play  the same with Camera moves (the camera's pitch
#                 and roll played, no step over 1.25 times the camera's own motion or its release in any angle), and
#                 with only the hand pointed at the crystal behind the player (the head looks away; the crush is behind
#                 the view: the facing checks and Still's tilt are left out)
#   menu-rune, menu-rune-play, menu-modbot, menu-modbot-play  the same checks at the same save's rune (CHOOSE A RUNE)
#                 and mod bot (CHOOSE A WEAPON MOD, the player placed in front of the drone; also its tabs), facing them
#   <name>-head, <name>-pad, <name>-view  the six facing ones (crystal, rune, mod bot, each with both settings) under
#                 head aim with the motion controllers, in gamepad mode (the pad: the menus, Use and the turn to the
#                 pickup by it) and under view aim with the motion controllers (Camera moves keeps no heading there:
#                 its heading after is checked as Still view's), the head sweeping (qa-scenarios-pickup.ps1
#                 New-QaAimVariant), by name only
#   menu-crystal-turns, menu-crystal-sway  measurements for Still view's hold: a fast hand and snap turns in the sync,
#                 or a swaying head; whether the player's view angles move in the sync and how many holds start

$script:QaMenuXrLine = 'xr: (\d+) frame\(s\), .*?; (\d+) head-tracked, (\d+) on the screen'

# The frames table's rows of the last $Seconds of the run (complete rows only: the game may stop in mid-write).
function Get-QaLastFrames($Ctx, [double]$Seconds) {
    $csv = @(Get-ChildItem -LiteralPath $Ctx.LogsDir -Filter 'eternalvr-frames-*.csv' -ErrorAction SilentlyContinue | Sort-Object Length) | Select-Object -Last 1
    if (-not $csv) { return @() }
    $rows = @(Import-Csv -LiteralPath $csv.FullName | Where-Object { $_.ui_shown -ne $null -and $_.ui_shown -ne '' -and $_.gui_age_ms -ne $null })
    if ($rows.Count -eq 0) { return @() }
    $end = [double]$rows[-1].display_time_ns
    return @($rows | Where-Object { $end - [double]$_.display_time_ns -le $Seconds * 1e9 })
}

$menuScenarios = @(
    @{
        Name = 'menu-mission'; Kind = 'game'; MenuMission = 'e1m3_cult'
        Proves = 'A mission started from the main menu as a player starts it (title screen, Campaign, Game Slot 1, Mission Select, Cultist Base) reaches play head-tracked with the HUD on its quad and no fallback to the cinema screen'
        Env = @()
        Input = @()
        Timeline = {
            param($c)
            Wait-QaSeconds $c 15
            Save-QaShot $c 'in-map'
            Save-QaXrShot $c 'in-map'
            Save-QaGameWindowShot $c 'in-map'
            # A 10 s xr line after the shots, with the whole window in play.
            Wait-QaSeconds $c 11
        }
        Asserts = {
            param($c)
            $text = Get-QaLogText $c
            $select = [regex]::Match($text, 'mp guard: main menu screen 14 ')
            $load = [regex]::Match($text, "mp guard: map load 'game/sp/e1m3_cult/e1m3_cult'")
            Add-QaResult $c 'the menus loaded the mission (its map load after Mission Select''s screen)' ($select.Success -and $load.Success -and $select.Index -lt $load.Index) `
                $(if ($load.Success) { 'map load at offset ' + $load.Index } else { 'no map load of e1m3_cult' })
            $launch = Get-Content -LiteralPath (Join-Path $c.Dir 'launch.txt') -Raw -ErrorAction SilentlyContinue
            Add-QaResult $c 'no +map on the command line' ($launch -and $launch -notmatch '\+map ') 'launch.txt'
            $aim = [regex]::Match($text, 'aim: head aim on')
            Add-QaResult $c 'head aim on after the menus'' map load' ($load.Success -and $aim.Success -and $aim.Index -gt $load.Index) ''
            Test-QaPresent $c 'stereo: eye views'
            $xr = @([regex]::Matches($text, $script:QaMenuXrLine))
            if ($xr.Count -ge 2) {
                $a = $xr[$xr.Count - 2].Groups; $b = $xr[$xr.Count - 1].Groups
                $tracked = [long]$b[2].Value - [long]$a[2].Value
                $screen = [long]$b[3].Value - [long]$a[3].Value
                Add-QaResult $c 'the last 10 s xr window head-tracked, no cinema-screen fallback' ($tracked -gt 0 -and $screen * 20 -le $tracked) "$tracked head-tracked, $screen on the screen"
            } else {
                Add-QaResult $c 'the last 10 s xr window head-tracked, no cinema-screen fallback' $false "$($xr.Count) xr line(s)"
            }
            $rows = @(Get-QaLastFrames $c 3)
            $bad = @($rows | Where-Object { $_.view -eq '0' -or $_.ui_shown -ne '1' })
            Add-QaResult $c 'the last 3 s of frames head-tracked with the HUD quad shown' ($rows.Count -ge 100 -and $bad.Count -eq 0) "$($rows.Count) frame(s), $($bad.Count) not head-tracked or without the quad"
            Test-QaAbsent $c 'ERROR|exception|crash' -Regex -CaseSensitive
        }
    }
)

# The Sentinel Crystal of Exultia (e1m2) from the start of the mission: teleported onto it, the crystal is behind the
# player (rig, 2026-11-12, runs q4 and cv1; 90 or 180 degrees to the left in runs clp6 to clp8). menu-crystal and
# menu-crystal-play turn the player with snap turns of 90 degrees until Use with the hand ahead (30 down) opens the
# upgrade menu: the head looks at the crystal, as a player's does. The -behind scenarios point only the hand at it
# (a list of hand aims until the menu comes up): the simulator's head stays still, so the head looks away from the
# crystal by the hand's 180 degrees (the first runs, q4 and q5play: the crush behind the view).
$script:QaCrystalSchedule = '3:god;g_setting_hud_auto_dismiss_tutorials 1;g_setting_tutorials 0|3.5:sync_printInteractionAndAnimationName 1|' +
    '5:teleport pickups_progress_argent_cell_1_1318987582_e1m2|6:where'
# The upgrade the save's crystal offers first: Quickdraw Belch's right end (its armour side) on the 1280 x 720 menu. A
# hold on the line between it and Napalm Belch only shows the other routing.
$script:QaCrystalUpgrade = @(549, 313)
# The rune of the same save (pickups_progress_rune_1_e1m2, in the tower after the courtyard): it opens when the player
# touches it, with no trial (rig, 2026-11-12, run pr1). Teleported onto it the player faces away from it, as at the
# crystal; Use opens CHOOSE A RUNE, a held trigger on a rune of the catalogue (the first of its second row) unlocks it,
# the menu closes and the rune's sync plays, about 7.6 s and a forced view all along.
$script:QaRuneSchedule = '3:god;g_setting_hud_auto_dismiss_tutorials 1;g_setting_tutorials 0|3.5:sync_printInteractionAndAnimationName 1|' +
    '5:teleport pickups_progress_rune_1_e1m2|6:where'
# The mod bot of the same save (pickups_progress_mod_bot_1_e1m2, in the hell section): a teleport puts the player on the
# drone's origin, with the eye inside the drone (its clip reaches about 0.55 to 1.55 m over that origin, the eye 1.29),
# where it is not seen and Use reaches nothing (the earlier runs' "no drone"). The player is placed 1.2 m in front of
# it, facing it. Use opens CHOOSE A WEAPON MOD, one tab a weapon; the save holds both mods of its Combat Shotgun and
# Heavy Cannon (nothing to unlock there), so the Plasma Rifle is given first: its tab, the third (two presses of the
# right grip, RG), offers Heat Blast, and a held trigger on it unlocks it (rig, runs pr4 to pr8).
$script:QaModBotSchedule = '3:god;g_setting_hud_auto_dismiss_tutorials 1;g_setting_tutorials 0|3.5:sync_printInteractionAndAnimationName 1|' +
    '4:give weapon/player/plasma_rifle|5:teleport pickups_progress_mod_bot_1_e1m2|6:setviewpos -92.61 -547.78 -2.47 228.4|6.5:where'
# What a menu-started pickup scenario reaches, by its Pickup field (New-QaCrystalScenario's $Pickup). Tabs: the menu's
# tabs to go right (the right grip) before the hold; the menu then shows at least Tabs + 1 tabs (the mod bot's third,
# the Plasma Rifle's, only with the rifle given).
$script:QaMenuPickups = @{
    crystal = @{ Schedule = $script:QaCrystalSchedule; Pixel = $script:QaCrystalUpgrade; Sync = 'interact/argent_cell/use_sync'; What = 'the crystal'; Tabs = 0 }
    rune = @{ Schedule = $script:QaRuneSchedule; Pixel = @(140, 380); Sync = 'interact/rune/use_sync'; What = 'the rune'; Tabs = 0 }
    modbot = @{ Schedule = $script:QaModBotSchedule; Pixel = @(234, 423); Sync = 'interact/mod_bot/mod_bot_sync'; What = 'the mod bot'; Tabs = 2 }
}
function Get-QaMenuPickupByKey([string]$Key) {
    if (-not $Key -or -not $script:QaMenuPickups.ContainsKey($Key)) {
        throw "unknown menu pickup '$Key' (pickups: $(($script:QaMenuPickups.Keys | Sort-Object) -join ', '))"
    }
    return $script:QaMenuPickups[$Key]
}
function Get-QaMenuPickup($Ctx) { return Get-QaMenuPickupByKey $Ctx.Scenario.Pickup }
$script:QaMenuUnderLine = 'aim: under the menu the view faces the camera the game drives \(body yaw (-?[\d.]+), held (-?[\d.]+); game view yaw (-?[\d.]+)\)'
# The trace line (camera_anim_hook.cpp traceView) with the log's clock and what head aim made of the view: `sync trace`
# for a view of the sync or the menu outside a forced view's window.
$script:QaCrystalTrace = '(?m)^\[\s*([\d.]+)\] \[\s*\d+\] camera: (?:sync )?trace (\d+): game \((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\) angles \((?:(-?[\d.]+) (-?[\d.]+)|none)\) written \((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\) eye \([^)]*\)( forced)?; body (-?[\d.]+) head \((-?[\d.]+) (-?[\d.]+)\) held (\S+?)( menu)?( sync)?\r?$'
# Camera moves: what is left of the turn as the pickup ends eases out over this (xr_math::CameraTurn's release, a
# smoothstep: its steepest step is 1.5 times the even one).
$script:QaCameraReleaseSeconds = 0.3

# The line that tells the pickup's menu came up: the cursor's over the game, or in gamepad mode (no cursor with a pad)
# the layer's menu without one.
$script:QaMenuCursorLine = 'menu: the game shows its cursor \(a popup over the game'
$script:QaPadMenuLine = 'menu: a menu over the game without a cursor'
function Get-QaMenuLine($Ctx) { if ($Ctx.Scenario.Pad) { return $script:QaPadMenuLine } else { return $script:QaMenuCursorLine } }

# Use at the crystal until its upgrade menu comes up; $true when it did (a note says how).
function Invoke-QaCrystalUse($c, [bool]$Facing) {
    if ($c.Scenario.Pad) {
        # Gamepad mode: Use is the pad's right stick click, twice at each heading (the head's sweep in between), and the
        # player turns with the pad's look stick (the game's own turn, a push of 0.2 s about 10 to 15 degrees here)
        # until Use opens the menu. The first Use waits for the schedule's placing to settle.
        Wait-QaSeconds $c 1.5
        for ($i = 0; $i -lt 30; $i++) {
            foreach ($try in 1, 2) {
                $mark = Get-QaLogMark $c
                Invoke-QaPadPress $c 'rs' 0.3
                if (Wait-QaLogAfter $c $mark $script:QaPadMenuLine 1.5) {
                    Add-QaNote $c "Use (the pad's right stick click) after $i push(es) of the look stick opened the menu"
                    return $true
                }
            }
            Invoke-QaPadPress $c 'rx=20000' 0.2; Wait-QaSeconds $c 0.5
        }
        return $false
    }
    $tries = if ($Facing) { @(0, 1, 2, 3) } else { @(@(90, -30), @(0, -60), @(0, -30), @(0, 0), @(180, -30), @(-90, -30)) }
    # Facing: at each heading (0 to 3 snap turns), the hand ahead 30 then 45 down. Behind: one aim per try. Each
    # branch assigns (an if's output would unroll a single pair into its two numbers).
    $facingAims = @(@(0, -30), @(0, -45))
    foreach ($t in $tries) {
        if ($Facing) { $aims = $facingAims } else { $aims = @(, $t) }
        foreach ($a in $aims) {
            $mark = Get-QaLogMark $c
            Write-QaInput $c @("right.aim = $($a[0]), $($a[1])"); Wait-QaSeconds $c 0.8
            Write-QaInput $c @("right.aim = $($a[0]), $($a[1])", 'right.click = 1'); Wait-QaSeconds $c 0.3
            Write-QaInput $c @("right.aim = $($a[0]), $($a[1])")
            if (Wait-QaLogAfter $c $mark $script:QaMenuCursorLine 2.5) {
                $how = if ($Facing) { "after $t snap turn(s) of 90 deg to the left, " } else { '' }
                Add-QaNote $c "Use at hand aim $($a[0]), $($a[1]) ${how}opened the upgrade menu"
                return $true
            }
        }
        if ($Facing) {
            # One snap turn to the left (ETERNALVR_TURN=snap, ETERNALVR_SNAP_DEGREES=90).
            Write-QaInput $c @('right.aim = 0, -30', 'right.stick = -1, 0'); Wait-QaSeconds $c 0.3
            Write-QaInput $c @('right.aim = 0, -30'); Wait-QaSeconds $c 0.8
        }
    }
    return $false
}

# Over the trace lines from the last menu before the sync to the end, with the written pitch and yaw taken less the
# head's (the trace's head: a head that moves under head aim is not a turn of the view; with the simulator's head still,
# the written view itself): the written yaw in the menu (its first line), the sync's lines (the written yaw's gap from
# the game camera's: the crush is drawn in front of that camera; GapRef its median after the opening below, signed, and Spread* how far each
# line's gap is from it), the largest change of the written yaw between two consecutive game views and where it was,
# and the written yaw on the last line (the heading after). Tilt: the written pitch's and roll's largest distance from
# 0 in the sync; TiltRange: the larger of their ranges there; StepAll: the written view's largest change in any angle
# between two consecutive game views from the menu on. GameStep: the game camera's largest step in any angle
# between two consecutive game views of the sync that is not a cut (20 degrees or more, or a lone step over 5 with the
# steps before and after it under a quarter of it: xr_math::CameraTurn's rule); FrameSeconds: the sync's game view time
# (its lines' span over their count); Span: the seconds from its first line to its last. All angles wrapped to +-180.
function Get-QaCrystalTrace($Ctx) {
    $rows = @(foreach ($m in [regex]::Matches((Get-QaLogText $Ctx), $script:QaCrystalTrace)) {
        $g = $m.Groups
        [pscustomobject]@{ T = [double]::Parse($g[1].Value, [Globalization.CultureInfo]::InvariantCulture); N = [long]$g[2].Value
            GamePitch = [double]$g[3].Value; GameYaw = [double]$g[4].Value; GameRoll = [double]$g[5].Value
            AnglesYaw = $(if ($g[7].Success) { [double]$g[7].Value } else { $null })
            Pitch = [double]$g[8].Value - [double]$g[13].Value; Yaw = [double]$g[9].Value - [double]$g[14].Value
            Roll = [double]$g[10].Value; Forced = $g[11].Success
            Body = [double]$g[12].Value; HeadYaw = [double]$g[14].Value; Held = $g[15].Value; Menu = $g[16].Success; Sync = $g[17].Success }
    })
    $wrap = { param($d) (($d % 360) + 540) % 360 - 180 }
    $r = [ordered]@{ Rows = $rows.Count; SyncRows = 0; MenuYaw = $null; GapMedian = 999.0; GapMax = 999.0; GapRef = 999.0
                     SpreadMedian = 999.0; SpreadMax = 999.0; Step = 0.0; StepAt = ''; LastYaw = $null; LastGameYaw = $null
                     AnglesMoves = 0; AnglesLargest = 0.0; Tilt = 999.0; TiltRange = 0.0; StepAll = 0.0; GameStep = 0.0
                     FrameSeconds = 0.0; Span = 0.0; StepRest = 0.0; OpenNet = 0.0; OpenPath = 0.0; OpenFrom = 0
                     SyncEndYaw = $null; MenuRange = 0.0; MenuRows = 0; HeadRange = 0.0; OpenBig = 0
                     OpenMax = 0.0 }
    $first = -1
    for ($i = 0; $i -lt $rows.Count; $i++) { if ($rows[$i].Sync) { $first = $i; break } }
    if ($first -lt 0) { return $r }
    $start = $first
    while ($start -gt 0 -and $rows[$start - 1].Menu -and $rows[$start - 1].N -eq $rows[$start].N - 1) { $start-- }
    $r.MenuYaw = & $wrap $rows[$start].Yaw
    # MenuRange: how far the written yaw (less the head's) moved over the menu's lines before the sync.
    $low = 0.0; $high = 0.0
    for ($i = $start; $i -lt $first; $i++) { $d = & $wrap ($rows[$i].Yaw - $rows[$start].Yaw); $low = [Math]::Min($low, $d); $high = [Math]::Max($high, $d) }
    $r.MenuRange = $high - $low
    $r.MenuRows = $first - $start
    $heads = @($rows | ForEach-Object { $_.HeadYaw } | Measure-Object -Minimum -Maximum)
    $r.HeadRange = $heads[0].Maximum - $heads[0].Minimum
    $sync = @($rows | Where-Object { $_.Sync })
    $signed = @($sync | ForEach-Object { & $wrap ($_.Yaw - $_.GameYaw) } | Sort-Object)
    $gaps = @($signed | ForEach-Object { [Math]::Abs($_) } | Sort-Object)
    $r.SyncRows = $gaps.Count
    $r.GapMedian = $gaps[[int][Math]::Floor($gaps.Count / 2)]
    $r.GapMax = $gaps[-1]
    $r.Span = $sync[-1].T - $sync[0].T
    if ($sync.Count -ge 2) { $r.FrameSeconds = $r.Span / ($sync.Count - 1) }
    # The camera's steps between consecutive game views of the sync, then its largest that is not a cut.
    $steps = @(for ($i = $first + 1; $i -lt $rows.Count; $i++) {
        if (-not $rows[$i].Sync -or -not $rows[$i - 1].Sync -or $rows[$i].N -ne $rows[$i - 1].N + 1) { , $null; continue }
        $s = 0.0
        foreach ($k in 'GamePitch', 'GameYaw', 'GameRoll') { $s = [Math]::Max($s, [Math]::Abs((& $wrap ($rows[$i].$k - $rows[$i - 1].$k)))) }
        , $s
    })
    # As CameraTurn: the step before a step is the last one played (none after a cut or a gap), and the sync's first
    # two steps are one cut when the step after them is under a quarter of their sum and either each is a quarter of
    # the other or more, neither is huge and together they pass 5 degrees, or the first passes 5 and the second is huge
    # (the start's cut over two frames, openingPair).
    $before = 0.0
    if ($steps.Count -ge 2 -and $null -ne $steps[0] -and $null -ne $steps[1]) {
        $a = $steps[0]; $b = $steps[1]; $c = if ($steps.Count -ge 3 -and $null -ne $steps[2]) { $steps[2] } else { 0.0 }
        $split = $a -lt 20 -and $b -lt 20 -and $a -ge $b / 4 -and $b -ge $a / 4 -and $a + $b -gt 5
        if (($split -or ($a -gt 5 -and $b -ge 20)) -and $c -lt ($a + $b) / 4) { $steps[0] = $null; $steps[1] = $null }
    }
    for ($i = 0; $i -lt $steps.Count; $i++) {
        $s = $steps[$i]
        if ($null -eq $s) { $before = 0.0; continue }
        $after = if ($i + 1 -lt $steps.Count -and $null -ne $steps[$i + 1]) { $steps[$i + 1] } else { 0.0 }
        if ($s -ge 20 -or ($s -gt 5 -and $before -lt $s / 4 -and $after -lt $s / 4)) { $before = 0.0; continue }
        $r.GameStep = [Math]::Max($r.GameStep, $s)
        $before = $s
    }
    # The player's view angles in the sync: how often they changed from one game view to the next, the largest change.
    for ($i = $first + 1; $i -lt $rows.Count; $i++) {
        if (-not $rows[$i].Sync -or $rows[$i].N -ne $rows[$i - 1].N + 1 -or $null -eq $rows[$i].AnglesYaw -or $null -eq $rows[$i - 1].AnglesYaw) { continue }
        $d = [Math]::Abs((& $wrap ($rows[$i].AnglesYaw - $rows[$i - 1].AnglesYaw)))
        if ($d -gt 0.5) { $r.AnglesMoves++ }
        $r.AnglesLargest = [Math]::Max($r.AnglesLargest, $d)
    }
    # The opening: the sync's first five game views, or the five from the menu's close when it closes later (a menu
    # over the sync's first views holds the view until then): a cut of the camera's heading in its first two, shown
    # whole once Still's hold has kept it two views. StepRest: Step without the opening; OpenNet / OpenPath:
    # the yaw's turn over it and the sum of its steps (a cut shown once goes one way: the path is the turn; a flick
    # shown goes and comes back). SyncEndYaw: the yaw on the sync's last line.
    $open = $first
    while ($open + 1 -lt $rows.Count -and $rows[$open].Menu -and $rows[$open].Sync) { $open++ }
    $r.OpenFrom = $open - $first
    # GapRef and Spread* over the sync's lines after the opening (the camera's own cut and flick at the start are not the
    # view's).
    $late = @(for ($i = $open + 5; $i -lt $rows.Count; $i++) { if ($rows[$i].Sync) { & $wrap ($rows[$i].Yaw - $rows[$i].GameYaw) } })
    if ($late.Count -gt 0) {
        $sorted = @($late | Sort-Object)
        $r.GapRef = $sorted[[int][Math]::Floor($sorted.Count / 2)]
        $spread = @($late | ForEach-Object { [Math]::Abs((& $wrap ($_ - $r.GapRef))) } | Sort-Object)
        $r.SpreadMedian = $spread[[int][Math]::Floor($spread.Count / 2)]
        $r.SpreadMax = $spread[-1]
    }
    for ($i = $start + 1; $i -lt $rows.Count; $i++) {
        if ($rows[$i].N -ne $rows[$i - 1].N + 1) { continue }
        $d = & $wrap ($rows[$i].Yaw - $rows[$i - 1].Yaw)
        if ($i -ge $first -and $i -le $open + 4) {
            $r.OpenPath += [Math]::Abs($d)
            if ([Math]::Abs($d) -gt 5) { $r.OpenBig++ }
            $r.OpenMax = [Math]::Max($r.OpenMax, [Math]::Abs($d))
        } else {
            $r.StepRest = [Math]::Max($r.StepRest, [Math]::Abs($d))
        }
        if ([Math]::Abs($d) -gt [Math]::Abs($r.Step)) {
            $r.Step = $d
            $what = if ($rows[$i - 1].Menu -and -not $rows[$i].Menu) { 'as the menu closed' } elseif ($rows[$i].Sync -and -not $rows[$i - 1].Sync) { 'as the sync started' } elseif ($rows[$i - 1].Sync -and -not $rows[$i].Sync) { 'as the sync ended' } elseif ($rows[$i].Sync) { 'in the sync' } elseif ($rows[$i].Menu) { 'in the menu' } else { 'after the sync' }
            $r.StepAt = "trace $($rows[$i].N), $what"
        }
        foreach ($k in 'Pitch', 'Yaw', 'Roll') { $r.StepAll = [Math]::Max($r.StepAll, [Math]::Abs((& $wrap ($rows[$i].$k - $rows[$i - 1].$k)))) }
    }
    $r.Tilt = 0.0
    foreach ($row in $sync) { $r.Tilt = [Math]::Max($r.Tilt, [Math]::Max([Math]::Abs((& $wrap $row.Pitch)), [Math]::Abs((& $wrap $row.Roll)))) }
    # The range of each about its first value in the sync, wrapped, so a roll about +-180 is not a range of 360.
    foreach ($k in 'Pitch', 'Roll') {
        $v0 = $sync[0].$k
        $v = @($sync | ForEach-Object { & $wrap ($_.$k - $v0) } | Measure-Object -Minimum -Maximum)
        $r.TiltRange = [Math]::Max($r.TiltRange, $v[0].Maximum - $v[0].Minimum)
    }
    if ($first -ge 1 -and $open + 4 -lt $rows.Count) { $r.OpenNet = & $wrap ($rows[$open + 4].Yaw - $rows[$first - 1].Yaw) }
    $r.SyncEndYaw = & $wrap $sync[-1].Yaw
    $r.LastYaw = & $wrap $rows[-1].Yaw
    $r.LastGameYaw = $rows[-1].GameYaw
    return $r
}

# $Pickup: a key of $script:QaMenuPickups (an unknown one throws as the suite loads).
function New-QaCrystalScenario([string]$Name, [string]$Proves, [string[]]$ExtraEnv, [string]$Pickup = 'crystal') {
    $what = Get-QaMenuPickupByKey $Pickup
    return @{
        Name = $Name; Kind = 'game'; OnlyByName = $true; Pickup = $Pickup
        Camera = $(if ($ExtraEnv -contains 'ETERNALVR_PICKUP_CAMERA=play') { 'play' } else { 'still' })
        MenuMission = 'continue'; MenuSlot = 2; MenuMap = 'game/sp/e1m2_battle/e1m2_battle'
        MenuSaveNote = "the save this scenario needs (Exultia from before its Sentinel Crystal, its rune and its mod bot; the test PC's slot 2 holds one)"
        Proves = $Proves
        Env = @('ETERNALVR_AIM=hand', 'ETERNALVR_TEST_CAMERA_TRACE=1', "ETERNALVR_DEBUG_COMMANDS=$($what.Schedule)") + $ExtraEnv
        Input = @()
        Timeline = {
            param($c)
            $p = Get-QaMenuPickup $c
            Wait-QaSeconds $c 7 # the teleport (5 s) has run
            if (-not (Invoke-QaCrystalUse $c ($c.Name -notlike '*-behind*'))) {
                Add-QaResult $c "$($p.What)'s menu came up" $false "no menu after Use at every aim tried: the slot's save may be from after $($p.What)"
                Save-QaGameWindowShot $c 'no-menu'; return
            }
            Wait-QaSeconds $c 1
            Save-QaXrShot $c 'upgrade-menu'
            Save-QaGameWindowShot $c 'upgrade-menu'
            $gone = $null
            if ($c.Scenario.Pad) {
                # Gamepad mode: the menu's own pad navigation. Its first item is highlighted (the crystal's Quickdraw
                # Belch, the rune catalogue's first, the mod bot tab's first mod: Heat Blast on the Plasma Rifle's
                # tab); the right shoulder goes one tab right, A held 3.5 s unlocks the highlighted item.
                # A push of the pad's look stick under the menu, as a player's thumb may give it: the game holds its view,
                # and the world behind the menu must not turn (it did by the push's 30 degrees or so before the menu held
                # the body in gamepad mode).
                Invoke-QaPadPress $c 'rx=24000' 0.4; Wait-QaSeconds $c 1
                for ($k = 0; $k -lt $p.Tabs; $k++) { Invoke-QaPadPress $c 'rb' 0.2; Wait-QaSeconds $c 0.8 }
                if ($p.Tabs -gt 0) { Save-QaGameWindowShot $c 'menu-tab' }
                for ($try = 1; $try -le 2 -and -not $gone; $try++) {
                    $mark = Get-QaLogMark $c
                    Invoke-QaPadPress $c 'a' 3.5
                    $gone = Wait-QaLogAfter $c $mark "controllers: sync '" 3
                }
            } else {
                $aim = @(Get-QaMenuAim (Get-QaMenuPanel $c) $p.Pixel[0] $p.Pixel[1])
                for ($k = 0; $k -lt $p.Tabs; $k++) {
                    Write-QaInput $c ($aim + 'right.grip = 1'); Wait-QaSeconds $c 0.2
                    Write-QaInput $c $aim; Wait-QaSeconds $c 0.8
                }
                if ($p.Tabs -gt 0) { Save-QaGameWindowShot $c 'menu-tab' }
                for ($try = 1; $try -le 2 -and -not $gone; $try++) {
                    $mark = Get-QaLogMark $c
                    Write-QaInput $c $aim; Wait-QaSeconds $c 0.8
                    Write-QaInput $c ($aim + 'right.trigger = 1'); Wait-QaSeconds $c 3.5
                    Write-QaInput $c $aim
                    $gone = Wait-QaLogAfter $c $mark 'menu: the cursor is gone' 2
                }
                Write-QaInput $c @()
            }
            if (-not $gone) { Add-QaNote $c 'the upgrade menu stayed up after two held unlocks'; Save-QaGameWindowShot $c 'menu-stays'; return }
            if ($c.Name -like '*-turns') {
                # During the sync: a fast hand (its yaw swept 30 degrees each way every 0.25 s, up to 750 degrees a
                # second), then a snap turn each way.
                Write-QaInput $c @('right.aim = 0, -30', 'right.sweep = 30, 0.25, 4'); Wait-QaSeconds $c 1.1
                Write-QaInput $c @('right.aim = 0, -30', 'right.stick = 1, 0'); Wait-QaSeconds $c 0.25
                Write-QaInput $c @('right.aim = 0, -30'); Wait-QaSeconds $c 0.4
                Write-QaInput $c @('right.aim = 0, -30', 'right.stick = -1, 0'); Wait-QaSeconds $c 0.25
                Write-QaInput $c @()
            }
            # The crush, in the headset's frame (the shots take a few tenths of a second each).
            Wait-QaSeconds $c 0.5
            Save-QaXrShot $c 'sync-1'
            Wait-QaSeconds $c 0.5
            Save-QaXrShot $c 'sync'
            Save-QaGameWindowShot $c 'sync'
            [void](Wait-QaLogAfter $c (Get-QaLogMark $c) 'SetViewAngles called from RVA 0xB4567C' 12)
            Wait-QaSeconds $c 2
            Save-QaXrShot $c 'after'
            Wait-QaSeconds $c 10 # a whole 10 s xr window after the sync
        }
        Asserts = {
            param($c)
            $p = Get-QaMenuPickup $c
            Test-QaPresent $c (Get-QaMenuLine $c) -Regex
            Test-QaPresent $c "controllers: sync '$($p.Sync)'"
            $text = Get-QaLogText $c
            $sync = [regex]::Match($text, "\[\s*([\d.]+)\] \[\s*\d+\] controllers: sync '$([regex]::Escape($p.Sync))'")
            $end = if ($sync.Success) { [regex]::Match($text.Substring($sync.Index), '\[\s*([\d.]+)\] \[\s*\d+\] controllers: SetViewAngles called from RVA 0xB4567C') } else { $null }
            $syncSeconds = $null
            if ($sync.Success -and $end -and $end.Success) {
                $t0 = [double]::Parse($sync.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture)
                $t1 = [double]::Parse($end.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture)
                $syncSeconds = $t1 - $t0
                $inside = @([regex]::Matches($text.Substring($sync.Index, $end.Index), 'controllers: forced view starts')).Count
                Add-QaNote $c ('the sync ran {0:N2} s to the game''s SetViewAngles (RVA 0xB4567C); {1} forced view(s) began in it' -f $syncSeconds, $inside)
            }
            if ($p.Tabs -gt 0) {
                # The tabs the mod bot's menu showed (the Plasma Rifle's third only once the schedule's give worked) and the
                # tab keys the right grip sent after the menu came up: which of the two failed when the hold unlocks nothing.
                $menuAt = [regex]::Match($text, (Get-QaMenuLine $c))
                $after = if ($menuAt.Success) { $text.Substring($menuAt.Index) } else { '' }
                $tabs = @([regex]::Matches($text, 'prompts: a tab list shows: keys \S+ / \S+, (\d+) tabs') | ForEach-Object { [int]$_.Groups[1].Value }) | Measure-Object -Maximum
                $shown = if ($tabs.Count) { [int]$tabs.Maximum } else { 0 }
                $sent = @([regex]::Matches($after, 'menu: key down 0x45')).Count
                if ($c.Scenario.Pad) {
                    # The pad's right shoulder goes to the game itself, and the prompts' hooks (the tab list's line) are
                    # not in with the controllers off: the sync, which needs the Plasma Rifle's tab, tells.
                    Add-QaNote $c "the menu's tabs: not logged in gamepad mode; the right shoulder pressed $($p.Tabs) time(s)"
                } else {
                    Add-QaResult $c "the menu's tabs: $($p.Tabs + 1) or more shown and $($p.Tabs) tab key(s) (E) sent" ($shown -ge $p.Tabs + 1 -and $sent -ge $p.Tabs) "$shown tab(s) shown (the schedule's give adds one), $sent E key(s) sent after the menu came up"
                }
            }
            $under = [regex]::Match($text, $script:QaMenuUnderLine)
            if ($under.Success) {
                $g = $under.Groups
                $turn = [double]$g[1].Value - [double]$g[2].Value
                $gap = [Math]::Abs((([double]$g[3].Value - [double]$g[1].Value) % 360 + 540) % 360 - 180)
                Add-QaNote $c ('as the menu handed over to the sync the view turned {0:N1} deg in one frame (body yaw {1} from {2}); the game''s camera faced {3:N1} deg away from the view' -f $turn, $g[1].Value, $g[2].Value, $gap)
            }
            $t = Get-QaCrystalTrace $c
            Add-QaNote $c ('trace: {0} line(s), {1} in the sync; written yaw {2} in the menu, {3} at the end (game {4}); the sync''s written yaw off the game camera''s by {5:N1} (median) to {6:N1} deg; largest one-frame turn {7:N1} deg ({8})' -f
                $t.Rows, $t.SyncRows, $t.MenuYaw, $t.LastYaw, $t.LastGameYaw, $t.GapMedian, $t.GapMax, $t.Step, $t.StepAt)
            # Traced: the sync's lines cover nine tenths of the sync, from its line to the game's SetViewAngles (the mod
            # bot's 2.8 s sync gives about 84 lines at 30 game frames a second, 170 at 60), and 20 lines at least.
            $traced = $null -ne $syncSeconds -and $t.SyncRows -ge 20 -and $t.Span -ge 0.9 * $syncSeconds
            $tracedText = '{0} sync line(s) over {1:N2} s of the sync''s {2} s' -f $t.SyncRows, $t.Span, $(if ($null -ne $syncSeconds) { '{0:N2}' -f $syncSeconds } else { 'unknown' })
            $holds = if ($sync.Success) { @([regex]::Matches($text.Substring($sync.Index), "pickup: a pickup's camera jumped")).Count } else { 0 }
            Add-QaNote $c ('the player''s view angles in the sync: changed in {0} game view(s), by up to {1:N2} deg; {2} hold(s) of the camera''s heading started after the sync began' -f $t.AnglesMoves, $t.AnglesLargest, $holds)
            if ($c.Name -like '*-turns' -or $c.Name -like '*-sway') {
                # Measurements: the hand, the stick or the head move on purpose; the checks below are of the view.
                Add-QaResult $c 'trace: the sync traced (its lines cover nine tenths of it)' $traced $tracedText
                return
            }
            Add-QaResult $c 'trace: the sync traced (its lines cover nine tenths of it)' $traced $tracedText
            if ($c.Scenario.Aim) {
                # The head's sweep reached the trace (its head group parsed): a still head would hide what head aim does.
                Add-QaResult $c 'trace: the head swept (its yaw over a range of 4 deg or more in the trace)' ($t.HeadRange -ge 4) ('{0:N1} deg' -f $t.HeadRange)
            }
            if ($c.Scenario.Pad) {
                # Not vacuous: the menu's own lines (10 or more before the sync) and the push applied by the pad.
                $padLog = Get-Content -LiteralPath (Join-Path $c.Dir 'pad.log') -Raw -ErrorAction SilentlyContinue
                $pushed = $padLog -and $padLog -match "state 'rx=24000'(?! \(update failed)"
                Add-QaResult $c 'trace: the view did not turn under the menu with the look stick pushed (the written yaw less the head''s within 1 deg over 10 menu lines or more; the push applied by the pad)' ($traced -and $t.MenuRows -ge 10 -and $pushed -and $t.MenuRange -le 1) ('{0:N2} deg over {1} menu line(s); the push {2}' -f $t.MenuRange, $t.MenuRows, $(if ($pushed) { 'applied' } else { 'not applied (pad.log)' }))
            }
            # The camera's own motion in the sync is at most 3 degrees a game frame at the rig's 60; the flick of its
            # first frame (9.6) and the game's turn of the player's angles as the sync ends (180 under Camera moves
            # before 2026-11-12) are not followed.
            if ($c.Scenario.Camera -eq 'play') {
                Add-QaResult $c 'trace: no turn of the view over 5 deg in one game frame, from the menu to 2 s after the sync' ($traced -and [Math]::Abs($t.Step) -le 5) "$tracedText; largest $('{0:N1}' -f $t.Step) ($($t.StepAt))"
            } else {
                # Still view shows a cut of the camera's heading once: the sync's start (or the menu's close, when the
                # menu stays over its first views) may turn the view to the animation's own heading (the rune's 6.7 to
                # 9.6 degrees when the player stood off it under head aim) in one step over 5 degrees at most: a flick
                # shown would be two, out and back. The camera's own small steps there are followed (a few degrees
                # each way at the rune's start). The cut capped at 25 degrees but in gamepad mode, where the pad's
                # turns leave the player up to 60 off.
                $cap = if ($c.Scenario.Pad) { 180 } else { 25 }
                Add-QaResult $c "trace: no turn of the view over 5 deg in one game frame, from the menu to 2 s after the sync, but one cut at the sync's start ($cap deg at most)" ($traced -and $t.StepRest -le 5 -and $t.OpenBig -le 1 -and $t.OpenMax -le $cap) ("$tracedText; largest $('{0:N1}' -f $t.Step) ($($t.StepAt)); " + ('{0:N1} outside the opening (the sync''s first 5 views from {3} in); there {4} step(s) over 5 deg, the largest {5:N1}, the view turned {1:N1} over a path of {2:N1}' -f $t.StepRest, $t.OpenNet, $t.OpenPath, $t.OpenFrom, $t.OpenBig, $t.OpenMax))
            }
            if ($c.Scenario.Camera -eq 'play' -and $c.Scenario.Aim -ne 'view') {
                # Camera moves keeps the heading the player had (not under view aim, below).
                $back = if ($traced) { [Math]::Abs((($t.LastYaw - $t.MenuYaw) % 360 + 540) % 360 - 180) } else { 999 }
                Add-QaResult $c 'trace: the heading after the sync is the one in the menu (within 1 deg)' ($traced -and $back -le 1) ('{0:N2} deg off' -f $back)
            } else {
                # Still view keeps the camera's heading after (the menu's when the player faced the pickup's own): the
                # game's turn of the player's angles as the sync ends turns no view. So does view aim with either
                # setting: nothing writes the player's angles there, the view is built on the game's yaw.
                $back = if ($traced) { [Math]::Abs((($t.LastYaw - $t.SyncEndYaw) % 360 + 540) % 360 - 180) } else { 999 }
                $menuOff = if ($traced) { [Math]::Abs((($t.LastYaw - $t.MenuYaw) % 360 + 540) % 360 - 180) } else { 999 }
                Add-QaResult $c 'trace: the heading after the sync is the one at its end (within 1 deg)' ($traced -and $back -le 1) ('{0:N2} deg off ({1:N2} off the menu''s)' -f $back, $menuOff)
                if (-not $c.Scenario.Aim) {
                    # Under hand aim the snap turns square the player with the pickup's own heading: the menu's too.
                    Add-QaResult $c 'trace: the heading after the sync is the one in the menu too (within 1 deg; hand aim faces the pickup)' ($traced -and $menuOff -le 1) ('{0:N2} deg off' -f $menuOff)
                }
            }
            if ($c.Name -notlike '*-behind*') {
                # The crush is drawn in front of the game's camera: the view faces its heading (Still: follows it; Camera
                # moves: its motion on the heading the view had, the flick left out).
                # Still takes the camera's yaw itself (0.0 off on the rig): within 1 degree; Camera moves plays it a game
                # frame late (0.1 off).
                $on = if ($c.Scenario.Camera -eq 'play') { 3 } else { 1 }
                if ($c.Scenario.Aim) {
                    # Under head aim the view follows a camera from where the head was when it took over (Still: the
                    # camera's heading less that head yaw) or keeps the player's own heading with the camera's motion on
                    # it (Camera moves; the player not facing the pickup's own heading exactly: a pad turns in steps of
                    # about 20 degrees): its gap from the camera's heading stays put through the sync after its opening.
                    # With Still view the gap is the head's turn since the player's view angles last took head aim's
                    # write: with a pad, Use makes the game put back an older write until the menu is up (camera-
                    # animations.md, the pad's two-step turn), so that can be from before the menu; within the
                    # sweep's whole range, one side to the other (14.5 degrees on the rig, tl-f1).
                    $gapCap = if ($c.Scenario.Camera -eq 'play') { 180 } else { 2 * $script:QaHeadSwayYaw + 2 }
                    Add-QaResult $c "trace: the view keeps one gap from the camera's heading in the sync after its opening ($gapCap deg at most; each line within $on deg of it as a median, 15 at most)" ($traced -and [Math]::Abs($t.GapRef) -le $gapCap -and $t.SpreadMedian -le $on -and $t.SpreadMax -le 15) ('gap {0:N1}; lines off it by {1:N1} (median) to {2:N1}' -f $t.GapRef, $t.SpreadMedian, $t.SpreadMax)
                } else {
                    Add-QaResult $c "trace: the view faces the camera's heading in the sync (median within $on deg, all within 15)" ($traced -and $t.GapMedian -le $on -and $t.GapMax -le 15) ('{0:N1} median, {1:N1} at most' -f $t.GapMedian, $t.GapMax)
                }
            }
            if ($c.Scenario.Camera -eq 'play') {
                $held = if ($c.Scenario.Aim -eq 'view') { 'no heading held' } else { 'the view keeps the body yaw' }
                Test-QaPresent $c "pickup: camera turn \d+ starts .*; $held" -Regex
                if ($c.Name -notlike '*-behind*') {
                    # The camera's tilt played (the crystal's and the rune's pitch about 12 degrees, their roll 6 to 10
                    # degrees of range), its cut of 30 to 40 degrees of pitch as the sync starts left out. No step of the
                    # view over 1.25 times the camera's own largest (its motion, not its cuts: 4.47 at the rune at about 60
                    # game frames a second, twice that at 30) or the release's steepest (the turn left as the pickup ends
                    # eased out over 0.3 s): bounds that follow the frame rate.
                    $left = [regex]::Match($text, 'pickup: camera turn \d+ ends after .*?; (-?[\d.]+) deg of pitch left eases out')
                    $release = if ($left.Success) { 1.5 * [Math]::Abs([double]$left.Groups[1].Value) * $t.FrameSeconds / $script:QaCameraReleaseSeconds } else { 0.0 }
                    $bound = 1.25 * [Math]::Max($t.GameStep, $release)
                    Add-QaResult $c 'trace: the camera''s pitch and roll played (a range of 4 deg or more), no step of the view over 1.25 times the camera''s own or its release''s' ($traced -and $t.TiltRange -ge 4 -and $t.GameStep -gt 0 -and $t.StepAll -le $bound) ('range {0:N1}, largest step {1:N2} against {2:N2} (the camera''s largest step {3:N2}, the release''s {4:N2}, {5:N1} ms a game view)' -f $t.TiltRange, $t.StepAll, $bound, $t.GameStep, $release, (1000 * $t.FrameSeconds))
                }
            } elseif ($c.Name -notlike '*-behind*') {
                # Facing: what the crush is drawn in front of. With the head looking away (-behind) not checked.
                Add-QaResult $c 'trace: no pitch or roll of the camera written in the sync (within 0.5 deg of the head''s)' ($traced -and $t.Tilt -le 0.5) ('{0:N2} at most' -f $t.Tilt)
            }
            $xr = @([regex]::Matches($text, $script:QaMenuXrLine))
            if ($xr.Count -ge 2) {
                $a = $xr[$xr.Count - 2].Groups; $b = $xr[$xr.Count - 1].Groups
                $tracked = [long]$b[2].Value - [long]$a[2].Value
                $screen = [long]$b[3].Value - [long]$a[3].Value
                Add-QaResult $c 'head-tracked again after the sync (the last 10 s xr window)' ($tracked -gt 0 -and $screen * 20 -le $tracked) "$tracked head-tracked, $screen on the screen"
            } else {
                Add-QaResult $c 'head-tracked again after the sync (the last 10 s xr window)' $false "$($xr.Count) xr line(s)"
            }
        }
    }
}

$crystalProves = 'A Sentinel Crystal in a campaign load (Game Slot 2''s Continue Game, which must hold a save of Exultia from before its crystal: it depends on the test PC''s own slot 2): Use opens its upgrade menu as a popup over the game, a held trigger unlocks an upgrade, and the crystal''s sync plays; the sync''s length, its forced views and the view through the menu, the sync and after are measured'
$crystalFacing = @('ETERNALVR_TURN=snap', 'ETERNALVR_SNAP_DEGREES=90')
$menuScenarios += @(
    (New-QaCrystalScenario 'menu-crystal' "$crystalProves (turned to face the crystal; Pickup animations Still view)" $crystalFacing),
    (New-QaCrystalScenario 'menu-crystal-play' "$crystalProves (turned to face the crystal; Camera moves)" ($crystalFacing + 'ETERNALVR_PICKUP_CAMERA=play')),
    (New-QaCrystalScenario 'menu-crystal-behind' "$crystalProves (the hand points at the crystal, the head looks away; Still view)" @()),
    (New-QaCrystalScenario 'menu-crystal-behind-play' "$crystalProves (the hand points at the crystal, the head looks away; Camera moves)" @('ETERNALVR_PICKUP_CAMERA=play')),
    # Measurements for Still's hold: whether the player's view angles move in the sync when the hand sweeps fast and
    # the stick snap-turns, or when the head sways fast (10 degrees each way every 0.2 s, up to 314 degrees a second).
    (New-QaCrystalScenario 'menu-crystal-turns' "$crystalProves (turned to face the crystal; Still view; a fast hand and two snap turns in the sync)" $crystalFacing),
    (New-QaCrystalScenario 'menu-crystal-sway' "$crystalProves (turned to face the crystal; Still view; the head swaying fast throughout)" ($crystalFacing + 'ETERNALVR_TEST_HEAD_SWAY=10,0,0.2'))
)
$runeProves = 'A rune in a campaign load (Game Slot 2''s Continue Game, Exultia from before its rune: it depends on the test PC''s own slot 2): Use opens CHOOSE A RUNE as a popup over the game, a held trigger on a rune unlocks it, and the rune''s sync plays; its length, its forced views and the view through the menu, the sync and after are measured'
$menuScenarios += @(
    (New-QaCrystalScenario 'menu-rune' "$runeProves (turned to face the rune; Pickup animations Still view)" $crystalFacing 'rune'),
    (New-QaCrystalScenario 'menu-rune-play' "$runeProves (turned to face the rune; Camera moves)" ($crystalFacing + 'ETERNALVR_PICKUP_CAMERA=play') 'rune')
)
$modBotProves = 'A mod bot in a campaign load (Game Slot 2''s Continue Game, Exultia from before its mod bot: it depends on the test PC''s own slot 2), the player placed in front of the drone: Use opens CHOOSE A WEAPON MOD as a popup over the game, a held trigger on a mod unlocks it, and the mod bot''s sync plays; its length, its forced views and the view through the menu, the sync and after are measured'
$menuScenarios += @(
    (New-QaCrystalScenario 'menu-modbot' "$modBotProves (Pickup animations Still view)" $crystalFacing 'modbot'),
    (New-QaCrystalScenario 'menu-modbot-play' "$modBotProves (Camera moves)" ($crystalFacing + 'ETERNALVR_PICKUP_CAMERA=play') 'modbot')
)

# The six facing scenarios under head aim, in gamepad mode and under view aim (qa-scenarios-pickup.ps1
# New-QaAimVariant: <name>-head, <name>-pad, <name>-view, by name only; the head sweeping): in gamepad mode Use is the
# pad's right stick click, the look stick turns the player to the pickup, and the menu is the game's own pad navigation
# (no cursor: QaPadMenuLine, the backdrop or, for the mod bot's, the weapon's 3D model).
$facingNames = 'menu-crystal', 'menu-crystal-play', 'menu-rune', 'menu-rune-play', 'menu-modbot', 'menu-modbot-play'
$facing = @($menuScenarios | Where-Object { $facingNames -contains $_.Name })
$menuScenarios += @(foreach ($aim in 'head', 'pad', 'view') { foreach ($s in $facing) { New-QaAimVariant $s $aim } })

# The short pause menu of the first mission (Load Checkpoint, Exit; docs/release/KNOWN-ISSUES.md, Menus): a
# measurement of what B and the Menu button do there, for a look at the shots and the notes.
$menuScenarios += @(
    @{
        Name = 'pause-first-mission'; Kind = 'game'; OnlyByName = $true
        Proves = 'The pause menu in the first mission (e1m1''s start): the left Menu button opens it (controllers: action pause), then B and then the Menu button again are pressed with the menu up; notes say whether the game took gameplay input back after each (the menu closed), shots of the menu and after each press'
        Map = 'game/sp/e1m1_intro/e1m1_intro'
        Env = @()
        Input = @()
        Timeline = {
            param($c)
            Wait-QaSeconds $c 12
            $press = {
                param($what, $lines)
                $mark = Get-QaLogMark $c
                Write-QaInput $c $lines; Wait-QaSeconds $c 0.2
                Write-QaInput $c @(); Wait-QaSeconds $c 2.5
                $back = Wait-QaLogAfter $c $mark 'gameplay input back on' 0.5
                Add-QaNote $c "$what`: $(if ($back) { 'the game took gameplay input back (the menu closed)' } else { 'no gameplay input back on line (the menu stayed up)' })"
                Save-QaGameWindowShot $c $what; Save-QaXrShot $c $what
            }
            & $press 'menu-opened' @('left.menu = 1')
            & $press 'after-b' @('right.secondary = 1')
            & $press 'after-menu' @('left.menu = 1')
        }
        Asserts = {
            param($c)
            Test-QaPresent $c 'controllers: action pause'
        }
    },
    # The late Fortress of Doom (Game Slot 1's Continue Game with the 100% save) for a look at its known rough spots,
    # and the dais steps that went dark and bright (docs/rig-findings/umbra-lights.md: the dark spot 0.5 21.67 and the
    # bright one 0.5 22.44, view z about 4.2): shots at both, then a full turn in 45-degree snap turns at the dais.
    @{
        Name = 'fortress-look'; Kind = 'game'; OnlyByName = $true
        MenuMission = 'continue'; MenuSlot = 1; MenuMap = 'game/hub/hub'
        MenuSaveNote = 'the 100% save (a finished campaign: Continue Game loads the late Fortress of Doom)'
        Proves = 'The late Fortress of Doom from Game Slot 1''s Continue Game: light-cull on, XR shots on the dais steps'' dark and bright spots (umbra-lights.md; the notes give each shot''s mean brightness), and eight shots of a full turn at the dais, for a look'
        Env = @('ETERNALVR_TURN=snap', 'ETERNALVR_SNAP_DEGREES=45',
                'ETERNALVR_DEBUG_COMMANDS=5:g_setting_hud_auto_dismiss_tutorials 1;g_setting_tutorials 0|8:setviewpos 0.5 21.67 5.0 90|16:setviewpos 0.5 22.44 5.0 90|24:setviewpos 0.4 25.0 5.5 270')
        Input = @()
        Timeline = {
            param($c)
            foreach ($s in @(@('dark', '0.5 21.67'), @('bright', '0.5 22.44'), @('dais', '0.4 25.0'))) {
                if (-not (Wait-QaLog $c "debug-commands: [\d.]+ s: setviewpos $([regex]::Escape($s[1]))" 40)) { Add-QaNote $c "setviewpos $($s[1]) never ran"; return }
                Wait-QaSeconds $c 3
                Save-QaXrShot $c "steps-$($s[0])"
            }
            for ($k = 1; $k -le 7; $k++) {
                Write-QaInput $c @('right.stick = 1, 0'); Wait-QaSeconds $c 0.3
                Write-QaInput $c @(); Wait-QaSeconds $c 1.5
                Save-QaXrShot $c "turn-$($k * 45)"
            }
        }
        Asserts = {
            param($c)
            Test-QaPresent $c 'light-cull: on by default'
            foreach ($n in 'dark', 'bright') {
                $f = Get-ChildItem -LiteralPath $c.Out -Filter "fortress-look-steps-$n-xr-*.png" -ErrorAction SilentlyContinue | Select-Object -First 1
                if (-not $f) { Add-QaNote $c "steps-$n`: no XR shot"; continue }
                Add-Type -AssemblyName System.Drawing
                $bmp = [System.Drawing.Bitmap]::FromFile($f.FullName)
                try {
                    # The lower half of the shot (the steps and the floor), every 4th pixel.
                    $sum = 0.0; $count = 0
                    for ($y = [int]($bmp.Height / 2); $y -lt $bmp.Height; $y += 4) {
                        for ($x = 0; $x -lt $bmp.Width; $x += 4) { $p = $bmp.GetPixel($x, $y); $sum += ($p.R + $p.G + $p.B) / 3.0; $count++ }
                    }
                    Add-QaNote $c ("steps-{0}: mean brightness of the lower half {1:N1} ({2})" -f $n, ($sum / [math]::Max(1, $count)), $f.Name)
                } finally { $bmp.Dispose() }
            }
        }
    }
)

# First, before every other scenario (see above).
$script:QaScenarios = $menuScenarios + @($script:QaScenarios)
