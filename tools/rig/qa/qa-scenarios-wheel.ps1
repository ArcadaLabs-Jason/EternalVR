# The thumb-rest weapon wheel's scenarios (docs/VR_CONTROLLERS.md, "The thumb-rest wheel"), dot-sourced by
# qa-suite.ps1 after qa-scenarios.ps1: added to $script:QaScenarios before the launcher's end-to-end suite.
# Scripted input stands in for the thumb rests (left.thumbrest = 1; with ETERNALVR_TEST_INPUT both hands
# count as having one). The window is 1 s here (ETERNALVR_THUMBREST_WINDOW) so the file's timing cannot miss
# it, except where a scenario needs the default. The layer re-reads the file every 10 game frames, so a
# change takes effect up to a sixth of a second after it is written: nothing here is shorter than that.

$script:QaWheelWindow = 'ETERNALVR_THUMBREST_WINDOW=1'
# The wheel is off by default since 2026-10-09: every scenario but the extreme one turns edge on itself.
$script:QaWheelOn = 'ETERNALVR_THUMBREST_WHEEL=edge'
$script:QaRestLeft = @('left.thumbrest = 1')

# The held item the viewmodel logged last before the first line containing $After, and those it logged after it.
function Get-QaHeldItems($Ctx, [string]$After) {
    $lines = (Get-QaLogText $Ctx) -split "`r?`n"
    $at = -1
    for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i].Contains($After)) { $at = $i; break } }
    $before = $null
    $later = @()
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -notmatch "held item '([^']*)'") { continue }
        if ($at -lt 0 -or $i -lt $at) { $before = $Matches[1] } else { $later += $Matches[1] }
    }
    return [pscustomobject]@{ Before = $before; After = $later }
}

# The thumb rest touched, the turn stick pushed within the window, held, turned, then let go.
function Invoke-QaRestPick($Ctx, [string[]]$First, [string[]]$Then) {
    Write-QaInput $Ctx $script:QaRestLeft; Wait-QaSeconds $Ctx 0.3
    Write-QaInput $Ctx ($script:QaRestLeft + $First); Wait-QaSeconds $Ctx 1.2
    Save-QaShot $Ctx 'wheel'
    if ($Then.Count -gt 0) { Write-QaInput $Ctx ($script:QaRestLeft + $Then); Wait-QaSeconds $Ctx 0.8 }
    Write-QaInput $Ctx $script:QaRestLeft; Wait-QaSeconds $Ctx 1.5
    Write-QaInput $Ctx @(); Wait-QaSeconds $Ctx 2
}

$wheelScenarios = @(
    @{
        Name = 'thumbrest-pick'; Kind = 'game'
        Proves = 'Thumb-rest wheel, touch then push: the left thumb rest, then the turn stick pushed up within the window arms it, the game''s wheel is held and pointed up, then right, and letting go picks (the held item changes)'
        Env = @($script:QaWheelOn, $script:QaWheelWindow)
        Input = @()
        Timeline = {
            param($c)
            Wait-QaSeconds $c 12
            Invoke-QaRestPick $c @('right.stick = 0, 1') @('right.stick = 1, 0')
            Save-QaShot $c 'after'
        }
        Asserts = {
            param($c)
            Test-QaPresent $c $script:QaInputRead -Regex
            Test-QaPresent $c 'thumb-rest wheel edge: rest sensors on both hands'
            Test-QaPresent $c 'thumb-rest wheel: armed: the right stick picks (left thumb rest, edge'
            Test-QaPresent $c "thumb-rest wheel: the game's wheel is held, pointing up"
            Test-QaPresent $c 'controllers: action weapon_wheel'
            Test-QaPresent $c "the weapon wheel is up: the stick moves the game's wheel cursor"
            Test-QaPresent $c 'weapon wheel: pointing right'
            Test-QaPresent $c "thumb-rest wheel: the game's wheel let go pointing right"
            Test-QaPresent $c 'weapon wheel released after'
            Test-QaAbsent $c 'controllers: action quick_switch'
            Test-QaAbsent $c 'controllers: action chainsaw'
            $held = Get-QaHeldItems $c "the game's wheel let go"
            $changed = @($held.After | Where-Object { $_ -ne $held.Before }).Count -gt 0
            Add-QaResult $c 'held item changed after the pick' $changed "before '$($held.Before)', after: $($held.After -join ', ')"
        }
    },
    @{
        Name = 'thumbrest-resting-turns'; Kind = 'game'
        Proves = 'A thumb left on its rest for seconds does not stop the turn stick turning (default window): the turn goes out, nothing arms, no wheel'
        Env = @($script:QaWheelOn, 'ETERNALVR_CONTROLLERS_TRACE=1')
        Input = $script:QaRestLeft
        Timeline = {
            param($c)
            Wait-QaSeconds $c 12
            Write-QaInput $c ($script:QaRestLeft + 'right.stick = 1, 0'); Wait-QaSeconds $c 1
            Write-QaInput $c $script:QaRestLeft; Wait-QaSeconds $c 1
            Save-QaShot $c 'after-turn'
            Write-QaInput $c @(); Wait-QaSeconds $c 2
        }
        Asserts = {
            param($c)
            Test-QaPresent $c 'thumb-rest wheel: the left controller reports a resting thumb'
            Test-QaPresent $c 'trace: mapper turn'
            Test-QaAbsent $c 'thumb-rest wheel: armed'
            Test-QaAbsent $c 'controllers: action weapon_wheel'
        }
    },
    @{
        Name = 'thumbrest-flick'; Kind = 'game'
        Proves = 'A push that never settles in a direction presses nothing: armed, then cancelled; no weapon_wheel, no quick switch, no chainsaw'
        Env = @($script:QaWheelOn, $script:QaWheelWindow)
        Input = @()
        Timeline = {
            param($c)
            Wait-QaSeconds $c 12
            # Out of the centre (0.25) but short of a direction (0.5): armed, then cancelled.
            Write-QaInput $c $script:QaRestLeft; Wait-QaSeconds $c 0.3
            Write-QaInput $c ($script:QaRestLeft + 'right.stick = 0, 0.4'); Wait-QaSeconds $c 0.6
            Write-QaInput $c $script:QaRestLeft; Wait-QaSeconds $c 1
            Save-QaShot $c 'after-push'
            Write-QaInput $c @(); Wait-QaSeconds $c 2
        }
        Asserts = {
            param($c)
            Test-QaPresent $c 'thumb-rest wheel: armed: the right stick picks'
            Test-QaPresent $c 'thumb-rest wheel: cancelled, nothing pressed'
            Test-QaAbsent $c 'controllers: action weapon_wheel'
            Test-QaAbsent $c 'controllers: action quick_switch'
            Test-QaAbsent $c 'controllers: action chainsaw'
        }
    },
    @{
        Name = 'thumbrest-window-expiry'; Kind = 'game'
        Proves = 'The default window ends: the left thumb rests, and a turn-stick flick after the window turns; nothing arms'
        Env = @($script:QaWheelOn, 'ETERNALVR_CONTROLLERS_TRACE=1')
        Input = @()
        Timeline = {
            param($c)
            Wait-QaSeconds $c 12
            # 0.9 s rather than just past the 0.5 s window: the landing is seen up to a sixth of a second late.
            Write-QaInput $c $script:QaRestLeft; Wait-QaSeconds $c 0.9
            Write-QaInput $c ($script:QaRestLeft + 'right.stick = 1, 0'); Wait-QaSeconds $c 0.6
            Write-QaInput $c $script:QaRestLeft; Wait-QaSeconds $c 1
            Write-QaInput $c @(); Wait-QaSeconds $c 2
        }
        Asserts = {
            param($c)
            Test-QaPresent $c 'thumb-rest wheel: the left controller reports a resting thumb'
            Test-QaPresent $c 'trace: mapper turn'
            Test-QaAbsent $c 'thumb-rest wheel: armed'
            Test-QaAbsent $c 'controllers: action weapon_wheel'
        }
    },
    @{
        Name = 'thumbrest-cancel-then-flick'; Kind = 'game'
        Proves = 'A cancel does not reopen the window: a push short of a direction is cancelled, and the next turn-stick flick, the thumb still resting inside the window, turns'
        Env = @($script:QaWheelOn, $script:QaWheelWindow, 'ETERNALVR_CONTROLLERS_TRACE=1')
        Input = @()
        Timeline = {
            param($c)
            Wait-QaSeconds $c 12
            Write-QaInput $c $script:QaRestLeft; Wait-QaSeconds $c 0.3
            Write-QaInput $c ($script:QaRestLeft + 'right.stick = 0, 0.4'); Wait-QaSeconds $c 0.4
            Write-QaInput $c $script:QaRestLeft; Wait-QaSeconds $c 0.3
            Write-QaInput $c ($script:QaRestLeft + 'right.stick = 1, 0'); Wait-QaSeconds $c 0.6
            Write-QaInput $c $script:QaRestLeft; Wait-QaSeconds $c 1
            Write-QaInput $c @(); Wait-QaSeconds $c 2
        }
        Asserts = {
            param($c)
            Test-QaCount $c 'thumb-rest wheel: armed' 1
            Test-QaPresent $c 'thumb-rest wheel: cancelled, nothing pressed'
            Test-QaPresent $c 'trace: mapper turn'
            Test-QaAbsent $c 'controllers: action weapon_wheel'
        }
    },
    @{
        Name = 'thumbrest-quick-pick'; Kind = 'game'
        Proves = 'A quick pick: the turn stick pushed right for about 0.2 s and let go opens the wheel, holds it for its minimum, and the held item changes'
        Env = @($script:QaWheelOn, $script:QaWheelWindow)
        Input = @()
        Timeline = {
            param($c)
            Wait-QaSeconds $c 12
            Write-QaInput $c $script:QaRestLeft; Wait-QaSeconds $c 0.3
            # Past the 0.1 s dwell even with the file seen a sixth of a second late.
            Write-QaInput $c ($script:QaRestLeft + 'right.stick = 1, 0'); Wait-QaSeconds $c 0.2
            Write-QaInput $c $script:QaRestLeft; Wait-QaSeconds $c 1.5
            Save-QaShot $c 'after'
            Write-QaInput $c @(); Wait-QaSeconds $c 2
        }
        Asserts = {
            param($c)
            Test-QaPresent $c "thumb-rest wheel: the game's wheel is held, pointing right"
            Test-QaPresent $c 'thumb-rest wheel: the game opens its wheel'
            Test-QaPresent $c "thumb-rest wheel: the game's wheel let go"
            Test-QaAbsent $c 'controllers: action quick_switch'
            $held = Get-QaHeldItems $c "the game's wheel let go"
            $changed = @($held.After | Where-Object { $_ -ne $held.Before }).Count -gt 0
            Add-QaResult $c 'held item changed after the quick pick' $changed "before '$($held.Before)', after: $($held.After -join ', ')"
        }
    },
    @{
        Name = 'thumbrest-extreme'; Kind = 'game'
        Proves = 'Turn stick is the wheel: with no thumb resting the turn stick opens the wheel, and with the left thumb on its rest it turns'
        Env = @('ETERNALVR_THUMBREST_WHEEL=extreme', 'ETERNALVR_CONTROLLERS_TRACE=1')
        Input = @()
        Timeline = {
            param($c)
            Wait-QaSeconds $c 12
            Write-QaInput $c @('right.stick = 0, 1'); Wait-QaSeconds $c 1.2
            Save-QaShot $c 'wheel'
            Write-QaInput $c @(); Wait-QaSeconds $c 1.5
            Write-QaInput $c $script:QaRestLeft; Wait-QaSeconds $c 0.5
            Write-QaInput $c ($script:QaRestLeft + 'right.stick = 1, 0'); Wait-QaSeconds $c 1
            Write-QaInput $c $script:QaRestLeft; Wait-QaSeconds $c 1
            Write-QaInput $c @(); Wait-QaSeconds $c 2
        }
        Asserts = {
            param($c)
            Test-QaPresent $c 'thumb-rest wheel extreme: rest sensors on both hands'
            Test-QaPresent $c 'thumb-rest wheel: armed: the turn stick picks weapons; the left thumb rest gives turning back'
            Test-QaPresent $c 'controllers: action weapon_wheel'
            Test-QaPresent $c "thumb-rest wheel: the game's wheel let go"
            Test-QaPresent $c 'trace: mapper turn'
        }
    },
    @{
        Name = 'thumbrest-slots'; Kind = 'game'
        Proves = 'Weapon by direction: the turn stick held right picks weapon slot 2 (the heavy cannon, as on the game''s wheel) on letting go, and the game''s wheel never opens'
        Env = @($script:QaWheelOn, $script:QaWheelWindow, 'ETERNALVR_THUMBREST_PICK=slots')
        Input = @()
        Timeline = {
            param($c)
            Wait-QaSeconds $c 12
            Write-QaInput $c $script:QaRestLeft; Wait-QaSeconds $c 0.3
            Write-QaInput $c ($script:QaRestLeft + 'right.stick = 1, 0'); Wait-QaSeconds $c 0.6
            Write-QaInput $c $script:QaRestLeft; Wait-QaSeconds $c 1.5
            Save-QaShot $c 'after'
            Write-QaInput $c @(); Wait-QaSeconds $c 2
        }
        Asserts = {
            param($c)
            Test-QaPresent $c 'thumb-rest wheel edge: rest sensors on both hands, picks with weapon by direction (up=1,up_right=5,'
            Test-QaPresent $c 'thumb-rest wheel: weapon_slot_2 picked by the right stick pointing right'
            Test-QaPresent $c 'controllers: action weapon_slot_2'
            Test-QaAbsent $c 'controllers: action weapon_wheel'
        }
    },
    @{
        Name = 'thumbrest-menu'; Kind = 'game'
        Proves = 'Nothing arms in a menu: with the pause menu up, a resting thumb and a pushed stick start nothing'
        Env = @($script:QaWheelOn, $script:QaWheelWindow)
        Input = @()
        Timeline = {
            param($c)
            Wait-QaSeconds $c 12
            Write-QaInput $c @('left.menu = 1'); Wait-QaSeconds $c 0.2
            Write-QaInput $c @(); Wait-QaSeconds $c 2
            Save-QaShot $c 'menu'
            Write-QaInput $c $script:QaRestLeft; Wait-QaSeconds $c 0.3
            Write-QaInput $c ($script:QaRestLeft + 'right.stick = 0, 1'); Wait-QaSeconds $c 1
            Write-QaInput $c @(); Wait-QaSeconds $c 1
            Write-QaInput $c @('left.menu = 1'); Wait-QaSeconds $c 0.2
            Write-QaInput $c @(); Wait-QaSeconds $c 3
        }
        Asserts = {
            param($c)
            Test-QaPresent $c 'controllers: action pause'
            Test-QaPresent $c 'gameplay input back on'
            Test-QaAbsent $c 'thumb-rest wheel: armed'
            Test-QaAbsent $c 'controllers: action weapon_wheel'
        }
    },
    @{
        Name = 'thumbrest-slowdown'; Kind = 'game'
        Proves = 'Slow time on the thumb-rest wheel off: weaponWheel_slowTimeScale is held at 1 while the wheel is open and its own value written back after'
        Env = @($script:QaWheelOn, $script:QaWheelWindow, 'ETERNALVR_THUMBREST_SLOWDOWN=0')
        Input = @()
        Timeline = {
            param($c)
            Wait-QaSeconds $c 12
            Invoke-QaRestPick $c @('right.stick = 0, 1') @()
        }
        Asserts = {
            param($c)
            Test-QaPresent $c 'no slowdown (ETERNALVR_THUMBREST_SLOWDOWN=0): weaponWheel_slowTimeScale, now'
            $held = @(Get-QaMatches $c 'weaponWheel_slowTimeScale (\S+) -> 1 while the wheel is open' -Regex)
            $back = @(Get-QaMatches $c "weaponWheel_slowTimeScale back to (\S+), the game's own value \(the wheel closed\)" -Regex)
            Add-QaResult $c 'held at 1 while the wheel is open' ($held.Count -gt 0) "$($held.Count) line(s)"
            Add-QaResult $c "the game's value written back after" ($back.Count -gt 0) "$($back.Count) line(s)"
            if ($held.Count -gt 0 -and $back.Count -gt 0 -and $held[0] -match '_slowTimeScale (\S+) ->') {
                $before = $Matches[1]
                $null = $back[0] -match 'back to (\S+),'
                Add-QaResult $c 'the value written back is the one held over' ($Matches[1] -eq $before) "held over $before, written back $($Matches[1])"
            }
            Test-QaAbsent $c 'weaponWheel_slowTimeScale not found'
        }
    }
)

# Before the launcher's end-to-end suite, which stays last.
$e2e = @($script:QaScenarios | Where-Object { $_.Kind -eq 'e2e' })
$script:QaScenarios = @($script:QaScenarios | Where-Object { $_.Kind -ne 'e2e' }) + $wheelScenarios + $e2e
