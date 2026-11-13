# A game started the way a player starts one (tools/rig/qa/README.md, "Menu-started scenarios"): at the title screen,
# then through the game's own menus to a mission, by the scripted right hand pointing at the menu panel and pulling
# the trigger (docs/VR_MENUS.md), instead of a `+map` load. Dot-sourced by qa-suite.ps1 after qa-common.ps1.
#
# Every step waits on the layer log, never on a fixed time alone. A step takes one mark in the log before its first
# click; its answer is the first line after that mark that matches its own pattern: the main menu's next screen (`mp
# guard: main menu screen N`, logged by the multiplayer guard's menu poll on every change) or the map load. A click is
# made only while the main menu shows the screen the step starts from (`From`) and its answer has not come yet, both
# checked again right before the trigger: a late answer to the click before is never taken for a screen that waits for
# another click. A click is sent once the layer read the input file that points at the item (`test input '...' read`)
# and counts once the router sent the button (`menu: button down`). A step without its answer in its time clicks again
# (Tries: the title 4, Campaign 3, Mission Select and a mission row 2, a slot and Continue Game 1, the steps whose
# click on the screen before would open the wrong slot), then fails the scenario with a shot of the game window and
# what the log showed since the step began.

# The game's frame (Start-QaGame's 1280 x 720 window): the menu cursor's pixels, which the panel shows whole.
$script:QaMenuFrameWidth = 1280
$script:QaMenuFrameHeight = 720
$script:QaMenuPoll = 0.5
$script:QaMenuInputRead = "test input '[^']*' read \("
$script:QaMenuPanelLine = 'menu: the game shows its cursor \([^)]*\): panel ([\d.]+) x ([\d.]+) m at \((-?[\d.]+), (-?[\d.]+), (-?[\d.]+)\)'
$script:QaMenuScreenLine = 'mp guard: main menu screen (-?\d+) '
# qa-suite.ps1 -TestMenuTitleTimeout: the title step's time (one try), to see the environment skip work.
$script:QaMenuTitleTimeout = 0

if (-not ('QaMenuNative' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class QaMenuNative {
    public delegate bool EnumProc(IntPtr h, IntPtr p);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr p);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
    // The game's own window (class Ghost_CLASS): the simulator's preview window in the same process can be the
    // process's main window.
    public static IntPtr GameWindow(uint pid) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((h, p) => {
            uint owner; GetWindowThreadProcessId(h, out owner);
            if (owner != pid || !IsWindowVisible(h)) return true;
            var c = new StringBuilder(64);
            GetClassNameW(h, c, 64);
            if (c.ToString() == "Ghost_CLASS") { found = h; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
'@
}

# The game window's own image (PrintWindow, as launch-ht.ps1 takes it) as <Out>\<scenario>-<checkpoint>-game.png: the
# menu on the screen where it stood, which the virtual display's shot does not always show.
function Save-QaGameWindowShot($Ctx, [string]$Checkpoint) {
    try {
        $p = Get-Process $script:QaGameProcess -ErrorAction Stop | Select-Object -First 1
        $hwnd = [QaMenuNative]::GameWindow([uint32]$p.Id)
        if ($hwnd -eq [IntPtr]::Zero) { Add-QaNote $Ctx "no game window for '$Checkpoint'"; return }
        $rc = New-Object QaNative+RECT
        [void][QaNative]::GetWindowRect($hwnd, [ref]$rc)
        $w = $rc.Right - $rc.Left; $h = $rc.Bottom - $rc.Top
        if ($w -lt 16 -or $h -lt 16) { return }
        $png = Join-Path $Ctx.Out "$($Ctx.Name)-$Checkpoint-game.png"
        $bmp = New-Object Drawing.Bitmap $w, $h
        $g = [Drawing.Graphics]::FromImage($bmp)
        try {
            $hdc = $g.GetHdc()
            try { [void][QaNative]::PrintWindow($hwnd, $hdc, 2) } finally { $g.ReleaseHdc($hdc) }
            $bmp.Save($png, [Drawing.Imaging.ImageFormat]::Png)
        } finally { $g.Dispose(); $bmp.Dispose() }
        [void]$Ctx.Shots.Add($png)
    } catch { Add-QaNote $Ctx "game window shot '$Checkpoint' failed: $($_.Exception.Message)" }
}

# The length of the layer log now: a wait after an action reads only what came after it.
function Get-QaLogMark($Ctx) { return (Get-QaLogText $Ctx).Length }

# The layer log after $Mark ('' when nothing came yet).
function Get-QaLogAfter($Ctx, [int]$Mark) {
    $text = Get-QaLogText $Ctx
    if ($text.Length -le $Mark) { return '' }
    return $text.Substring($Mark)
}

# Waits until the layer log after $Mark matches $Pattern (read every half second); the match, or $null after the timeout.
function Wait-QaLogAfter($Ctx, [int]$Mark, [string]$Pattern, [double]$TimeoutSeconds) {
    $end = (Get-Date).AddSeconds($TimeoutSeconds)
    while ($true) {
        $m = [regex]::Match((Get-QaLogAfter $Ctx $Mark), $Pattern)
        if ($m.Success) { return $m }
        if ((Get-Date) -ge $end) { return $null }
        Wait-QaSeconds $Ctx $script:QaMenuPoll
    }
}

# The main menu's screen the log named last (the guard's poll logs every change), or $null.
function Get-QaMenuScreen([string]$Text) {
    $all = [regex]::Matches($Text, $script:QaMenuScreenLine)
    if ($all.Count -eq 0) { return $null }
    return [int]$all[$all.Count - 1].Groups[1].Value
}

# What the log shows after $Mark, for a failure note: the screens the main menu went to, the cursor's kinds (a popup
# over the game, a menu screen), the buttons sent, and the screen it is on.
function Get-QaMenuState($Ctx, [int]$Mark) {
    $text = Get-QaLogText $Ctx
    $after = if ($text.Length -gt $Mark) { $text.Substring($Mark) } else { '' }
    $screens = @([regex]::Matches($after, $script:QaMenuScreenLine) | ForEach-Object { $_.Groups[1].Value })
    $cursors = @([regex]::Matches($after, 'menu: the game shows its cursor \(([^:)]{0,40})') | ForEach-Object { $_.Groups[1].Value.Trim() })
    $gone = @([regex]::Matches($after, 'menu: the cursor is gone')).Count
    $downs = @([regex]::Matches($after, 'menu: button down')).Count
    $now = Get-QaMenuScreen $text
    return [pscustomobject]@{
        Screens = $screens; Popup = @($cursors | Where-Object { $_ -like 'a popup*' }).Count -gt 0; Downs = $downs
        Text = ('since the step began: screens [{0}], cursor shown {1} time(s){2}, gone {3} time(s), {4} button down(s); the main menu on screen {5}' -f
            ($screens -join ', '), $cursors.Count, $(if ($cursors.Count) { " ('" + ($cursors -join "', '") + "')" } else { '' }), $gone, $downs,
            $(if ($null -ne $now) { $now } else { 'none logged' }))
    }
}

# The menu panel the log placed last (a popup places its own, so the newest line, read for each step and again before
# each retry): width, height and centre (LOCAL metres), or the default panel when none was logged.
function Get-QaMenuPanel($Ctx) {
    $panels = [regex]::Matches((Get-QaLogText $Ctx), $script:QaMenuPanelLine)
    if ($panels.Count -eq 0) { return [pscustomobject]@{ Width = 2.0; Height = 1.125; X = 0.0; Y = 0.0; Z = -1.5 } }
    $g = $panels[$panels.Count - 1].Groups
    $f = { param($s) [double]::Parse($s, [Globalization.CultureInfo]::InvariantCulture) }
    return [pscustomobject]@{ Width = (& $f $g[1].Value); Height = (& $f $g[2].Value); X = (& $f $g[3].Value); Y = (& $f $g[4].Value); Z = (& $f $g[5].Value) }
}

# The input lines that point the right hand, held at the head, at pixel ($X, $Y) of the game's frame on $Panel. The
# simulator's head is still, at LOCAL x 0 and z 0 and at the panel's height (the panel goes in front of the head at the
# UI quad's height, 0.00 m up); the panel faces the head from its centre, so a panel placed off to a side (x) or at
# another distance (z) is followed. The aim is in degrees: yaw to the left, pitch up, as the input file takes it.
function Get-QaMenuAim($Panel, [double]$X, [double]$Y) {
    $metres = $Panel.Width / $script:QaMenuFrameWidth
    $u = ($X - $script:QaMenuFrameWidth / 2) * $metres
    $v = ($script:QaMenuFrameHeight / 2 - $Y) * $metres
    $len = [Math]::Sqrt($Panel.X * $Panel.X + $Panel.Z * $Panel.Z)
    if ($len -lt 0.01) { $len = 1.5; $fx = 0.0; $fz = -1.0 } else { $fx = $Panel.X / $len; $fz = $Panel.Z / $len }
    # The panel's right is the facing direction turned 90 degrees clockwise seen from above: (-fz, fx).
    $dx = $Panel.X - $fz * $u
    $dz = $Panel.Z + $fx * $u
    $yaw = [Math]::Atan2(-$dx, -$dz)
    $pitch = [Math]::Atan2($v, [Math]::Sqrt($dx * $dx + $dz * $dz))
    $aim = [string]::Format([Globalization.CultureInfo]::InvariantCulture, 'right.aim = {0:F3}, {1:F3}',
        $yaw * 180 / [Math]::PI, $pitch * 180 / [Math]::PI)
    return @('right.position = 0, 0, 0', $aim)
}

# Points at ($X, $Y) on $Panel, waits for the layer to read that and gives the router's closed loop half a second to
# bring the game's cursor there (one move, shown within 150 ms). Then, unless $Until already came after $Mark or the
# main menu has left the screen $From (none for a step in a play map), pulls the trigger until the router sent the
# button. 'clicked', 'no button' (the trigger went unsent), 'answered' or 'moved on' (no click made).
function Invoke-QaMenuClick($Ctx, $Panel, [double]$X, [double]$Y, [int]$Mark, [string]$Until, $From) {
    $aim = @(Get-QaMenuAim $Panel $X $Y)
    $read = Get-QaLogMark $Ctx
    Write-QaInput $Ctx $aim
    [void](Wait-QaLogAfter $Ctx $read $script:QaMenuInputRead 3)
    Wait-QaSeconds $Ctx 0.5
    $text = Get-QaLogText $Ctx
    if ($text.Length -gt $Mark -and $text.Substring($Mark) -match $Until) { return 'answered' }
    if ($null -ne $From -and (Get-QaMenuScreen $text) -ne $From) { return 'moved on' }
    $down = $text.Length
    Write-QaInput $Ctx ($aim + 'right.trigger = 1')
    $sent = Wait-QaLogAfter $Ctx $down 'menu: button down' 3
    Wait-QaSeconds $Ctx 0.1
    Write-QaInput $Ctx $aim
    if ($sent) { return 'clicked' } else { return 'no button' }
}

# One step of the menu path ($Step: Name, X, Y, Until, From, Timeout, Tries, Settle): one mark before the first click,
# then up to Tries clicks while the step's own answer has not come and the main menu still shows From, each followed by
# up to Timeout seconds for Until after the mark. Once it is there, Settle seconds for the next screen to take clicks
# (its fade-in). Returns Ok, the match, the mark and the log's state since the mark; a failure goes into the report
# unless $Quiet (the caller decides: the title's failure can be the environment's).
function Invoke-QaMenuStep($Ctx, $Step, [switch]$Quiet) {
    $start = Get-Date
    $mark = Get-QaLogMark $Ctx
    $panel = Get-QaMenuPanel $Ctx
    $tries = if ($Step.Tries) { $Step.Tries } else { 3 }
    $timeout = if ($Step.Timeout) { $Step.Timeout } else { 8 }
    $why = ''
    for ($try = 1; $try -le $tries; $try++) {
        $click = Invoke-QaMenuClick $Ctx $panel $Step.X $Step.Y $mark $Step.Until $Step.From
        # A screen left without the answer (a load under way, a popup) is waited out once more, never clicked into.
        $m = Wait-QaLogAfter $Ctx $mark $Step.Until $timeout
        if ($m) {
            Add-QaResult $Ctx "menu: $($Step.Name)" $true ('{0:N1} s, try {1}{2}: {3}' -f ((Get-Date) - $start).TotalSeconds, $try,
                $(if ($click -eq 'answered') { ' (answered before its click)' } else { '' }), $m.Value.Trim())
            if ($Step.Settle) { Wait-QaSeconds $Ctx $Step.Settle }
            return [pscustomobject]@{ Ok = $true; Match = $m; Mark = $mark; State = $null }
        }
        Write-QaLine ("  menu: $($Step.Name), try {0}: no '{1}' in {2} s ({3})" -f $try, $Step.Until, $timeout, $click)
        Save-QaGameWindowShot $Ctx "menu-$($Step.Name -replace ' ', '-')-try$try"
        if ($click -eq 'moved on') { $why = "the main menu left screen $($Step.From) without the answer: no more clicks"; break }
        $panel = Get-QaMenuPanel $Ctx
    }
    $state = Get-QaMenuState $Ctx $mark
    $detail = "no '{0}' within {1} s of each click at ({2}, {3}){4}; {5}" -f $Step.Until, $timeout, $Step.X, $Step.Y,
        $(if ($why) { "; $why" } else { '' }), $state.Text
    if (-not $Quiet) { Add-QaResult $Ctx "menu: $($Step.Name)" $false $detail }
    return [pscustomobject]@{ Ok = $false; Match = $null; Mark = $mark; State = $state; Detail = $detail }
}

# The path from the title screen to a slot's campaign menu, measured on the rig (build 25216728, the 1280 x 720 frame,
# 2026-11-12): each item's pixel, the screen it is clicked on (From), the line its click leads to and the seconds that
# line may take. The title's click connects to Bethesda.net first (6 to 10 s; it needs the network and the account's
# sign-in) and opens the main menu (screen 3); Campaign opens the save slots (screen 13). The save slots and the slot
# menus share Campaign's buttons and rows, so no click point is inert on the screen after its own: the clicks are gated
# on the screen instead (above).
$script:QaMenuPath = @(
    @{ Name = 'title'; X = 640; Y = 440; From = 2; Until = 'mp guard: main menu screen 3 '; Timeout = 15; Tries = 4 }
    @{ Name = 'campaign'; X = 179; Y = 197; From = 3; Until = 'mp guard: main menu screen 13 '; Timeout = 8; Tries = 3; Settle = 0.5 }
)
# The slots' rows on the save slots screen and the slot menu's rows (Continue Game first, Mission Select second).
$script:QaMenuRowX = 134
$script:QaMenuRowY = 157
$script:QaMenuRowStep = 34

# Mission Select's list (all 13 missions on one page, in the campaign's order): the row's pixel and the map it loads.
$script:QaMenuMissions = [ordered]@{}
$rows = 'e1m1_intro', 'e1m2_battle', 'e1m3_cult', 'e1m4_boss', 'e2m1_nest', 'e2m2_base', 'e2m3_core', 'e2m4_boss',
    'e3m1_slayer', 'e3m2_hell', 'e3m2_hell_b', 'e3m3_maykr', 'e3m4_boss'
for ($i = 0; $i -lt $rows.Count; $i++) {
    $script:QaMenuMissions[$rows[$i]] = @{ X = $script:QaMenuRowX; Y = $script:QaMenuRowY + $script:QaMenuRowStep * $i; Map = "game/sp/$($rows[$i])/$($rows[$i])" }
}

# From the title screen to the map load of $Scenario.MenuMission: a key of $script:QaMenuMissions (Mission Select's
# mission), or 'continue' (the slot's Continue Game, whose map $Scenario.MenuMap names, with $Scenario.MenuSaveNote
# saying what the slot must hold), in the slot $Scenario.MenuSlot (1 to 3, default 1). Returns 'ok' once that map loaded,
# 'skip' when the title's click was sent and the main menu never came (the environment: Bethesda.net's connection or
# sign-in, or a popup over the title; the reason in $Ctx.SkipReason), 'fail' otherwise. Each step is a line in the
# report (`menu: <step>` with its seconds); the input file is the scenario's own again afterwards.
#
# The slot step's answer is its own `menu: button down`: the slot menu has no screen of its own, and the line does not
# say which button the game took (unverified there). What the next step checks instead: Mission Select's screen 14 only
# comes from a slot's menu, and the map load must be the one asked for. A slot click the game did not take leaves the
# save slots up, where Mission Select's point is slot 2's row and Continue Game's is slot 1's: those steps click once,
# so such a slip opens that slot's menu and fails there, never a second click into the wrong slot's save.
function Invoke-QaMenuMission($Ctx, $Scenario) {
    $slot = if ($Scenario.MenuSlot) { [int]$Scenario.MenuSlot } else { 1 }
    if ($Scenario.MenuMission -eq 'continue') {
        $target = @{ Name = 'continue game'; X = $script:QaMenuRowX; Y = $script:QaMenuRowY; Map = $Scenario.MenuMap; From = 13; Tries = 1 }
    } else {
        $m = $script:QaMenuMissions[$Scenario.MenuMission]
        $target = if ($m) { @{ Name = "mission $($Scenario.MenuMission)"; X = $m.X; Y = $m.Y; Map = $m.Map; From = 14; Tries = 2 } } else { $null }
    }
    if (-not $target -or -not $target.Map -or $slot -lt 1 -or $slot -gt 3) {
        Add-QaResult $Ctx 'menu: the mission is on the menus' $false "MenuMission '$($Scenario.MenuMission)', MenuSlot $slot, MenuMap '$($Scenario.MenuMap)'"
        return 'fail'
    }
    $path = @($script:QaMenuPath | ForEach-Object { $_.Clone() })
    if ($script:QaMenuTitleTimeout -gt 0) { $path[0].Timeout = $script:QaMenuTitleTimeout; $path[0].Tries = 1 }
    $path += @{ Name = "game slot $slot"; X = $script:QaMenuRowX; Y = $script:QaMenuRowY + $script:QaMenuRowStep * ($slot - 1); From = 13
                Until = 'menu: button down'; Timeout = 3; Tries = 1; Settle = 1 }
    if ($Scenario.MenuMission -ne 'continue') {
        $path += @{ Name = 'mission select'; X = $script:QaMenuRowX + 8; Y = $script:QaMenuRowY + $script:QaMenuRowStep; From = 13
                    Until = 'mp guard: main menu screen 14 '; Timeout = 8; Tries = 1; Settle = 0.5 }
    }
    # The title screen's cursor may have come before launch-ht.ps1 returned: the whole log counts.
    $start = Get-Date
    $title = Wait-QaLogAfter $Ctx 0 $script:QaMenuPanelLine 120
    Add-QaResult $Ctx 'menu: title screen up' ($null -ne $title) $(if ($title) { ('{0:N1} s after the launch returned: {1}' -f ((Get-Date) - $start).TotalSeconds, ($title.Value -replace '^menu: ', '')) } else { 'no menu cursor within 120 s' })
    if (-not $title) { Save-QaGameWindowShot $Ctx 'menu-title'; Write-QaInput $Ctx @($Scenario.Input | Where-Object { $_ }); return 'fail' }
    $result = 'ok'
    foreach ($s in $path) {
        $isTitle = $s.Name -eq 'title'
        $r = Invoke-QaMenuStep $Ctx $s -Quiet:$isTitle
        if ($r.Ok) { continue }
        Save-QaGameWindowShot $Ctx "menu-$($s.Name -replace ' ', '-')"
        if ($isTitle -and $r.State.Downs -gt 0) {
            # The title took a click and the main menu never came: Bethesda.net's connection or sign-in, or a popup.
            $Ctx.SkipReason = 'the title screen took the click and the main menu never came (Bethesda.net connection or sign-in{0}); {1}' -f
                $(if ($r.State.Popup) { ', a popup over the title' } else { '' }), $r.Detail
            Add-QaNote $Ctx "menu: title: $($Ctx.SkipReason)"
            $result = 'skip'
        } else {
            if ($isTitle) { Add-QaResult $Ctx 'menu: title' $false $r.Detail }
            $result = 'fail'
        }
        break
    }
    if ($result -eq 'ok') {
        $step = @{ Name = $target.Name; X = $target.X; Y = $target.Y; From = $target.From; Until = "mp guard: map load '([^']+)'"; Timeout = 15; Tries = $target.Tries }
        $r = Invoke-QaMenuStep $Ctx $step -Quiet
        if (-not $r.Ok) {
            Save-QaGameWindowShot $Ctx "menu-$($target.Name -replace ' ', '-')"
            Add-QaResult $Ctx "menu: $($target.Name) loads $($target.Map)" $false $r.Detail
            $result = 'fail'
        } elseif ($r.Match.Groups[1].Value -ne $target.Map) {
            $what = if ($Scenario.MenuMission -eq 'continue') { "Game Slot $slot does not hold $($Scenario.MenuSaveNote)" } else { 'Mission Select loaded another mission' }
            Add-QaResult $Ctx "menu: $($target.Name) loads $($target.Map)" $false "it loaded '$($r.Match.Groups[1].Value)': $what"
            $result = 'fail'
        } else {
            Add-QaResult $Ctx "menu: $($target.Name) loads $($target.Map)" $true ''
        }
    }
    Write-QaInput $Ctx @($Scenario.Input | Where-Object { $_ })
    return $result
}
