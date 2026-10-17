# The thumb rests' scenarios: the weapon wheel (docs/VR_CONTROLLERS.md, "The thumb-rest wheel") and, at the
# end, a rest bound like a button and a touch on a control bound like a button. Dot-sourced by
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

# The weapon wheel straight ahead (docs/VR_MENUS.md, "The weapon wheel"): two GUI captures (ETERNALVR_CAPTURE_UI into
# <stage>-ui), the last one written before the stick went down and the first one written 1 s after (the wheel open),
# differ inside the rectangle the layer logged when it put the wheel ahead: at least 99% of the changed pixels lie in
# it and they span at least 70% of its width and height. A game that draws the wheel anywhere else fails; a few
# pixels of HUD animation elsewhere (the vitals' icons changed 40 pixels in a rig run) do not.
if (-not ('QaWheelAlpha' -as [type])) {
    Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class QaWheelAlpha {
    // The alpha channel, row-major.
    public static byte[] Alpha(string path, out int width, out int height) {
        using (var bmp = new Bitmap(path)) {
            width = bmp.Width; height = bmp.Height;
            var data = bmp.LockBits(new Rectangle(0, 0, width, height), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            try {
                var bytes = new byte[data.Stride * height];
                Marshal.Copy(data.Scan0, bytes, 0, bytes.Length);
                var a = new byte[width * height];
                for (int y = 0; y < height; ++y)
                    for (int x = 0; x < width; ++x) a[y * width + x] = bytes[y * data.Stride + x * 4 + 3];
                return a;
            } finally { bmp.UnlockBits(data); }
        }
    }
    // The pixels whose alpha differs by more than 32, against the rectangle [x0, x1) x [y0, y1): the count inside,
    // the count outside, and the box of those inside (x0, y0, x1, y1).
    public static long[] DiffStats(byte[] a, byte[] b, int width, int height, int x0, int y0, int x1, int y1) {
        long inside = 0, outside = 0, bx0 = width, by0 = height, bx1 = 0, by1 = 0;
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                if (Math.Abs(a[y * width + x] - b[y * width + x]) <= 32) continue;
                if (x < x0 || x >= x1 || y < y0 || y >= y1) { ++outside; continue; }
                ++inside; bx0 = Math.Min(bx0, x); by0 = Math.Min(by0, y); bx1 = Math.Max(bx1, x + 1); by1 = Math.Max(by1, y + 1);
            }
        return new long[] { inside, outside, bx0, by0, bx1, by1 };
    }
}
'@
}
function Test-QaWheelRect($Ctx) {
    $name = 'the wheel lies in the logged rectangle'
    $line = @(Get-QaMatches $Ctx 'ui: weapon wheel straight ahead \(\w+ HUD\): GUI rect \((\d+), (\d+)\) (\d+) x (\d+)' -Regex) | Select-Object -First 1
    if (-not ($line -match 'GUI rect \((\d+), (\d+)\) (\d+) x (\d+)')) { Add-QaResult $Ctx $name $false 'no rectangle logged'; return }
    $rx = [int]$Matches[1]; $ry = [int]$Matches[2]; $rw = [int]$Matches[3]; $rh = [int]$Matches[4]
    if (-not $Ctx.WheelDown) { Add-QaResult $Ctx $name $false 'the timeline did not record the stick'; return }
    $files = @(Get-ChildItem -LiteralPath "$($Ctx.Stage)-ui" -Filter 'ui-*.png' -ErrorAction SilentlyContinue | Sort-Object LastWriteTime)
    $before = @($files | Where-Object { $_.LastWriteTime -lt $Ctx.WheelDown }) | Select-Object -Last 1
    $open = @($files | Where-Object { $_.LastWriteTime -gt $Ctx.WheelDown.AddSeconds(1) -and $_.LastWriteTime -lt $Ctx.WheelUp }) | Select-Object -First 1
    if (-not $before -or -not $open) { Add-QaResult $Ctx $name $false "no GUI capture before the stick and with the wheel open among $($files.Count)"; return }
    $w = 0; $h = 0; $w2 = 0; $h2 = 0
    try {
        $a = [QaWheelAlpha]::Alpha($before.FullName, [ref]$w, [ref]$h)
        $b = [QaWheelAlpha]::Alpha($open.FullName, [ref]$w2, [ref]$h2)
    } catch { Add-QaResult $Ctx $name $false "a capture did not load: $($_.Exception.Message)"; return }
    if ($w -ne $w2 -or $h -ne $h2 -or $rx + $rw -gt $w -or $ry + $rh -gt $h) { Add-QaResult $Ctx $name $false "sizes $w x $h, $w2 x $h2, rectangle ($rx, $ry) $rw x $rh"; return }
    $s = [QaWheelAlpha]::DiffStats($a, $b, $w, $h, $rx, $ry, $rx + $rw, $ry + $rh)
    $share = if ($s[0] + $s[1] -gt 0) { $s[0] / [double]($s[0] + $s[1]) } else { 0.0 }
    $fills = $s[0] -gt 0 -and ($s[4] - $s[2]) -ge 0.7 * $rw -and ($s[5] - $s[3]) -ge 0.7 * $rh
    Add-QaResult $Ctx $name ($share -ge 0.99 -and $fills) ("$($before.Name) -> $($open.Name): $($s[0]) changed pixel(s) inside, $($s[1]) outside ({0:P1} inside), inside ($($s[2]), $($s[3]))..($($s[4]), $($s[5])); rectangle ($rx, $ry) $rw x $rh" -f $share)
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
        Proves = 'Turn stick picks (standing): with no thumb resting the turn stick opens the wheel, with the left thumb on its rest it turns, and with the right thumb on its rest the left stick picks'
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
            Write-QaInput $c @('right.thumbrest = 1'); Wait-QaSeconds $c 0.5
            Write-QaInput $c @('right.thumbrest = 1', 'left.stick = 0, 1'); Wait-QaSeconds $c 1.2
            Write-QaInput $c @('right.thumbrest = 1'); Wait-QaSeconds $c 1
            Write-QaInput $c @(); Wait-QaSeconds $c 2
        }
        Asserts = {
            param($c)
            Test-QaPresent $c 'thumb-rest wheel extreme: rest sensors on both hands'
            Test-QaPresent $c 'thumb-rest wheel: armed: the right (turn) stick picks weapons; the left thumb rest gives turning back'
            Test-QaPresent $c 'thumb-rest wheel: armed: the left stick picks (right thumb rest, extreme'
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
    },
    @{
        Name = 'wheel-centre'; Kind = 'game'
        Proves = 'The weapon wheel held open with the stick shows straight ahead: the layer logs the wheel quad and its rectangle, the wheel the game drew lies in that rectangle (GUI captures before the stick and with the wheel open), and it goes back when the wheel closes'
        Env = @()
        UiCaptures = 10
        Input = @()
        Timeline = {
            param($c)
            Wait-QaSeconds $c 12
            $c.WheelDown = Get-Date
            Write-QaInput $c @('right.stick = 0, -1'); Wait-QaSeconds $c 2
            Save-QaShot $c 'wheel'; Save-QaXrShot $c 'wheel'
            $c.WheelUp = Get-Date
            Write-QaInput $c @(); Wait-QaSeconds $c 1.5
            Save-QaXrShot $c 'after'
        }
        Asserts = {
            param($c)
            Test-QaPresent $c 'controllers: forced view starts (weapon wheel'
            Test-QaPresent $c 'ui: weapon wheel straight ahead (panel HUD): GUI rect'
            Test-QaPresent $c 'ui: weapon wheel back on the HUD after'
            Test-QaAbsent $c 'ui: weapon wheel up, but no wheel quad'
            Test-QaWheelRect $c
        }
    }
)

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
$script:QaScenarios = @($script:QaScenarios | Where-Object { $_.Kind -ne 'e2e' }) + $wheelScenarios + $restButtonScenarios + $touchButtonScenarios + $e2e
