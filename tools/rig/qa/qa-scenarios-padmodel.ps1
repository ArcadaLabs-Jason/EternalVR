# Gamepad mode and the screens that place a weapon's 3D model (docs/rig-findings/camera-animations.md section 9): in
# gamepad mode a menu's 3D model placed in the last 0.25 s (menu_model::modelShown, the idMenuWidget_3D_Stand hook)
# counts as a menu over the game, as the mod bot's weapon mod screen needs, so head aim holds the body under it. These
# check that no other screen or moment holds the body past its own end: the Dossier's screens, the pause menu, a walk
# through the Fortress of Doom, and combat with weapon switches and the weapon wheel. All in gamepad mode (the virtual
# pad, README "Gamepad mode scenarios"; ETERNALVR_AIM unset, so head aim as a player with the controllers off gets
# it), with ETERNALVR_TEST_CAMERA_TRACE=all (every head-tracked game view: the written view, the head, the body and
# the menu flag), the simulator's head still. By name only. Dot-sourced by qa-suite.ps1 after qa-scenarios-menu.ps1.
#
# A look-stick probe is the pad's look stick pushed (rx 24000 for 0.4 s) and 1.2 s more: the written yaw less the
# head's over that span (the head is still: the written yaw) is how far the push turned the view, about 102 degrees a
# second of push on the rig (the push lasts 0.40 to 0.49 s through the harness: its length comes from pad.log).
#
#   pad-model-screens   Game Slot 1's Continue Game by the pad (the 100% save: the Fortress, game/hub/hub, every
#                       weapon and its mods). Two probes in play first (what a push turns: the expectation), then one
#                       screen at a time: the pause menu (Start), the Dossier (View) on its map, its Arsenal (the
#                       right shoulder), the Arsenal's CUSTOMIZE WEAPON (X: the weapon's model and its skins), more
#                       weapons (the right trigger) and a weapon's upgrades (A), SUIT (the Praetor Suit and the Sentinel
#                       Crystals), RUNES, and a CODEX entry, each closed by B (B again, up to three times, while the
#                       menu line is still up: a press that only left a page). Checked: a 3D model line under one of
#                       them at least (the Arsenal's weapon), and after every close `menu: the menu without a cursor is
#                       gone` and no 3D model placed within 1.5 s of the last press, no trace line with the menu flag
#                       from 1 s after it, and a probe that turns the view as the play probes' rate gives for its push
#                       (within a tenth, 1 deg at least; Get-QaPmPushSeconds). Under every screen, the Dossier's map
#                       and CODEX pages included (no backdrop the layer sees and no model there: the Dossier's own open
#                       and close tell it, dossier_screen_hook.hpp), a probe turns nothing (within 1 deg over 10 lines
#                       with the menu flag or more), and from the close's first press to 1 s after the last no frame
#                       turns the view over 1 deg (before the Dossier's rule the body pushed under the map and CODEX
#                       pages came back 43 and 45 degrees in one frame as it closed). Each Dossier screen logs the
#                       Dossier's open and close. Notes per screen: its menu and model lines
#   pad-model-fortress  The same save's Fortress: walking with the move stick and turning with the look stick for
#                       about 50 s (the bridge, the mission select dais and the consoles round it, by the schedule's
#                       teleports; nothing opened): no menu line without a cursor and no 3D model line at all, no
#                       trace line with the menu flag, and every one of the walk's probes turns the view
#   pad-model-combat    e1m1's barge encounter (the glory scenarios' schedule, qa-scenarios-takeover.ps1) with three
#                       more weapons given: fire, weapon switches by the right shoulder's tap and picks from the
#                       weapon wheel (the right shoulder held, the look stick to a slot), probes between them: no 3D
#                       model line, no menu line without a cursor, every probe turns the view
#   pad-dossier-close   The same save's Fortress: the Dossier (View) closed each way a pad closes it, one step each: B;
#                       Start inside it (nothing: the Dossier stays open and held; rig e1), then B; B and the pause
#                       menu's Resume; B and at once the pause menu's Load Checkpoint (a map load right after the close;
#                       its NOTICE opens on Cancel: Left, A); B and Exit to Main Menu. Checked: one `menu: the Dossier
#                       opens` in each step and none elsewhere (not at the start, at HUD set-up or at the map loads), its
#                       `menu: the Dossier is closed` within 1 s of the B and never `(a map load ended it)`, a probe
#                       under it turning nothing (in the Start step after the Start), and in each step that goes back to
#                       play a probe after it turning the view as in play

$script:QaPmPush = 'rx=24000'
$script:QaPmPushSeconds = 0.4
$script:QaPmAfterPush = 1.2
$script:QaPmSave = 'the 100% save (the Fortress of Doom after the campaign)'
$script:QaPmModelLine = "menu: a menu's 3D model is placed every tick"
$script:QaPmModelGone = "menu: no menu's 3D model placed any more"
$script:QaPmMenuGone = 'menu: the menu without a cursor is gone'
$script:QaPmTrace = '^\[\s*([\d.]+)\] \[\s*\d+\] camera: (?:sync )?trace \d+: game \([^)]*\) angles \([^)]*\) written \((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\) eye \([^)]*\)(?: forced)?; body (-?[\d.]+) head \((-?[\d.]+) (-?[\d.]+)\) held \S+?( menu)?( sync)?\r?$'

function Get-QaPmWrap([double]$Degrees) { return (($Degrees % 360) + 540) % 360 - 180 }

# The layer log's clock now: the seconds of its last line (with the trace on, a line every game view).
function Get-QaPmNow($Ctx) {
    $m = [regex]::Matches((Get-QaLogText $Ctx), '(?m)^\[\s*([\d.]+)\]')
    if ($m.Count -eq 0) { return 0.0 }
    return [double]$m[$m.Count - 1].Groups[1].Value
}

# The trace lines as objects: T (log seconds), View (the written yaw less the head's), Body, Menu.
function Get-QaPmTrace($Ctx) {
    $rows = New-Object System.Collections.ArrayList
    foreach ($m in [regex]::Matches((Get-QaLogText $Ctx), $script:QaPmTrace, 'Multiline')) {
        $g = $m.Groups
        [void]$rows.Add([pscustomobject]@{ T = [double]$g[1].Value; View = Get-QaPmWrap ([double]$g[3].Value - [double]$g[7].Value)
            Body = [double]$g[5].Value; Menu = $g[8].Success })
    }
    return $rows
}

# A probe: the look stick pushed, then still; its span (log seconds) kept in the context's PmProbes under $Name.
function Invoke-QaPmProbe($c, [string]$Name, [string]$Kind) {
    if (-not $c.PmProbes) { $c.PmProbes = New-Object System.Collections.ArrayList }
    $from = Get-QaPmNow $c
    Invoke-QaPadPress $c $script:QaPmPush $script:QaPmPushSeconds
    Wait-QaSeconds $c $script:QaPmAfterPush
    [void]$c.PmProbes.Add([pscustomobject]@{ Name = $Name; Kind = $Kind; From = $from; To = (Get-QaPmNow $c); Index = $c.PmProbes.Count })
}

# How far the view turned over a span: the sum of the written yaw's steps (less the head's), the lines, the lines with
# the menu flag.
function Measure-QaPmSpan($Rows, [double]$From, [double]$To) {
    $span = @($Rows | Where-Object { $_.T -ge $From -and $_.T -le $To })
    $turn = 0.0
    for ($i = 1; $i -lt $span.Count; $i++) { $turn += Get-QaPmWrap ($span[$i].View - $span[$i - 1].View) }
    return [pscustomobject]@{ Lines = $span.Count; Turn = $turn; Menu = @($span | Where-Object { $_.Menu }).Count }
}

# The largest turn of the written yaw (less the head's) from one trace line to the next over a span, and the lines.
function Measure-QaPmJump($Rows, [double]$From, [double]$To) {
    $span = @($Rows | Where-Object { $_.T -ge $From -and $_.T -le $To })
    $jump = 0.0
    for ($i = 1; $i -lt $span.Count; $i++) { $jump = [Math]::Max($jump, [Math]::Abs((Get-QaPmWrap ($span[$i].View - $span[$i - 1].View)))) }
    return [pscustomobject]@{ Lines = $span.Count; Jump = $jump }
}

# Whether the layer's menu without a cursor is up now (its lines so far: more comings than goings).
function Test-QaPmMenuUp($c) {
    $text = Get-QaLogText $c
    return ([regex]::Matches($text, 'menu: a menu over the game without a cursor')).Count -gt ([regex]::Matches($text, [regex]::Escape($script:QaPmMenuGone))).Count
}

# A screen: $Open pressed (a pad state, or several in turn), $Settle s for it to come up, shots (the headset's view
# shows the GUI on its quad; the game window only the 3D), then $Then (more presses inside it, optional), a probe
# under it, $Close pressed to close it (then B again, up to three times, while the menu line is still up: a press
# that only left a page of the Dossier, which the shot shows; a hold that outlives the screen stays up through them
# and fails the check) and a probe after. Kept in the context's PmScreens: its name, when it opened and closed (log
# seconds), the presses it took to close.
function Invoke-QaPmScreen($c, [string]$Name, [string[]]$Open, [double]$Settle, [string[]]$Close, [scriptblock]$Then) {
    if (-not $c.PmScreens) { $c.PmScreens = New-Object System.Collections.ArrayList }
    $opened = Get-QaPmNow $c
    foreach ($p in $Open) { Invoke-QaPadPress $c $p 0.2; Wait-QaSeconds $c 1.2 }
    Wait-QaSeconds $c $Settle
    Save-QaXrShot $c "$Name-up"
    Save-QaGameWindowShot $c "$Name-up"
    if ($Then) { & $Then $c }
    Invoke-QaPmProbe $c "$Name (under it)" 'under'
    # What the headset shows after the push (a turn under the page shows against the -up shot).
    Save-QaXrShot $c "$Name-pushed"
    $closing = Get-QaPmNow $c
    $presses = @($Close)
    foreach ($p in $Close) { Invoke-QaPadPress $c $p 0.2; Wait-QaSeconds $c 0.8 }
    for ($k = 0; $k -lt 3 -and (Test-QaPmMenuUp $c); $k++) {
        Save-QaXrShot $c "$Name-still-up-$k"
        Invoke-QaPadPress $c 'b' 0.2; Wait-QaSeconds $c 0.8; $presses += 'b'
    }
    $closed = Get-QaPmNow $c
    Wait-QaSeconds $c 1
    Save-QaXrShot $c "$Name-closed"
    Invoke-QaPmProbe $c "$Name (after it)" 'after'
    [void]$c.PmScreens.Add([pscustomobject]@{ Name = $Name; Opened = $opened; Closing = $closing; Closed = $closed; Presses = ($presses -join ', ') })
}

# How long each push of the look stick lasted (s), in order, from the pad's log (qa-pad.py's applied states: the push
# state, then the next): the harness's own timing varies (0.41 to 0.49 s for 0.4 s on the rig), and the turn with it.
function Get-QaPmPushSeconds($c) {
    $out = New-Object System.Collections.ArrayList
    $lines = @(Get-Content -LiteralPath (Join-Path $c.Dir 'pad.log') -ErrorAction SilentlyContinue | Where-Object { $_ -match "^(\d\d):(\d\d):(\d\d\.\d+) state '" })
    for ($i = 0; $i -lt $lines.Count - 1; $i++) {
        if ($lines[$i] -notmatch "^(\d\d):(\d\d):(\d\d\.\d+) state '$([regex]::Escape($script:QaPmPush))'$") { continue }
        $t0 = 3600 * [double]$Matches[1] + 60 * [double]$Matches[2] + [double]$Matches[3]
        if ($lines[$i + 1] -notmatch '^(\d\d):(\d\d):(\d\d\.\d+) ') { continue }
        $t1 = 3600 * [double]$Matches[1] + 60 * [double]$Matches[2] + [double]$Matches[3]
        [void]$out.Add($t1 - $t0)
    }
    return $out
}

# A probe's turn and the push's length (its pad state's, or the asked 0.4 s without the pad's log).
function Measure-QaPmProbe($c, $Rows, $Probe, $Pushes) {
    $m = Measure-QaPmSpan $Rows $Probe.From $Probe.To
    $seconds = if ($Probe.Index -lt @($Pushes).Count) { $Pushes[$Probe.Index] } else { $script:QaPmPushSeconds }
    return [pscustomobject]@{ Lines = $m.Lines; Turn = $m.Turn; Menu = $m.Menu; Seconds = $seconds }
}

# The probes of $Kind: each turned the view as a play probe does (play: over 3 degrees, no menu flag; after: within a
# tenth of what the play probes' rate gives for this push's length, 1 degree at least, no menu flag), or (under) not at
# all.
function Test-QaPmProbes($c, $Rows, [string]$Kind, [double]$Rate) {
    $pushes = Get-QaPmPushSeconds $c
    $probes = @($c.PmProbes | Where-Object { $_.Kind -eq $Kind })
    $text = @(); $bad = 0
    foreach ($p in $probes) {
        $m = Measure-QaPmProbe $c $Rows $p $pushes
        $expect = $Rate * $m.Seconds
        $ok = switch ($Kind) {
            'play' { $m.Lines -ge 20 -and [Math]::Abs($m.Turn) -gt 3 -and $m.Menu -eq 0 }
            'under' { $m.Menu -ge 10 -and [Math]::Abs($m.Turn) -le 1 }
            'after' { $m.Lines -ge 20 -and $m.Menu -eq 0 -and [Math]::Abs([Math]::Abs($m.Turn) - $expect) -le [Math]::Max(1.0, 0.1 * $expect) }
        }
        if (-not $ok) { $bad++ }
        $text += ('{0}: {1:N2} deg over {2} line(s) for a {3:N3} s push{5}, {4} with the menu flag{6}' -f $p.Name, $m.Turn, $m.Lines, $m.Seconds, $m.Menu,
            $(if ($Kind -eq 'after') { ' ({0:N2} expected)' -f $expect } else { '' }), $(if ($ok) { '' } else { ' (off)' }))
    }
    return [pscustomobject]@{ Count = $probes.Count; Bad = $bad; Text = ($text -join '; ') }
}

# The play probes' turn rate (degrees per second of the push: the expectation) and their spread.
function Get-QaPmExpect($c, $Rows) {
    $pushes = Get-QaPmPushSeconds $c
    $rates = @($c.PmProbes | Where-Object { $_.Kind -eq 'play' } | ForEach-Object { $m = Measure-QaPmProbe $c $Rows $_ $pushes; [Math]::Abs($m.Turn) / $m.Seconds })
    if ($rates.Count -eq 0) { return [pscustomobject]@{ Mean = 0.0; Spread = 0.0; Count = 0 } }
    $s = $rates | Measure-Object -Average -Minimum -Maximum
    return [pscustomobject]@{ Mean = $s.Average; Spread = $s.Maximum - $s.Minimum; Count = $rates.Count }
}

# The lines between two log seconds that match $Pattern, with their seconds.
function Get-QaPmEvents($Ctx, [string]$Pattern) {
    $out = New-Object System.Collections.ArrayList
    foreach ($m in [regex]::Matches((Get-QaLogText $Ctx), '^\[\s*([\d.]+)\] \[\s*\d+\] .*?' + $Pattern, 'Multiline')) {
        [void]$out.Add([pscustomobject]@{ T = [double]$m.Groups[1].Value; Text = $m.Value })
    }
    return $out
}

$script:QaPmSchedule = '3:god;g_setting_hud_auto_dismiss_tutorials 1;g_setting_tutorials 0'

# The lines matching $Pattern so far, and a wait (s) for there to be $Count of them.
function Get-QaPmCount($Ctx, [string]$Pattern) { return @(Get-QaPmEvents $Ctx $Pattern).Count }
function Wait-QaPmCount($Ctx, [string]$Pattern, [int]$Count, [double]$Timeout) {
    $end = (Get-Date).AddSeconds($Timeout)
    while ((Get-Date) -lt $end) {
        if ((Get-QaPmCount $Ctx $Pattern) -ge $Count) { return $true }
        Wait-QaSeconds $Ctx 0.5
    }
    return (Get-QaPmCount $Ctx $Pattern) -ge $Count
}

$script:QaPmLoadLine = "mp guard: map load '"

# A Dossier close step (pad-dossier-close): View opens the Dossier, then $Before (pad states inside it, each with a
# shot), a probe under it, B (the closing press), then $Then (the pause menu's choice). With $Load the step ends in a
# map load (waited for); else B while the pad's menu line is up, and a probe after it. Kept in the context's PmDossier:
# the log seconds of the open, the B and the end.
function Invoke-QaPmDossierClose($c, [string]$Name, [string[]]$Before, [string[]]$Then, [bool]$Load) {
    if (-not $c.PmDossier) { $c.PmDossier = New-Object System.Collections.ArrayList }
    $loads = Get-QaPmCount $c $script:QaPmLoadLine
    $opened = Get-QaPmNow $c
    Invoke-QaPadPress $c 'back' 0.2
    Wait-QaSeconds $c 2
    Save-QaXrShot $c "$Name-up"
    foreach ($p in $Before) { Invoke-QaPadPress $c $p 0.2; Wait-QaSeconds $c 1.5; Save-QaXrShot $c "$Name-$p" }
    Invoke-QaPmProbe $c "$Name (under it)" 'under'
    $press = Get-QaPmNow $c
    Invoke-QaPadPress $c 'b' 0.2
    Wait-QaSeconds $c 1.0
    Save-QaXrShot $c "$Name-closed"
    foreach ($p in $Then) { Invoke-QaPadPress $c $p 0.2; Wait-QaSeconds $c 1.0 }
    if ($Then) { Save-QaXrShot $c "$Name-then" }
    $loaded = $false
    if ($Load) {
        $loaded = Wait-QaPmCount $c $script:QaPmLoadLine ($loads + 1) 15
        # Past the load's fade in and PadMenuWatch's wait after a load.
        Wait-QaSeconds $c 12
        Save-QaXrShot $c "$Name-loaded"
    } else {
        for ($k = 0; $k -lt 3 -and (Test-QaPmMenuUp $c); $k++) { Invoke-QaPadPress $c 'b' 0.2; Wait-QaSeconds $c 0.8 }
        Wait-QaSeconds $c 1.5
        Invoke-QaPmProbe $c "$Name (after it)" 'after'
    }
    [void]$c.PmDossier.Add([pscustomobject]@{ Name = $Name; Opened = $opened; Press = $press; End = (Get-QaPmNow $c); Load = $Load; Loaded = $loaded })
}

$padModelScenarios = @(
    @{
        Name = 'pad-model-screens'; Kind = 'game'; OnlyByName = $true; Pad = $true
        MenuMission = 'continue'; MenuSlot = 1; MenuMap = 'game/hub/hub'; MenuSaveNote = $script:QaPmSave
        Proves = 'Gamepad mode: the pause menu and the Dossier''s screens (its map, the Arsenal, the weapon customize screen, the Arsenal''s other lists, SUIT, RUNES, CODEX) hold the body while they are up, and only then; no turn as each closes; after each the look stick turns the view as in play'
        Env = @('ETERNALVR_CONTROLLERS=0', 'ETERNALVR_TEST_CAMERA_TRACE=all', "ETERNALVR_DEBUG_COMMANDS=$script:QaPmSchedule")
        Args = @('+in_joystick 1')
        Timeline = {
            param($c)
            Wait-QaSeconds $c 8
            Save-QaGameWindowShot $c 'in-map'
            Invoke-QaPmProbe $c 'play 1' 'play'
            Invoke-QaPmProbe $c 'play 2' 'play'
            # The pause menu (no Customize in it: the Slayer's skins are the main menu's).
            Invoke-QaPmScreen $c 'pause' @('start') 1.5 @('b') {
                param($c)
                foreach ($k in 1..4) { Invoke-QaPadPress $c 'down' 0.2; Wait-QaSeconds $c 0.6 }
            }
            # The Dossier's tabs: MAP, ARSENAL, SUIT, RUNES, CODEX, CHALLENGES (the right shoulder goes right).
            Invoke-QaPmScreen $c 'dossier-map' @('back') 2 @('b') $null
            Invoke-QaPmScreen $c 'arsenal' @('back', 'rb') 2.5 @('b') $null
            # X: CUSTOMIZE WEAPON, the weapon's skins with its model.
            Invoke-QaPmScreen $c 'customize' @('back', 'rb', 'x') 3 @('b', 'b') $null
            # The Arsenal's other weapons (each its own model) and a weapon's upgrades page.
            Invoke-QaPmScreen $c 'arsenal-weapons' @('back', 'rb') 2 @('b') {
                param($c)
                foreach ($k in 1..3) { Invoke-QaPadPress $c 'rt' 0.2; Wait-QaSeconds $c 1.2; Save-QaXrShot $c "arsenal-rt$k" }
                Invoke-QaPadPress $c 'a' 0.2; Wait-QaSeconds $c 1.5; Save-QaXrShot $c 'arsenal-upgrades'
            }
            # SUIT: the Praetor Suit's upgrades and the Sentinel Crystals.
            Invoke-QaPmScreen $c 'suit' @('back', 'rb', 'rb') 2 @('b') {
                param($c)
                Invoke-QaPadPress $c 'a' 0.2; Wait-QaSeconds $c 1.5; Save-QaXrShot $c 'suit-a'
            }
            Invoke-QaPmScreen $c 'runes' @('back', 'rb', 'rb', 'rb') 2 @('b') $null
            # CODEX: an entry shows a demon's or a weapon's model.
            Invoke-QaPmScreen $c 'codex' @('back', 'rb', 'rb', 'rb', 'rb') 2 @('b') {
                param($c)
                Invoke-QaPadPress $c 'a' 0.2; Wait-QaSeconds $c 2; Save-QaXrShot $c 'codex-entry'
            }
            Invoke-QaPmProbe $c 'play 3' 'play'
            Save-QaGameWindowShot $c 'end'
        }
        Asserts = {
            param($c)
            Test-QaPresent $c 'controllers: gamepad mode (motion controllers off)'
            $rows = Get-QaPmTrace $c
            Add-QaNote $c "trace: $($rows.Count) line(s)"
            $expect = Get-QaPmExpect $c $rows
            $play = Test-QaPmProbes $c $rows 'play' 0
            Add-QaResult $c 'play: every look-stick probe in play turned the view (over 3 deg, no menu flag)' ($play.Count -ge 3 -and $play.Bad -eq 0) ('{0:N1} deg/s of push, spread {1:N1}; {2}' -f $expect.Mean, $expect.Spread, $play.Text)
            $models = @(Get-QaPmEvents $c ([regex]::Escape($script:QaPmModelLine)))
            $modelsGone = @(Get-QaPmEvents $c ([regex]::Escape($script:QaPmModelGone)))
            $menus = @(Get-QaPmEvents $c 'menu: a menu over the game without a cursor')
            $menusGone = @(Get-QaPmEvents $c ([regex]::Escape($script:QaPmMenuGone)))
            $late = 0; $withModel = 0; $text = @()
            foreach ($s in @($c.PmScreens)) {
                $model = @($models | Where-Object { $_.T -ge $s.Opened -and $_.T -le $s.Closed }).Count
                $menu = @($menus | Where-Object { $_.T -ge $s.Opened -and $_.T -le $s.Closing }).Count
                # Gone: the menu line's end (or none up) by 1.5 s after the close; no model placed after it.
                $upAtClose = @($menus | Where-Object { $_.T -le $s.Closing }).Count -gt @($menusGone | Where-Object { $_.T -le $s.Closing }).Count
                $gone = @($menusGone | Where-Object { $_.T -ge $s.Closing -and $_.T -le $s.Closed + 1.5 }) | Select-Object -First 1
                $stillUp = @($menus | Where-Object { $_.T -le $s.Closed + 1.5 }).Count -gt @($menusGone | Where-Object { $_.T -le $s.Closed + 1.5 }).Count
                $modelUp = @($models | Where-Object { $_.T -le $s.Closed + 1.5 }).Count -gt @($modelsGone | Where-Object { $_.T -le $s.Closed + 1.5 }).Count
                $flag = (Measure-QaPmSpan $rows ($s.Closed + 1) ($s.Closed + 1 + $script:QaPmAfterPush)).Menu
                if ($stillUp -or $modelUp -or $flag -gt 0) { $late++ }
                if ($model) { $withModel++ }
                $text += ('{0}: menu line {1}, 3D model line {2}; up at the close {3} (closed by {7}); gone {4}; model still placed {5}; menu flag after {6}' -f $s.Name,
                    $(if ($menu) { 'yes' } else { 'no' }), $(if ($model) { 'yes' } else { 'no' }), $(if ($upAtClose) { 'yes' } else { 'no' }),
                    $(if ($gone) { '{0:N2} s after the close''s start' -f ($gone.T - $s.Closing) } elseif ($upAtClose) { 'NOT within 1.5 s' } else { '(not up)' }),
                    $(if ($modelUp) { 'YES' } else { 'no' }), $flag, $s.Presses)
            }
            Add-QaNote $c ("screens: " + ($text -join '; '))
            # Not vacuous: the Arsenal's weapon is a menu's 3D model.
            Add-QaResult $c 'screens: a 3D model line came under a screen (the Arsenal''s weapon)' ($withModel -ge 1) "$withModel screen(s)"
            Add-QaResult $c 'screens: no hold outlives its screen (the menu line gone and no 3D model placed 1.5 s after each close, no menu flag after)' (@($c.PmScreens).Count -ge 8 -and $late -eq 0) "$late of $(@($c.PmScreens).Count) screen(s) held past the close"
            $under = Test-QaPmProbes $c $rows 'under' 0
            Add-QaResult $c 'under each screen: the look stick turned nothing (within 1 deg over 10 lines with the menu flag or more), the Dossier''s map and CODEX pages included' ($under.Count -ge 8 -and $under.Bad -eq 0) $under.Text
            # The close: no frame turns the view (a body pushed under the screen coming back at once).
            $bad = 0; $text = @()
            foreach ($s in @($c.PmScreens)) {
                $j = Measure-QaPmJump $rows $s.Closing ($s.Closed + 1)
                if ($j.Lines -lt 20 -or $j.Jump -gt 1) { $bad++ }
                $text += ('{0}: {1:N2} deg at most over {2} line(s){3}' -f $s.Name, $j.Jump, $j.Lines, $(if ($j.Lines -lt 20 -or $j.Jump -gt 1) { ' (off)' } else { '' }))
            }
            Add-QaResult $c 'each close: no frame turned the view over 1 deg from the first press to 1 s after the last' (@($c.PmScreens).Count -ge 8 -and $bad -eq 0) ($text -join '; ')
            # The Dossier's own open and close (dossier_screen_hook.hpp): one open per Dossier screen.
            $opens = @(Get-QaPmEvents $c 'menu: the Dossier opens')
            $closes = @(Get-QaPmEvents $c 'menu: the Dossier is closed')
            $dossiers = @($c.PmScreens | Where-Object { $_.Name -ne 'pause' })
            $missing = @()
            foreach ($s in $dossiers) {
                $opened = @($opens | Where-Object { $_.T -ge $s.Opened -and $_.T -le $s.Closing }).Count
                $shut = @($closes | Where-Object { $_.T -ge $s.Closing -and $_.T -le $s.Closed + 1 }).Count
                if ($opened -ne 1 -or $shut -ne 1) { $missing += $s }
            }
            Add-QaResult $c 'the Dossier: its open logged under each Dossier screen and its close at each close, none elsewhere' ($dossiers.Count -ge 7 -and $missing.Count -eq 0 -and $opens.Count -eq $dossiers.Count -and $closes.Count -eq $dossiers.Count) ('{0} open line(s), {1} close line(s) for {2} Dossier screen(s){3}' -f $opens.Count, $closes.Count, $dossiers.Count, $(if ($missing.Count) { '; missing for ' + (($missing | ForEach-Object { $_.Name }) -join ', ') } else { '' }))
            $after = Test-QaPmProbes $c $rows 'after' $expect.Mean
            Add-QaResult $c 'after each screen: the look stick turned the view as in play (within a tenth of the play probes'' rate for its push''s length, 1 deg at least)' ($after.Count -ge 8 -and $after.Bad -eq 0) $after.Text
        }
    },
    @{
        Name = 'pad-model-fortress'; Kind = 'game'; OnlyByName = $true; Pad = $true
        MenuMission = 'continue'; MenuSlot = 1; MenuMap = 'game/hub/hub'; MenuSaveNote = $script:QaPmSave
        Proves = 'Gamepad mode: a walk through the Fortress of Doom with the pad, past its consoles and displays without opening them, never holds the body: no menu without a cursor, no menu''s 3D model, the look stick turning the view all along'
        Env = @('ETERNALVR_CONTROLLERS=0', 'ETERNALVR_TEST_CAMERA_TRACE=all',
            "ETERNALVR_DEBUG_COMMANDS=$script:QaPmSchedule|22:teleport interact_hub_mission_select_1|36:teleport interact_hub_arc_console_1|50:teleport interact_hub_master_level_panel")
        Args = @('+in_joystick 1')
        Timeline = {
            param($c)
            Wait-QaSeconds $c 6
            Save-QaGameWindowShot $c 'in-map'
            Invoke-QaPmProbe $c 'start' 'play'
            # About 50 s: walk forward turning, strafe, back; a probe every leg; shots as it goes.
            $legs = @('ly=32767,rx=9000', 'ly=32767,rx=-9000', 'lx=32767', 'ly=-32767,rx=12000', 'ly=32767', 'lx=-32767,rx=-12000')
            for ($i = 0; $i -lt 12; $i++) {
                Invoke-QaPadPress $c $legs[$i % $legs.Count] 2.2
                Invoke-QaPmProbe $c "leg $($i + 1)" 'play'
                if ($i % 3 -eq 2) { Save-QaGameWindowShot $c "walk-$($i + 1)" }
            }
            Save-QaGameWindowShot $c 'end'
        }
        Asserts = {
            param($c)
            Test-QaPresent $c 'controllers: gamepad mode (motion controllers off)'
            Test-QaPresent $c 'debug-commands: 50.0 s: teleport interact_hub_master_level_panel'
            $rows = Get-QaPmTrace $c
            $inMap = Get-QaPmEvents $c "the schedule's clock starts"
            $from = if (@($inMap).Count) { $inMap[0].T + 6 } else { 0.0 }
            $span = @($rows | Where-Object { $_.T -ge $from })
            Add-QaResult $c 'walk: no trace line with the menu flag (the body never held)' ($span.Count -ge 1000 -and @($span | Where-Object { $_.Menu }).Count -eq 0) ('{0} line(s) from 6 s into the map, {1} with the menu flag' -f $span.Count, @($span | Where-Object { $_.Menu }).Count)
            Test-QaAbsent $c 'menu: a menu over the game without a cursor'
            Test-QaAbsent $c $script:QaPmModelLine
            Test-QaAbsent $c 'menu: the Dossier opens'
            $play = Test-QaPmProbes $c $rows 'play' 0
            $expect = Get-QaPmExpect $c $rows
            Add-QaResult $c 'walk: every look-stick probe turned the view (over 3 deg, no menu flag)' ($play.Count -ge 13 -and $play.Bad -eq 0) ('{0:N1} deg/s of push, spread {1:N1}; {2}' -f $expect.Mean, $expect.Spread, $play.Text)
        }
    },
    @{
        Name = 'pad-model-combat'; Kind = 'game'; OnlyByName = $true; Pad = $true
        Proves = 'Gamepad mode: combat with weapon switches and the weapon wheel never counts as a menu''s 3D model or a menu without a cursor; the look stick turns the view between them'
        Map = 'game/sp/e1m1_intro/e1m1_intro'
        Env = @('ETERNALVR_CONTROLLERS=0', 'ETERNALVR_TEST_CAMERA_TRACE=all', 'ETERNALVR_BHAPTICS=1',
            ("ETERNALVR_DEBUG_COMMANDS=" + ((@(($script:QaGloryWave -f 20, 21, 24, 25)) + '26:give weapon/player/plasma_rifle;give weapon/player/rocket_launcher;give weapon/player/heavy_rifle' +
                @($script:QaGloryTeleports[0..2] | ForEach-Object { $script:QaGloryTeleport -f $_ })) -join '|')))
        Args = @('+in_joystick 1')
        Input = @()
        Timeline = {
            param($c)
            if (-not (Wait-QaLog $c 'debug-commands: 26\.0 s: give' 90)) { Add-QaNote $c 'the schedule''s give at 26 s was not logged' }
            Wait-QaSeconds $c 2
            Save-QaGameWindowShot $c 'in-combat'
            Invoke-QaPmProbe $c 'start' 'play'
            for ($k = 1; $k -le 4; $k++) {
                Invoke-QaPadPress $c 'rt' 0.6; Wait-QaSeconds $c 0.4
                Invoke-QaPadPress $c 'rb' 0.15; Wait-QaSeconds $c 1.2
                Save-QaGameWindowShot $c "switch-$k"
                Invoke-QaPadPress $c 'rt' 0.6
                Invoke-QaPmProbe $c "after switch $k" 'play'
            }
            foreach ($pick in 'ry=-32767', 'rx=32767', 'rx=-32767', 'ry=32767') {
                Write-QaPad $c 'rb'; Wait-QaSeconds $c 0.8
                Write-QaPad $c "rb,$pick"; Wait-QaSeconds $c 0.6
                Save-QaGameWindowShot $c "wheel-$($pick -replace '[=-]', '')"
                # The shoulder let go first, the stick still on the slot: the release picks it.
                Write-QaPad $c $pick; Wait-QaSeconds $c 0.1
                Write-QaPad $c ''; Wait-QaSeconds $c 1
                Invoke-QaPadPress $c 'rt' 0.6
                Invoke-QaPmProbe $c "after wheel $pick" 'play'
            }
            Save-QaGameWindowShot $c 'end'
        }
        Asserts = {
            param($c)
            Test-QaPresent $c 'controllers: gamepad mode (motion controllers off)'
            Test-QaPresent $c 'forced view starts \(weapon wheel' -Regex
            Test-QaAbsent $c $script:QaPmModelLine
            Test-QaAbsent $c 'menu: the Dossier opens'
            Test-QaAbsent $c 'menu: a menu over the game without a cursor'
            $rows = Get-QaPmTrace $c
            $play = Test-QaPmProbes $c $rows 'play' 0
            Add-QaResult $c 'combat: every look-stick probe after a switch or a wheel pick turned the view (over 3 deg, no menu flag)' ($play.Count -ge 9 -and $play.Bad -eq 0) $play.Text
            $fired = @(Get-QaPmEvents $c "bhaptics: firing '([^']+)'" | ForEach-Object { if ($_.Text -match "firing '([^']+)'") { $Matches[1] } })
            Add-QaNote $c "weapons fired (bHaptics' line on each change): $(($fired | Select-Object -Unique) -join ', ')"
        }
    },
    @{
        Name = 'pad-dossier-close'; Kind = 'game'; OnlyByName = $true; Pad = $true
        MenuMission = 'continue'; MenuSlot = 1; MenuMap = 'game/hub/hub'; MenuSaveNote = $script:QaPmSave
        Proves = 'Gamepad mode: the Dossier''s hold ends at once however a pad closes it (B, after a Start inside it, then the pause menu''s Resume, Load Checkpoint or Exit to Main Menu), and nothing else logs a Dossier open'
        Env = @('ETERNALVR_CONTROLLERS=0', 'ETERNALVR_TEST_CAMERA_TRACE=all', "ETERNALVR_DEBUG_COMMANDS=$script:QaPmSchedule")
        Args = @('+in_joystick 1')
        Timeline = {
            param($c)
            Wait-QaSeconds $c 8
            Invoke-QaPmProbe $c 'play 1' 'play'
            Invoke-QaPmProbe $c 'play 2' 'play'
            Invoke-QaPmDossierClose $c 'b' @() @() $false
            Invoke-QaPmDossierClose $c 'start' @('start') @() $false
            # The pause menu: Resume, Load Checkpoint, Settings, Milestones, Exit to Main Menu, Exit to Desktop; its
            # NOTICE (accept or cancel) opens on Cancel.
            Invoke-QaPmDossierClose $c 'resume' @() @('start', 'a') $false
            Invoke-QaPmDossierClose $c 'load' @() @('start', 'down', 'a', 'left', 'a') $true
            Invoke-QaPmDossierClose $c 'exit-main' @() @('start', 'down', 'down', 'down', 'down', 'a', 'left', 'a') $true
        }
        Asserts = {
            param($c)
            Test-QaPresent $c 'controllers: gamepad mode (motion controllers off)'
            Test-QaPresent $c "menu: the Dossier's open and close hooked"
            $rows = Get-QaPmTrace $c
            $opens = @(Get-QaPmEvents $c 'menu: the Dossier opens')
            $closes = @(Get-QaPmEvents $c 'menu: the Dossier is closed')
            $steps = @($c.PmDossier)
            $bad = 0; $text = @()
            foreach ($s in $steps) {
                $open = @($opens | Where-Object { $_.T -ge $s.Opened -and $_.T -le $s.Press }).Count
                $close = @($closes | Where-Object { $_.T -ge $s.Press -and $_.T -le $s.End }) | Select-Object -First 1
                $ok = $open -eq 1 -and $close -and ($close.T - $s.Press) -le 1.0 -and $close.Text -notmatch 'a map load ended it' -and (-not $s.Load -or $s.Loaded)
                if (-not $ok) { $bad++ }
                $text += ('{0}: {1} open(s), closed {2}{3}' -f $s.Name, $open,
                    $(if ($close) { '{0:N2} s after the B' -f ($close.T - $s.Press) } else { 'NOT logged' }),
                    $(if ($s.Load) { $(if ($s.Loaded) { ', map load came' } else { ', NO map load' }) } else { '' }))
            }
            Add-QaResult $c 'each close path: one Dossier open, its close logged within 1 s of the B (not by a map load)' ($steps.Count -ge 5 -and $bad -eq 0) ($text -join '; ')
            Add-QaResult $c 'no Dossier open but the steps'' (none at the start, at HUD set-up or at a map load)' ($opens.Count -eq $steps.Count) ('{0} open line(s), {1} step(s); {2} map load(s)' -f $opens.Count, $steps.Count, (Get-QaPmCount $c $script:QaPmLoadLine))
            Test-QaAbsent $c 'menu: the Dossier is closed (a map load ended it)'
            $under = Test-QaPmProbes $c $rows 'under' 0
            Add-QaResult $c 'under the Dossier in each step: the look stick turned nothing (within 1 deg over 10 lines with the menu flag or more)' ($under.Count -ge 5 -and $under.Bad -eq 0) $under.Text
            $expect = Get-QaPmExpect $c $rows
            $after = Test-QaPmProbes $c $rows 'after' $expect.Mean
            Add-QaResult $c 'back in play after each close that returns to it: the look stick turned the view as in play' ($after.Count -ge 3 -and $after.Bad -eq 0) $after.Text
        }
    }
)

# Before the launcher's end-to-end suite, which stays last.
$e2e = @($script:QaScenarios | Where-Object { $_.Kind -eq 'e2e' })
$script:QaScenarios = @($script:QaScenarios | Where-Object { $_.Kind -ne 'e2e' }) + $padModelScenarios + $e2e
