# The weapon cues in the game's crosshair under hand aim (docs/VR_CONTROLLERS.md, "The game's crosshair under hand
# aim"; ui_layer/gun_crosshair.hpp). The game draws every weapon's reticle with its cues in the middle of its GUI
# target; under hand aim (the default) ETERNALVR_UI_CROSSHAIR=gun (opt-in: the launcher's "Game's crosshair (hand aim)"
# set to "On the gun") moves that rectangle to its own quad on the gun's ray; the default, hold, keeps it hidden. By
# name only (-Only). Dot-sourced by qa-suite.ps1 after qa-scenarios-meathook.ps1, whose e1m1 barge,
# door and zombie placement it uses.
#
# The run: e1m1's barge with its first encounter started, god, everything given (`give all`), the demons held where
# they are put (`ai_freezeMovement 1`), the Rocket Launcher up (weapon_slot_4 on a touch of A) with its Lock-On Burst
# (the weapon mod switched with Y until the log names `rocket_launcher_lock_mod`). From the barge's door a zombie is
# brought onto the floor about 9.5 m ahead along the game's view; under hand aim the hand is 20 degrees left of the
# head, so the zombie stands where the gun points and away from where the head looks. Then the weapon mod (the right
# grip) is held on it: shots of the composed frame (-xr-both.png) as the lock builds and once it is locked, the
# trigger with it (the burst), and the cooldown after. GUI captures (ETERNALVR_CAPTURE_UI into <stage>-ui) show what
# the game drew in its GUI target, before the layer moves anything.
#
#   crosshair-gun-lockon        hand aim, gun: the game's crosshair (the Lock-On Burst's lock, LOCKED ON, the
#                               cooldown) shows on the gun's ray. Checks: the mode and its quad in the log, no hold of
#                               hud_reticle_styleOverride once that quad is up, every GUI copy moved while hand aim
#                               runs, and the GUI captures' middle holds the drawn reticle.
#   crosshair-hold-lockon       hand aim, hold (the behaviour up to 0.1.96, the positive control): the crosshair held at
#                               none, the GUI captures' middle empty, nothing moved.
#   crosshair-gun-lockon-head   head aim, gun: the game's crosshair stays in the middle of the HUD, nothing moved, no
#                               hold (the look the hand-aim quad should match).
#   crosshair-default-lockon    hand aim, no ETERNALVR_UI_CROSSHAIR (the launcher's default path, Hidden): hold, as
#                               crosshair-hold-lockon, and the gun's layer never made.
#   crosshair-gun-nodot         hand aim, gun with the aim dot off (ETERNALVR_UI_RETICLE=0): held as under hold, nothing
#                               in the middle, the gun's layer never made.
#   crosshair-gun-lockon-margin hand aim, gun with ETERNALVR_EDGE_MARGIN=small: the same checks as crosshair-gun-lockon; the
#                               shots show the quad still on the dot, the HUD as without the margin.
#   crosshair-gun-wheel         hand aim, gun: the weapon wheel opened and closed eight times, a GUI capture of nearly
#                               every copy: no copy moved while the wheel is up or just gone (ui_layer::WheelLinger),
#                               and the captures, timed by their copy index, show when the game stops drawing it.
#
# What the simulator cannot show: a headset's lenses and the reticle's size and sharpness in them, a real hand's aim
# and tremor (the scripted hand is still but for its noise), and how far a player's eyes go between the crosshair on
# the gun and the HUD.

$script:QaCueSchedule = ((@(($script:QaGloryWave -f 20, 21, 24, 25)) + '27:give all' + '30:ai_freezeMovement 1' +
    ('40:{0}|41:nextActiveAI;ai_teleportToPlayer trace|90:removeAI' -f $script:QaHookDoor)) -join '|')
# The Rocket Launcher on a touch of A (weapon_slot_4, where the game's wheel has it).
$script:QaCueSlot = '"right.primary_touch.tap" = "weapon_slot_4"'
$script:QaCueRest = 'right.position = 0.2, -0.3, -0.35'
$script:QaCueLockMod = 'weapon/player/rocket_launcher_lock_mod'
# The held Rocket Launcher's mods as the controllers log them (`held item` at the switch, `mods changed on held` at a
# mod switch): the last such line.
$script:QaCueModsLine = "(?:held item|mods changed on held) 'weapon/player/rocket_launcher' \(mods '([^']*)', '([^']*)'\)"
# A GUI capture's middle counts as drawn with this many pixels of alpha over 64 in the moved rectangle, at 1280 x 720
# of band: under the hold, the ability indicator beside the crosshair has about 210 alone and the lock-on's targeting
# brackets on the zombie (a world marker, which the move leaves on the HUD) add about 170 (rig runs QA-wc-q2 and -q3:
# 207 to 379 in every capture); with the crosshair drawn the Rocket Launcher's idle reticle makes 395, its lock 620 to
# 1100.
$script:QaCueDrawnPixels = 500

function Get-QaCueMods($Ctx) {
    $all = [regex]::Matches((Get-QaLogText $Ctx), $script:QaCueModsLine)
    if ($all.Count -eq 0) { return $null }
    $g = $all[$all.Count - 1].Groups
    return "$($g[1].Value),$($g[2].Value)"
}

# The moved rectangle of a W x H GUI target as x, y, w, h: as the layer logs it (`ui: the game's crosshair goes on the
# gun's ray under hand aim: WxH at (X, Y) of the WxH GUI target`), else as ui::gunCrosshairRect makes it
# (kGunCrosshairWidth x kGunCrosshairHeight of the 16:9 band's height, centred in the band).
$script:QaCueWidth = 1.28
$script:QaCueHeight = 0.35
function Get-QaCueRect($Ctx, [int]$W, [int]$H) {
    if ((Get-QaLogText $Ctx) -match "the game's crosshair goes on the gun's ray under hand aim: (\d+)x(\d+) at \((\d+), (\d+)\) of the (\d+)x(\d+) GUI target") {
        if ([int]$Matches[5] -eq $W -and [int]$Matches[6] -eq $H) { return @([int]$Matches[3], [int]$Matches[4], [int]$Matches[1], [int]$Matches[2]) }
    }
    $bw = $W; $bh = $H
    if ([Math]::Abs($W / $H - 16 / 9) -gt 0.005 * 16 / 9) {
        if ($W / $H -lt 16 / 9) { $bh = [int][Math]::Round($W * 9 / 16) } else { $bw = [int][Math]::Round($H * 16 / 9) }
    }
    $bx = [int](($W - $bw) / 2); $by = [int](($H - $bh) / 2)
    $rw = [Math]::Min($bw, [int][Math]::Round($script:QaCueWidth * $bh)); $rh = [Math]::Min($bh, [int][Math]::Round($script:QaCueHeight * $bh))
    return @(($bx + [int][Math]::Floor(($bw - $rw) / 2)), ($by + [int][Math]::Floor(($bh - $rh) / 2)), $rw, $rh)
}

# Per GUI capture of the run (<stage>-ui) written after the schedule's `give all` (the map's start, its loading screen
# and its texts left out), the pixels of alpha over 64 inside the moved rectangle: objects File, Drawn.
function Measure-QaCueCaptures($Ctx) {
    $out = New-Object System.Collections.ArrayList
    $since = Get-QaLogLineTime (Get-QaLogText $Ctx) 'debug-commands: 27\.0 s: give all'
    if (-not $since) { Add-QaNote $Ctx 'no give all line: no GUI capture measured'; return $out }
    $files = @(Get-ChildItem -LiteralPath "$($Ctx.Stage)-ui" -Filter 'ui-*.png' -File -ErrorAction SilentlyContinue |
        Where-Object { $_.LastWriteTime -ge $since } | Sort-Object LastWriteTime)
    foreach ($f in $files) {
        try {
            $bmp = New-Object Drawing.Bitmap $f.FullName
            try {
                $r = Get-QaCueRect $Ctx $bmp.Width $bmp.Height
                $rect = New-Object Drawing.Rectangle $r[0], $r[1], $r[2], $r[3]
                $data = $bmp.LockBits($rect, [Drawing.Imaging.ImageLockMode]::ReadOnly, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
                try {
                    $bytes = New-Object byte[] ($data.Stride * $r[3])
                    [Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
                } finally { $bmp.UnlockBits($data) }
                $n = 0
                for ($y = 0; $y -lt $r[3]; $y++) {
                    $row = $y * $data.Stride
                    for ($x = 0; $x -lt $r[2]; $x++) { if ($bytes[$row + 4 * $x + 3] -gt 64) { $n++ } }
                }
                [void]$out.Add([pscustomobject]@{ File = $f.Name; Drawn = $n })
            } finally { $bmp.Dispose() }
        } catch { Add-QaNote $Ctx "GUI capture $($f.Name) not read: $($_.Exception.Message)" }
    }
    return $out
}

$script:QaCueTimeline = {
    param($c)
    $rest = $script:QaCueRest
    $p0 = $script:QaHookPlacePitch
    $place = if ($c.Scenario.Aim -eq 'hand') { 20 } else { 0 }
    $base = @("right.aim = $place, $p0", $rest)
    Write-QaInput $c $base
    if (-not (Wait-QaLog $c 'debug-commands: 27\.0 s: give all' 120)) { Add-QaNote $c 'no give all line' }
    Wait-QaSeconds $c 2
    Write-QaInput $c ($base + 'right.primary_touch = 1'); Wait-QaSeconds $c 0.4
    Write-QaInput $c $base; Wait-QaSeconds $c 2.5
    # The Lock-On Burst: the weapon mod switched (Y) until the controllers name it.
    for ($i = 0; $i -lt 3 -and (Get-QaCueMods $c) -notmatch [regex]::Escape($script:QaCueLockMod); $i++) {
        Write-QaInput $c ($base + 'left.secondary = 1'); Wait-QaSeconds $c 0.15
        Write-QaInput $c $base; Wait-QaSeconds $c 2
    }
    Add-QaNote $c "the Rocket Launcher's mods: $(Get-QaCueMods $c)"
    if (-not (Wait-QaLog $c 'debug-commands: 41\.0 s: nextActiveAI' 120)) { Add-QaNote $c 'the zombie at 41 s was not logged'; return }
    Wait-QaSeconds $c 0.5
    $on = @("right.aim = $place, $script:QaHookBodyPitch", $rest)
    Write-QaInput $c $on; Wait-QaSeconds $c 1.5
    Save-QaXrShot $c 'idle'
    # The mod held on the zombie: the lock builds, then holds.
    Write-QaInput $c ($on + 'right.grip = 1'); Wait-QaSeconds $c 0.3
    Save-QaXrShot $c 'lock-start'
    Wait-QaSeconds $c 0.6
    Save-QaXrShot $c 'lock-building'
    Wait-QaSeconds $c 1.5
    Save-QaXrShot $c 'locked'
    # The burst, then the mod let go: the cooldown.
    Write-QaInput $c ($on + @('right.grip = 1', 'right.trigger = 1')); Wait-QaSeconds $c 0.2
    Write-QaInput $c ($on + 'right.grip = 1'); Wait-QaSeconds $c 0.4
    Save-QaXrShot $c 'fired'
    Write-QaInput $c $on; Wait-QaSeconds $c 0.8
    Save-QaXrShot $c 'cooldown'
    Wait-QaSeconds $c 4
    # A second lock after the cooldown, held without firing.
    Write-QaInput $c ($on + 'right.grip = 1'); Wait-QaSeconds $c 2.5
    Save-QaXrShot $c 'locked-again'
    Write-QaInput $c $on; Wait-QaSeconds $c 2
}

# $Mode '' leaves ETERNALVR_UI_CROSSHAIR out (the layer's default, as the launcher's Hidden passes nothing); $Extra adds
# environment entries.
function New-QaCueScenario([string]$Name, [string]$Mode, [string]$Aim, [string]$Proves, [scriptblock]$Checks, [string[]]$Extra = @()) {
    $envList = @("ETERNALVR_AIM=$Aim")
    if ($Mode) { $envList += "ETERNALVR_UI_CROSSHAIR=$Mode" }
    return @{
        Name = $Name; Kind = 'game'; OnlyByName = $true; Aim = $Aim; Proves = $Proves
        Map = 'game/sp/e1m1_intro/e1m1_intro'
        # The render size makes the GUI target the eye's near-square size, as in a headset (1280x1400 on the
        # simulator), as the crosshair scenarios do.
        Env = @($envList + @('ETERNALVR_RENDER_SIZE=auto', 'ETERNALVR_HOLD_SECONDS=1', "ETERNALVR_DEBUG_COMMANDS=$script:QaCueSchedule") + $Extra)
        UiCaptures = 45
        Prepare = { param($c) New-QaRestControls $c $script:QaCueSlot }
        Input = @("right.aim = 0, $script:QaHookPlacePitch", $script:QaCueRest)
        Timeline = $script:QaCueTimeline
        CueChecks = $Checks
        Asserts = {
            param($c)
            Test-QaPresent $c $script:QaInputRead -Regex
            $mods = @(Get-QaMatches $c $script:QaCueModsLine -Regex | Where-Object { $_.Contains($script:QaCueLockMod) })
            Add-QaResult $c "the Rocket Launcher held with its Lock-On Burst ($script:QaCueLockMod)" ($mods.Count -gt 0) "$($mods.Count) line(s)"
            $shots = @(Measure-QaCueCaptures $c)
            $drawn = @($shots | Where-Object { $_.Drawn -ge $script:QaCueDrawnPixels })
            $c['CueDrawn'] = $drawn.Count
            $c['CueShots'] = $shots.Count
            Add-QaNote $c ('GUI captures: {0}, {1} with the crosshair drawn in the middle (pixels: {2})' -f $shots.Count, $drawn.Count,
                (($shots | ForEach-Object { $_.Drawn }) -join ' '))
            & $c.Scenario.CueChecks $c
        }
    }
}

# The GUI copies and moves of the `ui: the gun's crosshair: ...` 10 s lines after the first move (e1m1's opening
# cutscene holds a full-screen backdrop until about 40 s into the log: no move then, as intended), the first of them
# left out (it counts copies from before that move).
function Get-QaCueMoves($Ctx) {
    $copies = 0; $moves = 0
    $span = @(Get-QaLogSpan $Ctx "ui: first GUI copy with the game's crosshair moved to the gun's ray" '(?!)')
    $lines = @($span | Where-Object { $_.Contains("ui: the gun's crosshair: ") } | Select-Object -Skip 1)
    foreach ($l in $lines) {
        if ($l -match "(\d+) GUI copies, (\d+) with the crosshair moved") { $copies += [int]$Matches[1]; $moves += [int]$Matches[2] }
    }
    return @($copies, $moves)
}

# Every weapon slot with both its mods under hand aim (gun): each slot drawn (a touch bound to weapon_slot_N), then
# for each mod: idle, the mod held, the mod held and firing; the chainsaw held up once. Shots of the composed frame
# and GUI captures every 20 copies for the inventory of the crosshair's cues (docs/VR_CONTROLLERS.md, "The game's
# crosshair under hand aim"): what each reticle draws, and that the moved rectangle holds it.
$script:QaSweepSlots = @(
    @{ Slot = 1; Touch = 'right.primary_touch'; Bind = 'right.primary_touch' },
    @{ Slot = 2; Touch = 'right.secondary_touch'; Bind = 'right.secondary_touch' },
    @{ Slot = 3; Touch = 'left.primary_touch'; Bind = 'left.primary_touch' },
    @{ Slot = 4; Touch = 'left.secondary_touch'; Bind = 'left.secondary_touch' },
    @{ Slot = 5; Touch = 'right.thumbrest'; Bind = 'right.thumbrest' },
    @{ Slot = 6; Touch = 'left.thumbrest'; Bind = 'left.thumbrest' },
    @{ Slot = 7; Touch = 'right.thumbstick_touch'; Bind = 'right.stick_touch' },
    @{ Slot = 8; Touch = 'left.thumbstick_touch'; Bind = 'left.stick_touch' })
$script:QaSweepBinds = (@($script:QaSweepSlots | ForEach-Object { '"{0}.tap" = "weapon_slot_{1}"' -f $_.Bind, $_.Slot }) -join "`n")

$script:QaSweepTimeline = {
    param($c)
    $rest = $script:QaCueRest
    $p0 = $script:QaHookPlacePitch
    $base = @("right.aim = 20, $p0", $rest)
    Write-QaInput $c $base
    if (-not (Wait-QaLog $c 'debug-commands: 41\.0 s: nextActiveAI' 150)) { Add-QaNote $c 'the zombie at 41 s was not logged'; return }
    Wait-QaSeconds $c 0.5
    $on = @("right.aim = 20, $script:QaHookBodyPitch", $rest)
    foreach ($s in $script:QaSweepSlots) {
        Write-QaInput $c ($on + "$($s.Touch) = 1"); Wait-QaSeconds $c 0.4
        Write-QaInput $c $on; Wait-QaSeconds $c 2.5
        foreach ($mod in 'a', 'b') {
            $n = "s$($s.Slot)$mod"
            Save-QaXrShot $c "$n-idle"
            Write-QaInput $c ($on + 'right.grip = 1'); Wait-QaSeconds $c 1.2
            Save-QaXrShot $c "$n-mod"
            Write-QaInput $c ($on + @('right.grip = 1', 'right.trigger = 1')); Wait-QaSeconds $c 0.5
            Save-QaXrShot $c "$n-fire"
            Write-QaInput $c $on; Wait-QaSeconds $c 1
            Save-QaXrShot $c "$n-after"
            if ($mod -eq 'a') {
                Write-QaInput $c ($on + 'left.secondary = 1'); Wait-QaSeconds $c 0.15
                Write-QaInput $c $on; Wait-QaSeconds $c 2
            }
        }
    }
    Write-QaInput $c ($on + 'right.stick = 0, 1'); Wait-QaSeconds $c 0.6
    Save-QaXrShot $c 'chainsaw'
    Write-QaInput $c $on; Wait-QaSeconds $c 2
}

# The weapon wheel opened (right stick down held past the hold time) and closed eight times with the Rocket Launcher
# up, a zombie ahead; then a few seconds of play so the last fade is followed by moved copies.
$script:QaCueWheelTimeline = {
    param($c)
    $rest = $script:QaCueRest
    $on = @("right.aim = 20, $script:QaHookPlacePitch", $rest)
    Write-QaInput $c $on
    if (-not (Wait-QaLog $c 'debug-commands: 41\.0 s: nextActiveAI' 150)) { Add-QaNote $c 'the zombie at 41 s was not logged'; return }
    Wait-QaSeconds $c 1
    Write-QaInput $c ($on + 'right.primary_touch = 1'); Wait-QaSeconds $c 0.4
    Write-QaInput $c $on; Wait-QaSeconds $c 2.5
    foreach ($i in 1..8) {
        Write-QaInput $c ($on + 'right.stick = 0, -1'); Wait-QaSeconds $c 1.8
        Save-QaXrShot $c "wheel$i-up"
        Write-QaInput $c $on; Wait-QaSeconds $c 0.15
        Save-QaXrShot $c "wheel$i-closing"
        Wait-QaSeconds $c 3
    }
    Save-QaXrShot $c 'after'
    Wait-QaSeconds $c 2
}

$cueScenarios = @(
    @{
        Name = 'crosshair-gun-sweep'; Kind = 'game'; OnlyByName = $true; Aim = 'hand'
        Proves = 'Hand aim with ETERNALVR_UI_CROSSHAIR=gun: every weapon slot with both mods, idle, the mod held and firing, a zombie on the gun''s line: the game''s crosshair and its cues on the gun''s ray (shots and GUI captures for the inventory)'
        Map = 'game/sp/e1m1_intro/e1m1_intro'
        Env = @('ETERNALVR_AIM=hand', 'ETERNALVR_UI_CROSSHAIR=gun', 'ETERNALVR_RENDER_SIZE=auto', 'ETERNALVR_HOLD_SECONDS=1',
                "ETERNALVR_DEBUG_COMMANDS=$script:QaCueSchedule")
        UiCaptures = 20
        Prepare = { param($c) New-QaRestControls $c $script:QaSweepBinds }
        Input = @("right.aim = 20, $script:QaHookPlacePitch", $script:QaCueRest)
        Timeline = $script:QaSweepTimeline
        Asserts = {
            param($c)
            Test-QaPresent $c $script:QaInputRead -Regex
            Test-QaPresent $c "ui: first GUI copy with the game's crosshair moved to the gun's ray"
            $held = @([regex]::Matches((Get-QaLogText $c), "held item '(weapon/player/[^']+)'") | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
            Add-QaResult $c 'the sweep drew 8 weapons or more' ($held.Count -ge 8) ($held -join ', ')
            Test-QaGunHoldOnlyScoped $c
        }
    },
    (New-QaCueScenario 'crosshair-gun-lockon' 'gun' 'hand' 'Hand aim with ETERNALVR_UI_CROSSHAIR=gun (the defaults): the game draws its crosshair and the layer moves it to the gun''s ray; the Rocket Launcher''s Lock-On Burst locking onto a zombie 20 degrees off the head, where the gun points, shows its lock, LOCKED ON and cooldown there' {
        param($c)
        Test-QaPresent $c "the game's crosshair under hand aim: gun"
        Test-QaPresent $c "ui: the game's crosshair goes on the gun's ray under hand aim"
        Test-QaPresent $c "ui: first GUI copy with the game's crosshair moved to the gun's ray"
        Test-QaPresent $c "ui: the game's crosshair on the gun's ray: its quad"
        # Once the quad is up, no hold: the game draws its crosshair (a write before it, from the start-up's hold, is
        # given back).
        $after = @(Get-QaLogSpan $c "ui: the game's crosshair goes on the gun's ray" '(?!)')
        $hides = @($after | Where-Object { $_ -match 'hud_reticle_styleOverride -?\d+ -> 0 \(reads 0\)' })
        Add-QaResult $c 'after the crosshair''s quad is up: no hud_reticle_styleOverride -> 0 write' ($after.Count -gt 0 -and $hides.Count -eq 0) "$($hides.Count) write(s) in $($after.Count) line(s)"
        $m = Get-QaCueMoves $c
        Add-QaResult $c 'the 10 s lines after the first move: every GUI copy moved to the gun''s ray' ($m[0] -gt 0 -and $m[1] -eq $m[0]) "$($m[1]) of $($m[0]) copies"
        Add-QaResult $c 'the GUI captures: the crosshair drawn in the middle (no hold) in most of them' ($c.CueShots -gt 0 -and $c.CueDrawn -ge 0.8 * $c.CueShots) "$($c.CueDrawn) of $($c.CueShots)"
    }),
    (New-QaCueScenario 'crosshair-hold-lockon' 'hold' 'hand' 'Hand aim with ETERNALVR_UI_CROSSHAIR=hold (the behaviour up to 0.1.96, the positive control): the game''s crosshair held at none, so the Lock-On Burst''s lock, LOCKED ON and cooldown show nowhere' {
        param($c)
        Test-QaPresent $c "the game's crosshair under hand aim: hold"
        Test-QaPresent $c 'hud_reticle_styleOverride -?\d+ -> 0 \(reads 0\); hand aim' -Regex
        Test-QaAbsent $c "ui: the game's crosshair goes on the gun's ray"
        Add-QaResult $c 'the GUI captures: no crosshair drawn in the middle (the ability indicator beside it only)' ($c.CueShots -gt 3 -and $c.CueDrawn -eq 0) "$($c.CueDrawn) of $($c.CueShots)"
    }),
    (New-QaCueScenario 'crosshair-gun-lockon-head' 'gun' 'head' 'Head aim with ETERNALVR_UI_CROSSHAIR=gun: the game''s crosshair stays in the middle of the HUD (no hold, nothing moved), the reference for the hand-aim quad' {
        param($c)
        Test-QaPresent $c "ui: the game's crosshair goes on the gun's ray under hand aim"
        Test-QaAbsent $c "ui: first GUI copy with the game's crosshair moved to the gun's ray"
        Test-QaAbsent $c 'hud_reticle_styleOverride -?\d+ -> 0 \(reads 0\)' -Regex
        Add-QaResult $c 'the GUI captures: the crosshair drawn in the middle in most of them' ($c.CueShots -gt 0 -and $c.CueDrawn -ge 0.8 * $c.CueShots) "$($c.CueDrawn) of $($c.CueShots)"
    }),
    (New-QaCueScenario 'crosshair-default-lockon' '' 'hand' 'Hand aim without ETERNALVR_UI_CROSSHAIR (the launcher''s default, Game''s crosshair Hidden, passes nothing): the layer''s default is hold, the crosshair held at none, nothing moved and no layer made for it' {
        param($c)
        Test-QaPresent $c "the game's crosshair under hand aim: hold"
        Test-QaPresent $c 'hud_reticle_styleOverride -?\d+ -> 0 \(reads 0\); hand aim' -Regex
        Test-QaAbsent $c "ui: the game's crosshair goes on the gun's ray"
        Test-QaAbsent $c "ui: the gun's crosshair:"
        Add-QaResult $c 'the GUI captures: no crosshair drawn in the middle (the ability indicator beside it only)' ($c.CueShots -gt 3 -and $c.CueDrawn -eq 0) "$($c.CueDrawn) of $($c.CueShots)"
    }),
    (New-QaCueScenario 'crosshair-gun-nodot' 'gun' 'hand' 'Hand aim with ETERNALVR_UI_CROSSHAIR=gun and the aim dot off (ETERNALVR_UI_RETICLE=0): nothing in the middle, held as under hold, no layer made for the gun''s crosshair' {
        param($c)
        Test-QaPresent $c "the game's crosshair under hand aim: gun, with the aim dot off"
        Test-QaPresent $c 'hud_reticle_styleOverride -?\d+ -> 0 \(reads 0\); hand aim' -Regex
        Test-QaAbsent $c "ui: the game's crosshair goes on the gun's ray"
        Test-QaAbsent $c 'ui: the aim dot at' -Regex
        Add-QaResult $c 'the GUI captures: no crosshair drawn in the middle (the ability indicator beside it only)' ($c.CueShots -gt 3 -and $c.CueDrawn -eq 0) "$($c.CueDrawn) of $($c.CueShots)"
    } @('ETERNALVR_UI_RETICLE=0')),
    (New-QaCueScenario 'crosshair-gun-lockon-margin' 'gun' 'hand' 'Hand aim with ETERNALVR_UI_CROSSHAIR=gun and ETERNALVR_EDGE_MARGIN=small: the eyes rendered wider than their view, the crosshair''s quad still on the dot and the HUD as without the margin (shots)' {
        param($c)
        Test-QaPresent $c "edge margin: the projection layer's eye"
        Test-QaPresent $c "ui: the game's crosshair goes on the gun's ray under hand aim"
        Test-QaPresent $c "ui: the game's crosshair on the gun's ray: its quad"
        $m = Get-QaCueMoves $c
        Add-QaResult $c 'the 10 s lines after the first move: every GUI copy moved to the gun''s ray' ($m[0] -gt 0 -and $m[1] -eq $m[0]) "$($m[1]) of $($m[0]) copies"
        Add-QaResult $c 'the GUI captures: the crosshair drawn in the middle (no hold) in most of them' ($c.CueShots -gt 0 -and $c.CueDrawn -ge 0.8 * $c.CueShots) "$($c.CueDrawn) of $($c.CueShots)"
    } @('ETERNALVR_EDGE_MARGIN=small')),
    @{
        Name = 'crosshair-gun-wheel'; Kind = 'game'; OnlyByName = $true; Aim = 'hand'
        Proves = 'Hand aim with ETERNALVR_UI_CROSSHAIR=gun: the weapon wheel opened and closed eight times; while it is up and for a moment after it goes down (ui_layer::WheelLinger) the GUI copies stay whole (the wheel''s middle never moved to the gun''s ray); GUI captures for when the game stops drawing it'
        Map = 'game/sp/e1m1_intro/e1m1_intro'
        Env = @('ETERNALVR_AIM=hand', 'ETERNALVR_UI_CROSSHAIR=gun', 'ETERNALVR_RENDER_SIZE=auto', 'ETERNALVR_HOLD_SECONDS=1',
                "ETERNALVR_DEBUG_COMMANDS=$script:QaCueSchedule")
        UiCaptures = 1
        Prepare = { param($c) New-QaRestControls $c $script:QaCueSlot }
        Input = @("right.aim = 20, $script:QaHookPlacePitch", $script:QaCueRest)
        Timeline = $script:QaCueWheelTimeline
        Asserts = {
            param($c)
            Test-QaPresent $c $script:QaInputRead -Regex
            Test-QaPresent $c "ui: first GUI copy with the game's crosshair moved to the gun's ray"
            $ups = @(Get-QaMatches $c 'ui: weapon wheel back on the HUD after|ui: weapon wheel up for' -Regex)
            Add-QaResult $c 'the weapon wheel went up and down three times or more' ($ups.Count -ge 3) "$($ups.Count) line(s)"
            $fades = @(Get-QaMatches $c 'ui: the weapon wheel went down: (\d+) GUI copies in the' -Regex)
            Add-QaResult $c 'each wheel close: the copies just after it kept whole' ($fades.Count -ge 3) ($fades -join ' | ')
        }
    })

$e2e = @($script:QaScenarios | Where-Object { $_.Kind -eq 'e2e' })
$script:QaScenarios = @($script:QaScenarios | Where-Object { $_.Kind -ne 'e2e' }) + $cueScenarios + $e2e
