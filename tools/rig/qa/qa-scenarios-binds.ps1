# The thumb rests and touches bound like buttons: a rest bound like a button (docs/VR_CONTROLLERS.md, "A thumb rest
# as a button") and a touch on a control bound like a button. Dot-sourced by qa-suite.ps1 after
# qa-scenarios-wheel.ps1, whose helpers and settings it uses (Get-QaHeldItems, $script:QaRestLeft, $script:QaWheelOn,
# $script:QaWheelWindow): added to $script:QaScenarios before the launcher's end-to-end suite.

# A thumb rest bound like a button (docs/VR_CONTROLLERS.md, "A thumb rest as a button"): the player's Touch file
# is the built-in one with "left.thumbrest.tap" = "weapon_slot_2" added to [map.right], written into the
# scenario's folder and passed as ETERNALVR_CONTROLLER_DATA. The hold time is 1 s (the launcher's Hold time,
# ETERNALVR_HOLD_SECONDS) and a touch 0.4 s: the file is seen up to a sixth of a second late at each end, so
# the touch as sensed is 0.23 to 0.57 s, a tap under any timing, where with the default 0.25 s it could be
# either. The bound slot is the heavy cannon, which the 100% save does not start the map with.
$script:QaRestTap = '"left.thumbrest.tap" = "weapon_slot_2"'
$script:QaRestHold = 'ETERNALVR_HOLD_SECONDS=1'
# One line per action pressed: "controllers: action <name>" (action aim's lines go on with a colon).
$script:QaActionLine = 'controllers: action [a-z0-9_]+( \(|$)'

# The built-in Touch data with $Line added to the right-handed map, into the scenario's folder.
function New-QaRestControls($Ctx, [string]$Line) {
    $src = Join-Path $script:QaRepo 'data\input\controllers\oculus_touch.toml'
    $lines = [Collections.Generic.List[string]](Get-Content -LiteralPath $src)
    $map = $lines.IndexOf('[map.right]')
    if ($map -lt 0) { throw "no [map.right] in $src" }
    $lines.Insert($map + 1, $Line)
    $Ctx.ControlsFile = Join-Path $Ctx.Dir 'oculus_touch.toml'
    [IO.File]::WriteAllText($Ctx.ControlsFile, (($lines -join "`n") + "`n"))
    return "ETERNALVR_CONTROLLER_DATA=$($Ctx.ControlsFile)"
}

# The left thumb rest touched for 0.4 s and lifted.
function Invoke-QaRestTap($Ctx) {
    Write-QaInput $Ctx $script:QaRestLeft; Wait-QaSeconds $Ctx 0.4
    Write-QaInput $Ctx @(); Wait-QaSeconds $Ctx 2
}

# The taps and holds summed over the 10 s lines of the left rest bound like a button.
function Get-QaRestButtonCounts($Ctx) {
    $taps = 0; $holds = 0
    foreach ($l in @(Get-QaMatches $Ctx 'left rest button, last 10 s:')) {
        if ($l -match ' (\d+) tap\(s\), (\d+) hold\(s\)') { $taps += [int]$Matches[1]; $holds += [int]$Matches[2] }
    }
    return [pscustomobject]@{ Taps = $taps; Holds = $holds }
}

# Every action line of the run, in order, against the expected list.
function Test-QaActionLines($Ctx, [string[]]$Expected) {
    $got = @(Get-QaMatches $Ctx $script:QaActionLine -Regex | ForEach-Object { ($_ -split 'controllers: action ')[1] })
    $want = if ($Expected.Count) { $Expected -join ', ' } else { 'none' }
    $seen = if ($got.Count) { $got -join ', ' } else { 'none' }
    Add-QaResult $Ctx "actions pressed: $want" ($seen -eq $want) "logged: $seen"
}

$restButtonScenarios = @(
    @{
        Name = 'restbutton-tap'; Kind = 'game'
        Proves = 'A thumb rest bound like a button reaches the game (wheel off): the player''s Touch file binds left.thumbrest.tap to weapon_slot_2, a 0.4 s touch of the left rest presses weapon_slot_2 once and the heavy cannon comes up'
        Env = @($script:QaRestHold, 'ETERNALVR_CONTROLLERS_TRACE=1')
        Prepare = { param($c) New-QaRestControls $c $script:QaRestTap }
        Input = @()
        Timeline = {
            param($c)
            Wait-QaSeconds $c 12
            Invoke-QaRestTap $c
            Save-QaShot $c 'after'
            [void](Wait-QaLog $c 'left rest button, last 10 s:' 20)
            Wait-QaSeconds $c 10
        }
        Asserts = {
            param($c)
            Test-QaPresent $c "controller data '$($c.ControlsFile)' replaces the built-in data of its profile"
            Test-QaPresent $c 'controllers: the left thumb rest is bound (tap = weapon_slot_2)'
            Test-QaPresent $c 'controllers: thumb-rest wheel off'
            Test-QaPresent $c 'thumb rest button: the left controller reports a resting thumb'
            Test-QaActionLines $c @('weapon_slot_2')
            $n = Get-QaRestButtonCounts $c
            Add-QaResult $c 'the rest button counted 1 tap, 0 holds' ($n.Taps -eq 1 -and $n.Holds -eq 0) "$($n.Taps) tap(s), $($n.Holds) hold(s)"
            $held = Get-QaHeldItems $c 'controllers: action weapon_slot_2'
            $first = @($held.After)[0]
            Add-QaResult $c 'the heavy cannon came up after the tap' ($first -match 'heavy_cannon' -and $held.Before -ne $first) "before '$($held.Before)', after: $($held.After -join ', ')"
        }
    },
    @{
        Name = 'restbutton-unbound'; Kind = 'game'
        Proves = 'The control for restbutton-wheel: the built-in Touch data (no rest bound), the wheel on, the same 0.4 s touch of the left rest: the wheel sees the touch, nothing is pressed and the held item stays'
        Env = @($script:QaWheelOn, $script:QaWheelWindow, $script:QaRestHold, 'ETERNALVR_CONTROLLERS_TRACE=1')
        Input = @()
        Timeline = {
            param($c)
            Wait-QaSeconds $c 12
            Invoke-QaRestTap $c
            [void](Wait-QaLog $c 'thumb-rest wheel: left rest, last 10 s:' 20)
            Wait-QaSeconds $c 10
        }
        Asserts = {
            param($c)
            Test-QaPresent $c 'thumb-rest wheel edge: rest sensors on both hands'
            Test-QaAbsent $c 'thumb rest is bound'
            Test-QaPresent $c 'thumb-rest wheel: the left controller reports a resting thumb'
            Test-QaPresent $c 'thumb-rest wheel: left rest, last 10 s: [1-9]\d* touch\(es\) from the sensor' -Regex
            Test-QaAbsent $c 'thumb rest button'
            Test-QaAbsent $c 'thumb-rest wheel: armed'
            Test-QaActionLines $c @()
            $items = @(Get-QaMatches $c "held item '[^']*'" -Regex | ForEach-Object { if ($_ -match "held item '([^']*)'") { $Matches[1] } } | Select-Object -Unique)
            Add-QaResult $c 'the held item never changed' ($items.Count -eq 1) "held: $($items -join ', ')"
        }
    },
    @{
        Name = 'restbutton-wheel'; Kind = 'game'
        Proves = 'One rest, one job (wheel edge on): with left.thumbrest.tap bound the wheel uses the right rest only; a 0.4 s touch of the left rest presses weapon_slot_2 once, the left rest then the turn stick within the window turns and arms nothing (that 2 s touch is a hold: no second press), and the right rest then the left stick arms the wheel'
        Env = @($script:QaWheelOn, $script:QaWheelWindow, $script:QaRestHold, 'ETERNALVR_CONTROLLERS_TRACE=1')
        Prepare = { param($c) New-QaRestControls $c $script:QaRestTap }
        Input = @()
        Timeline = {
            param($c)
            Wait-QaSeconds $c 12
            Invoke-QaRestTap $c
            Write-QaInput $c $script:QaRestLeft; Wait-QaSeconds $c 0.3
            Write-QaInput $c ($script:QaRestLeft + 'right.stick = 1, 0'); Wait-QaSeconds $c 0.6
            Write-QaInput $c $script:QaRestLeft; Wait-QaSeconds $c 1
            Write-QaInput $c @(); Wait-QaSeconds $c 2
            Write-QaInput $c @('right.thumbrest = 1'); Wait-QaSeconds $c 0.3
            Write-QaInput $c @('right.thumbrest = 1', 'left.stick = 0, 1'); Wait-QaSeconds $c 1.2
            Write-QaInput $c @('right.thumbrest = 1'); Wait-QaSeconds $c 1.5
            Write-QaInput $c @(); Wait-QaSeconds $c 2
            # The rest button's 10 s lines: the tap and the hold may fall in two of them.
            [void](Wait-QaLog $c 'left rest button, last 10 s:' 20)
            Wait-QaSeconds $c 10
        }
        Asserts = {
            param($c)
            Test-QaPresent $c "controller data '$($c.ControlsFile)' replaces the built-in data of its profile"
            Test-QaPresent $c 'controllers: the left thumb rest is bound (tap = weapon_slot_2); the thumb-rest wheel uses the right rest only'
            Test-QaPresent $c 'thumb-rest wheel edge: rest sensors on the right hand'
            Test-QaPresent $c 'thumb rest button: the left controller reports a resting thumb'
            Test-QaCount $c 'controllers: action weapon_slot_2' 1
            Test-QaAbsent $c 'armed: the right stick picks (left thumb rest'
            Test-QaPresent $c 'trace: mapper turn'
            Test-QaPresent $c 'thumb-rest wheel: armed: the left stick picks (right thumb rest, edge'
            Test-QaPresent $c 'controllers: action weapon_wheel'
            $n = Get-QaRestButtonCounts $c
            Add-QaResult $c 'the rest button counted 1 tap, 1 hold' ($n.Taps -eq 1 -and $n.Holds -eq 1) "$($n.Taps) tap(s), $($n.Holds) hold(s)"
        }
    }
)

# A touch on a control bound like a button (docs/VR_CONTROLLERS.md, "Touch sensors as buttons"): the player's
# Touch file binds "right.primary_touch.tap" (A touched, not pressed) to weapon_slot_2, as the rest button's
# scenarios bind the rest, with the same 1 s hold time and 0.4 s touch. A press of A while touched is a jump and
# ends the touch: no slot 2. So is a press of A with the thumb also on B when B's touch is the one bound.
$script:QaTouchTap = '"right.primary_touch.tap" = "weapon_slot_2"'
$script:QaTouchTapB = '"right.secondary_touch.tap" = "weapon_slot_2"'
$script:QaTouchA = @('right.primary_touch = 1')
$script:QaTouchB = @('right.secondary_touch = 1')
$script:QaTouchLine = 'controllers: touches bound as buttons (using a control under that thumb or finger ends one): '
# The scripted touch reaching the layer (ETERNALVR_CONTROLLERS_TRACE), bound or not.
$script:QaTouchTraceA = 'controllers: trace: touch sensors: right primary_touch'

# The taps and holds summed over the 10 s lines of a touch bound like a button ('primary_touch', on the right
# hand unless $Hand says), the touches its own controls (a button under that thumb pressed) ended, and the
# landings voided straight after the control under the touch.
function Get-QaTouchButtonCounts($Ctx, [string]$Touch = 'primary_touch', [string]$Hand = 'right') {
    $n = [pscustomobject]@{ Taps = 0; Holds = 0; Ended = 0; Voided = 0 }
    foreach ($l in @(Get-QaMatches $Ctx "$Hand $Touch button, last 10 s:")) {
        if ($l -match ' (\d+) tap\(s\), (\d+) hold\(s\), (\d+) ended by its own control') {
            $n.Taps += [int]$Matches[1]; $n.Holds += [int]$Matches[2]; $n.Ended += [int]$Matches[3]
        }
        if ($l -match ' (\d+) landing\(s\) voided') { $n.Voided += [int]$Matches[1] }
    }
    return $n
}

$touchButtonScenarios = @(
    @{
        Name = 'touchbtn-tap'; Kind = 'game'
        Proves = 'A touch on a control bound like a button reaches the game: the player''s Touch file binds right.primary_touch.tap to weapon_slot_2, A touched (not pressed) for 0.4 s presses weapon_slot_2 once and the heavy cannon comes up'
        Env = @($script:QaRestHold, 'ETERNALVR_CONTROLLERS_TRACE=1')
        Prepare = { param($c) New-QaRestControls $c $script:QaTouchTap }
        Input = @()
        Timeline = {
            param($c)
            Wait-QaSeconds $c 12
            Write-QaInput $c $script:QaTouchA; Wait-QaSeconds $c 0.4
            Write-QaInput $c @(); Wait-QaSeconds $c 2
            Save-QaShot $c 'after'
            [void](Wait-QaLog $c 'right primary_touch button, last 10 s:' 20)
            Wait-QaSeconds $c 10
        }
        Asserts = {
            param($c)
            Test-QaPresent $c "controller data '$($c.ControlsFile)' replaces the built-in data of its profile"
            Test-QaPresent $c 'controllers: 1 touch binding(s) for touches bound as buttons added to the /interaction_profiles/oculus/touch_controller data'
            Test-QaPresent $c ($script:QaTouchLine + 'right.primary_touch.tap = weapon_slot_2')
            Test-QaPresent $c $script:QaTouchTraceA
            Test-QaPresent $c 'touch button: the right controller reports primary_touch'
            Test-QaActionLines $c @('weapon_slot_2')
            $n = Get-QaTouchButtonCounts $c
            Add-QaResult $c 'the touch button counted 1 tap, 0 holds' ($n.Taps -eq 1 -and $n.Holds -eq 0) "$($n.Taps) tap(s), $($n.Holds) hold(s)"
            $held = Get-QaHeldItems $c 'controllers: action weapon_slot_2'
            $first = @($held.After)[0]
            Add-QaResult $c 'the heavy cannon came up after the tap' ($first -match 'heavy_cannon' -and $held.Before -ne $first) "before '$($held.Before)', after: $($held.After -join ', ')"
        }
    },
    @{
        Name = 'touchbtn-unbound'; Kind = 'game'
        Proves = 'The control for touchbtn-tap: the built-in Touch data (no touch bound), the same 0.4 s touch of A reaches the layer (its trace line): no touch binding is added, nothing is pressed and the held item stays'
        Env = @($script:QaRestHold, 'ETERNALVR_CONTROLLERS_TRACE=1')
        Input = @()
        Timeline = {
            param($c)
            Wait-QaSeconds $c 12
            Write-QaInput $c $script:QaTouchA; Wait-QaSeconds $c 0.4
            Write-QaInput $c @(); Wait-QaSeconds $c 12
        }
        Asserts = {
            param($c)
            Test-QaAbsent $c 'for touches bound as buttons added'
            Test-QaAbsent $c 'touches bound as buttons'
            # 'touch button:' (not 'touch button': the prompts line names 'oculus_touch buttons').
            Test-QaAbsent $c 'touch button:'
            Test-QaAbsent $c 'primary_touch button'
            Test-QaPresent $c $script:QaTouchTraceA
            Test-QaActionLines $c @()
            $items = @(Get-QaMatches $c "held item '[^']*'" -Regex | ForEach-Object { if ($_ -match "held item '([^']*)'") { $Matches[1] } } | Select-Object -Unique)
            Add-QaResult $c 'the held item never changed' ($items.Count -eq 1) "held: $($items -join ', ')"
        }
    },
    @{
        Name = 'touchbtn-press'; Kind = 'game'
        Proves = 'A touch that goes on to press its own control is no tap: with right.primary_touch.tap bound, A touched 0.2 s, pressed 0.3 s (a jump), let go with the thumb still on it 0.3 s, then lifted: jump is pressed once and weapon_slot_2 never; then a plain 0.4 s touch still taps it once'
        Env = @($script:QaRestHold, 'ETERNALVR_CONTROLLERS_TRACE=1')
        Prepare = { param($c) New-QaRestControls $c $script:QaTouchTap }
        Input = @()
        Timeline = {
            param($c)
            Wait-QaSeconds $c 12
            Write-QaInput $c $script:QaTouchA; Wait-QaSeconds $c 0.2
            Write-QaInput $c ($script:QaTouchA + 'right.primary = 1'); Wait-QaSeconds $c 0.3
            Write-QaInput $c $script:QaTouchA; Wait-QaSeconds $c 0.3
            Write-QaInput $c @(); Wait-QaSeconds $c 2
            Write-QaInput $c $script:QaTouchA; Wait-QaSeconds $c 0.4
            Write-QaInput $c @(); Wait-QaSeconds $c 2
            [void](Wait-QaLog $c 'right primary_touch button, last 10 s:' 20)
            Wait-QaSeconds $c 10
        }
        Asserts = {
            param($c)
            Test-QaPresent $c ($script:QaTouchLine + 'right.primary_touch.tap = weapon_slot_2')
            Test-QaActionLines $c @('jump', 'weapon_slot_2')
            $n = Get-QaTouchButtonCounts $c
            Add-QaResult $c 'the touch button counted 1 tap, 0 holds, 1 ended by A' ($n.Taps -eq 1 -and $n.Holds -eq 0 -and $n.Ended -eq 1) "$($n.Taps) tap(s), $($n.Holds) hold(s), $($n.Ended) ended by its own control"
        }
    },
    @{
        Name = 'touchbtn-face'; Kind = 'game'
        Proves = 'One thumb works every button under it: with right.secondary_touch.tap bound (B touched), the thumb on A and B 0.2 s, A pressed 0.3 s (a jump), let go with the thumb still on both 0.3 s, then lifted: jump is pressed once and weapon_slot_2 never; then a plain 0.4 s touch of B still taps it once'
        Env = @($script:QaRestHold, 'ETERNALVR_CONTROLLERS_TRACE=1')
        Prepare = { param($c) New-QaRestControls $c $script:QaTouchTapB }
        Input = @()
        Timeline = {
            param($c)
            Wait-QaSeconds $c 12
            $both = $script:QaTouchA + $script:QaTouchB
            Write-QaInput $c $both; Wait-QaSeconds $c 0.2
            Write-QaInput $c ($both + 'right.primary = 1'); Wait-QaSeconds $c 0.3
            Write-QaInput $c $both; Wait-QaSeconds $c 0.3
            Write-QaInput $c @(); Wait-QaSeconds $c 2
            Write-QaInput $c $script:QaTouchB; Wait-QaSeconds $c 0.4
            Write-QaInput $c @(); Wait-QaSeconds $c 2
            [void](Wait-QaLog $c 'right secondary_touch button, last 10 s:' 20)
            Wait-QaSeconds $c 10
        }
        Asserts = {
            param($c)
            Test-QaPresent $c ($script:QaTouchLine + 'right.secondary_touch.tap = weapon_slot_2')
            Test-QaPresent $c 'controllers: trace: touch sensors: right primary_touch, right secondary_touch'
            Test-QaActionLines $c @('jump', 'weapon_slot_2')
            $n = Get-QaTouchButtonCounts $c 'secondary_touch'
            Add-QaResult $c 'the B touch button counted 1 tap, 0 holds, 1 ended by A' ($n.Taps -eq 1 -and $n.Holds -eq 0 -and $n.Ended -eq 1) "$($n.Taps) tap(s), $($n.Holds) hold(s), $($n.Ended) ended by its own control"
        }
    },
    @{
        Name = 'touchbtn-stick'; Kind = 'game'
        Proves = 'Only the control under a touch voids its landing: with left.primary_touch.tap bound (X touched), the left stick pushed 0.5 s and let go, X touched 0.15 s later for 0.2 s: weapon_slot_2 is pressed once, no landing voided'
        Env = @($script:QaRestHold, 'ETERNALVR_CONTROLLERS_TRACE=1')
        Prepare = { param($c) New-QaRestControls $c '"left.primary_touch.tap" = "weapon_slot_2"' }
        Input = @()
        Timeline = {
            param($c)
            Wait-QaSeconds $c 12
            Write-QaInput $c @('left.stick = 0, 1'); Wait-QaSeconds $c 0.5
            Write-QaInput $c @(); Wait-QaSeconds $c 0.15
            Write-QaInput $c @('left.primary_touch = 1'); Wait-QaSeconds $c 0.2
            Write-QaInput $c @(); Wait-QaSeconds $c 2
            [void](Wait-QaLog $c 'left primary_touch button, last 10 s:' 20)
            Wait-QaSeconds $c 10
        }
        Asserts = {
            param($c)
            Test-QaPresent $c ($script:QaTouchLine + 'left.primary_touch.tap = weapon_slot_2')
            Test-QaPresent $c 'controllers: trace: touch sensors: left primary_touch'
            Test-QaActionLines $c @('weapon_slot_2')
            $n = Get-QaTouchButtonCounts $c 'primary_touch' 'left'
            Add-QaResult $c 'the X touch button counted 1 tap, 0 holds, 0 landings voided' ($n.Taps -eq 1 -and $n.Holds -eq 0 -and $n.Voided -eq 0) "$($n.Taps) tap(s), $($n.Holds) hold(s), $($n.Voided) landing(s) voided"
        }
    }
)


# Before the launcher's end-to-end suite, which stays last.
$e2e = @($script:QaScenarios | Where-Object { $_.Kind -eq 'e2e' })
$script:QaScenarios = @($script:QaScenarios | Where-Object { $_.Kind -ne 'e2e' }) + $restButtonScenarios + $touchButtonScenarios + $e2e
