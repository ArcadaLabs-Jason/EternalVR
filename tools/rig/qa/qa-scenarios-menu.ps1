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
#                 on the camera's heading through the sync (the crush in front), the menu's heading after it
#   menu-crystal-play, menu-crystal-behind, menu-crystal-behind-play  the same with Camera moves, and with only the
#                 hand pointed at the crystal behind the player (the head looks away; the crush is behind the view)
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
$script:QaMenuUnderLine = 'aim: under the menu the view faces the camera the game drives \(body yaw (-?[\d.]+), held (-?[\d.]+); game view yaw (-?[\d.]+)\)'
# The trace line (camera_anim_hook.cpp traceView) with what head aim made of the view: `sync trace` for a view of the
# sync or the menu outside a forced view's window.
$script:QaCrystalTrace = '(?m)camera: (?:sync )?trace (\d+): game \((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\) angles \((?:(-?[\d.]+) (-?[\d.]+)|none)\) written \((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\) eye \([^)]*\)( forced)?; body (-?[\d.]+) head \((-?[\d.]+) (-?[\d.]+)\) held (\S+?)( menu)?( sync)?\r?$'

# Use at the crystal until its upgrade menu comes up; $true when it did (a note says how).
function Invoke-QaCrystalUse($c, [bool]$Facing) {
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
            if (Wait-QaLogAfter $c $mark 'menu: the game shows its cursor \(a popup over the game' 2.5) {
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

# Over the trace lines from the last menu before the sync to the end: the written yaw in the menu (its first line),
# the sync's lines (the written yaw's gap from the game camera's: the crush is drawn in front of that camera), the
# largest change of the written yaw between two consecutive game views and where it was, and the written yaw on the
# last line (the heading after).
function Get-QaCrystalTrace($Ctx) {
    $rows = @(foreach ($m in [regex]::Matches((Get-QaLogText $Ctx), $script:QaCrystalTrace)) {
        $g = $m.Groups
        [pscustomobject]@{ N = [long]$g[1].Value; GamePitch = [double]$g[2].Value; GameYaw = [double]$g[3].Value
            AnglesYaw = $(if ($g[6].Success) { [double]$g[6].Value } else { $null })
            Pitch = [double]$g[7].Value; Yaw = [double]$g[8].Value; Forced = $g[10].Success; Body = [double]$g[11].Value
            HeadYaw = [double]$g[13].Value; Held = $g[14].Value; Menu = $g[15].Success; Sync = $g[16].Success }
    })
    $wrap = { param($d) (($d % 360) + 540) % 360 - 180 }
    $r = [ordered]@{ Rows = $rows.Count; SyncRows = 0; MenuYaw = $null; GapMedian = 999.0; GapMax = 999.0; Step = 0.0
                     StepAt = ''; LastYaw = $null; LastGameYaw = $null; AnglesMoves = 0; AnglesLargest = 0.0 }
    $first = -1
    for ($i = 0; $i -lt $rows.Count; $i++) { if ($rows[$i].Sync) { $first = $i; break } }
    if ($first -lt 0) { return $r }
    $start = $first
    while ($start -gt 0 -and $rows[$start - 1].Menu -and $rows[$start - 1].N -eq $rows[$start].N - 1) { $start-- }
    $r.MenuYaw = $rows[$start].Yaw
    $gaps = @($rows | Where-Object { $_.Sync } | ForEach-Object { [Math]::Abs((& $wrap ($_.Yaw - $_.GameYaw))) } | Sort-Object)
    $r.SyncRows = $gaps.Count
    $r.GapMedian = $gaps[[int][Math]::Floor($gaps.Count / 2)]
    $r.GapMax = $gaps[-1]
    # The player's view angles in the sync: how often they changed from one game view to the next, the largest change.
    for ($i = $first + 1; $i -lt $rows.Count; $i++) {
        if (-not $rows[$i].Sync -or $rows[$i].N -ne $rows[$i - 1].N + 1 -or $null -eq $rows[$i].AnglesYaw -or $null -eq $rows[$i - 1].AnglesYaw) { continue }
        $d = [Math]::Abs((& $wrap ($rows[$i].AnglesYaw - $rows[$i - 1].AnglesYaw)))
        if ($d -gt 0.5) { $r.AnglesMoves++ }
        $r.AnglesLargest = [Math]::Max($r.AnglesLargest, $d)
    }
    for ($i = $start + 1; $i -lt $rows.Count; $i++) {
        if ($rows[$i].N -ne $rows[$i - 1].N + 1) { continue }
        $d = & $wrap ($rows[$i].Yaw - $rows[$i - 1].Yaw)
        if ([Math]::Abs($d) -gt [Math]::Abs($r.Step)) {
            $r.Step = $d
            $what = if ($rows[$i - 1].Menu -and -not $rows[$i].Menu) { 'as the menu closed' } elseif ($rows[$i].Sync -and -not $rows[$i - 1].Sync) { 'as the sync started' } elseif ($rows[$i - 1].Sync -and -not $rows[$i].Sync) { 'as the sync ended' } elseif ($rows[$i].Sync) { 'in the sync' } elseif ($rows[$i].Menu) { 'in the menu' } else { 'after the sync' }
            $r.StepAt = "trace $($rows[$i].N), $what"
        }
    }
    $r.LastYaw = $rows[-1].Yaw
    $r.LastGameYaw = $rows[-1].GameYaw
    return $r
}

function New-QaCrystalScenario([string]$Name, [string]$Proves, [string[]]$ExtraEnv) {
    return @{
        Name = $Name; Kind = 'game'; OnlyByName = $true
        MenuMission = 'continue'; MenuSlot = 2; MenuMap = 'game/sp/e1m2_battle/e1m2_battle'
        MenuSaveNote = 'the pre-crystal save this scenario needs (Exultia from before its Sentinel Crystal; the test PC''s slot 2 holds one)'
        Proves = $Proves
        Env = @('ETERNALVR_AIM=hand', 'ETERNALVR_TEST_CAMERA_TRACE=1', "ETERNALVR_DEBUG_COMMANDS=$script:QaCrystalSchedule") + $ExtraEnv
        Input = @()
        Timeline = {
            param($c)
            Wait-QaSeconds $c 7 # the teleport (5 s) has run
            if (-not (Invoke-QaCrystalUse $c ($c.Name -notlike '*-behind*'))) {
                Add-QaResult $c 'the crystal''s upgrade menu came up' $false 'no menu after Use at every aim tried: the slot''s save may be from after the crystal'
                Save-QaGameWindowShot $c 'no-menu'; return
            }
            Wait-QaSeconds $c 1
            Save-QaXrShot $c 'upgrade-menu'
            Save-QaGameWindowShot $c 'upgrade-menu'
            $aim = @(Get-QaMenuAim (Get-QaMenuPanel $c) $script:QaCrystalUpgrade[0] $script:QaCrystalUpgrade[1])
            $gone = $null
            for ($try = 1; $try -le 2 -and -not $gone; $try++) {
                $mark = Get-QaLogMark $c
                Write-QaInput $c $aim; Wait-QaSeconds $c 0.8
                Write-QaInput $c ($aim + 'right.trigger = 1'); Wait-QaSeconds $c 3.5
                Write-QaInput $c $aim
                $gone = Wait-QaLogAfter $c $mark 'menu: the cursor is gone' 2
            }
            Write-QaInput $c @()
            if (-not $gone) { Add-QaNote $c 'the upgrade menu stayed up after two held triggers'; Save-QaGameWindowShot $c 'menu-stays'; return }
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
            [void](Wait-QaLogAfter $c (Get-QaLogMark $c) 'SetViewAngles called from RVA 0xB4567C' 8)
            Wait-QaSeconds $c 2
            Save-QaXrShot $c 'after'
            Wait-QaSeconds $c 10 # a whole 10 s xr window after the sync
        }
        Asserts = {
            param($c)
            Test-QaPresent $c 'menu: the game shows its cursor (a popup over the game'
            Test-QaPresent $c "controllers: sync 'interact/argent_cell/use_sync'"
            $text = Get-QaLogText $c
            $sync = [regex]::Match($text, "\[\s*([\d.]+)\] \[\s*\d+\] controllers: sync 'interact/argent_cell/use_sync'")
            $end = if ($sync.Success) { [regex]::Match($text.Substring($sync.Index), '\[\s*([\d.]+)\] \[\s*\d+\] controllers: SetViewAngles called from RVA 0xB4567C') } else { $null }
            if ($sync.Success -and $end -and $end.Success) {
                $t0 = [double]::Parse($sync.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture)
                $t1 = [double]::Parse($end.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture)
                $inside = @([regex]::Matches($text.Substring($sync.Index, $end.Index), 'controllers: forced view starts')).Count
                Add-QaNote $c ('the sync ran {0:N2} s to the game''s SetViewAngles (RVA 0xB4567C); {1} forced view(s) began in it' -f ($t1 - $t0), $inside)
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
            $traced = $t.SyncRows -ge 100
            $holds = if ($sync.Success) { @([regex]::Matches($text.Substring($sync.Index), "pickup: a pickup's camera jumped")).Count } else { 0 }
            Add-QaNote $c ('the player''s view angles in the sync: changed in {0} game view(s), by up to {1:N2} deg; {2} hold(s) of the camera''s heading started after the sync began' -f $t.AnglesMoves, $t.AnglesLargest, $holds)
            if ($c.Name -like '*-turns' -or $c.Name -like '*-sway') {
                # Measurements: the hand, the stick or the head move on purpose; the checks below are of the view.
                Add-QaResult $c 'trace: the sync traced (100 lines or more)' $traced "$($t.SyncRows) sync line(s)"
                return
            }
            # The camera's own motion in the sync is at most 3 degrees a game frame at the rig's 60; the flick of its
            # first frame (9.6) and the game's turn of the player's angles as the sync ends (180 under Camera moves
            # before 2026-11-12) are not followed.
            Add-QaResult $c 'trace: no turn of the view over 5 deg in one game frame, from the menu to 2 s after the sync' ($traced -and [Math]::Abs($t.Step) -le 5) "$($t.SyncRows) sync line(s); largest $('{0:N1}' -f $t.Step) ($($t.StepAt))"
            $back = if ($traced) { [Math]::Abs((($t.LastYaw - $t.MenuYaw) % 360 + 540) % 360 - 180) } else { 999 }
            Add-QaResult $c 'trace: the heading after the sync is the one in the menu (within 1 deg)' ($traced -and $back -le 1) ('{0:N2} deg off' -f $back)
            if ($c.Name -notlike '*-behind*') {
                # The crush is drawn in front of the game's camera: the view faces its heading (Still: follows it; Camera
                # moves: its motion on the heading the view had, the flick left out).
                Add-QaResult $c 'trace: the view faces the camera''s heading in the sync (median within 3 deg, all within 15)' ($traced -and $t.GapMedian -le 3 -and $t.GapMax -le 15) ('{0:N1} median, {1:N1} at most' -f $t.GapMedian, $t.GapMax)
            }
            if ($c.Name -like '*-play') {
                Test-QaPresent $c 'pickup: camera turn \d+ starts .*; the view keeps the body yaw' -Regex
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

# First, before every other scenario (see above).
$script:QaScenarios = $menuScenarios + @($script:QaScenarios)
