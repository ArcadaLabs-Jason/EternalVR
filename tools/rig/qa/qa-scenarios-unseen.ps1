# The low hold while nobody can see the game's frames (T-228, docs/VR_STEREO.md "Frame pacing", While nobody looks).
# Dot-sourced by qa-suite.ps1 after qa-scenarios-fit.ps1: added to $script:QaScenarios before the launcher's
# end-to-end suite.
#
# OpenXR-Simulator has no user presence and never goes SYNCHRONIZED; its session is FOCUSED while its preview window
# is active and VISIBLE otherwise, from its window procedure's WM_ACTIVATE (its log: `WndProc: WM_ACTIVATE ->
# focused` / `unfocused`). ETERNALVR_TEST_UNSEEN_VISIBLE=1 takes VISIBLE after focus as nobody looking, so that
# message drives the same hold a headset taken off drives on a headset runtime: the session's state events, the hold
# and its release are the real ones. The scenario posts WM_ACTIVATE to the simulator's window itself instead of moving
# the desktop's foreground: an unelevated QA process cannot take the foreground from another process's window (on
# the RTX 3080 Ti test PC a Virtual Desktop Streamer dialog held it, and SetForegroundWindow with the AttachThreadInput trick was
# refused in both rig runs of 2026-11-07), and a message to a window of the same integrity level always arrives.
#
#   unseen-hold  in play, headset pacing: WM_ACTIVATE active, then inactive for 25 s (the low hold), then active
#                again: the hold starts within a second of VISIBLE and ends within a second of FOCUSED, 20 s or more
#                held at 8.5 images a second or fewer (summed over its end lines, so a stray focus change in between
#                does not fail it), a 10 s pace line and a whole 10 s rates window inside a hold at that rate, no
#                stall line and no hold switch change in it, 30 images or more in the first second after it and a
#                whole rates window after it at 30 pairs a second or more

if (-not ('QaSimActivate' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class QaSimActivate {
    [DllImport("user32.dll")] static extern bool PostMessageW(IntPtr h, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] static extern bool IsWindow(IntPtr h);
    const uint WM_ACTIVATE = 0x0006;
    // WA_ACTIVE (1) or WA_INACTIVE (0) to the window's own procedure, as Windows sends it on an activation change.
    public static bool Post(IntPtr h, bool active) {
        return h != IntPtr.Zero && IsWindow(h) && PostMessageW(h, WM_ACTIVATE, new IntPtr(active ? 1 : 0), IntPtr.Zero);
    }
}
'@
}

$script:QaUnseenStart = "pace: nobody can see the game's frames (the session is VISIBLE, taken as nobody looking"
$script:QaUnseenEnd = 'pace: the low hold ends after ([\d.]+) s \((\d+) image\(s\) handed over, ([\d.]+) a second\)'
$script:QaUnseenAfter = 'pace: in the ([\d.]+) s after the low hold the game handed (\d+) image\(s\) to the headset'
$script:QaUnseenRates = 'rates: game ([\d.]+) present\(s\)/s, ([\d.]+) tick\(s\)/s, ([\d.]+) stereo pair\(s\)/s shown'
# The hold switch's three change lines (frame_pacing.cpp onHeadsetFrame).
$script:QaUnseenSwitch = '^pace: (the game falls behind the headset: let go|the game keeps up with the headset: held again|a menu or a loading screen: held again)'
$script:QaUnseenMaxRate = 8.5  # the hold's 8 images a second, and a little for the windows' edges
$script:QaUnseenMinRate = 30.0 # the game at its own rate again (the rig: about 90 a second, paced)

# The layer log's lines with their time stamps ([  120.000] [pid] text).
function Get-QaTimedLines($Ctx) {
    $out = New-Object System.Collections.ArrayList
    foreach ($line in ((Get-QaLogText $Ctx) -split "`r?`n")) {
        if ($line -match '^\[ *([\d.]+)\] \[ *\d+\] (.*)$') {
            [void]$out.Add([pscustomobject]@{ T = [double]$Matches[1]; Text = $Matches[2] })
        }
    }
    return $out
}

# The last session state the layer logged (0: none yet).
function Get-QaSessionState($Ctx) {
    $last = @(Get-QaMatches $Ctx 'xr: session state \d+$' -Regex) | Select-Object -Last 1
    if ($last -and $last.Trim() -match 'session state (\d+)$') { return [int]$Matches[1] }
    return 0
}

# Posts WM_ACTIVATE to the simulator's window ('sim': active, 'away': inactive) and waits until the last session
# state logged is the one the simulator gives it (FOCUSED 5, VISIBLE 4). Returns the layer log's last time stamp
# before the post.
function Set-QaSimFocus($Ctx, [string]$Where) {
    $state = if ($Where -eq 'sim') { 5 } else { 4 }
    $before = (Get-QaTimedLines $Ctx | Select-Object -Last 1).T
    $hwnd = [QaNative]::FindByTitlePrefix('OpenXR Simulator')
    $posted = [QaSimActivate]::Post($hwnd, $Where -eq 'sim')
    $end = (Get-Date).AddSeconds(5)
    while ((Get-QaSessionState $Ctx) -ne $state -and (Get-Date) -lt $end) { Wait-QaSeconds $Ctx 0.25 }
    $now = Get-QaSessionState $Ctx
    Add-QaResult $Ctx "simulator window $(if ($Where -eq 'sim') { 'active' } else { 'inactive' }): session state $state" ($now -eq $state) `
        "WM_ACTIVATE posted $posted to window $hwnd, last state logged $now"
    return $before
}

# The rates lines whose 10 s window lies inside [$From, $To] (a line's window runs from the line before it).
function Get-QaRatesWindows($Lines, [double]$From, [double]$To) {
    $prev = $null
    $windows = @()
    foreach ($l in $Lines) {
        if ($l.Text -notmatch $script:QaUnseenRates) { continue }
        $w = [pscustomobject]@{ From = $(if ($prev) { $prev.T } else { 0.0 }); To = $l.T; Ticks = [double]$Matches[2]; Pairs = [double]$Matches[3] }
        $prev = $l
        if ($w.From -ge $From -and $w.To -le $To) { $windows += $w }
    }
    return $windows
}

# The low holds that started from $From on, each with its end line (or none): start, end, seconds, images.
function Get-QaLowHolds($Lines, [double]$From) {
    $holds = @()
    $open = $null
    foreach ($l in $Lines) {
        if ($l.T -lt $From) { continue }
        if ($l.Text.StartsWith($script:QaUnseenStart)) { $open = $l; continue }
        if ($open -and $l.Text -match $script:QaUnseenEnd) {
            $holds += [pscustomobject]@{ Start = $open.T; End = $l.T; Seconds = [double]$Matches[1]; Images = [int]$Matches[2] }
            $open = $null
        }
    }
    if ($open) { $holds += [pscustomobject]@{ Start = $open.T; End = $null; Seconds = 0.0; Images = 0 } }
    return $holds
}

$unseenScenarios = @(
    @{
        Name = 'unseen-hold'; Kind = 'game'
        Proves = 'With nobody looking (the simulator''s VISIBLE taken so by ETERNALVR_TEST_UNSEEN_VISIBLE=1, WM_ACTIVATE inactive posted to its window) the game is held to 8 images a second within a second of the state change, without stall lines or hold switch changes, shown in the 10 s lines, and released within a second of focus coming back, back at its own rate in the first second'
        Env = @('ETERNALVR_PACE=headset', 'ETERNALVR_TEST_UNSEEN_VISIBLE=1')
        Input = @()
        Timeline = {
            param($c)
            Wait-QaSeconds $c 8
            [void](Set-QaSimFocus $c 'sim')
            Wait-QaSeconds $c 12
            # The checks start here: a hold earlier in the run (the window's focus moved by something else) is not
            # this one.
            $c.AwayAt = Set-QaSimFocus $c 'away'
            Wait-QaSeconds $c 25
            Save-QaShot $c 'held'
            $c.BackAt = Set-QaSimFocus $c 'sim'
            [void](Wait-QaLog $c $script:QaUnseenAfter 10)
            # A whole 10 s rates window after the release.
            Wait-QaSeconds $c 22
            Save-QaShot $c 'released'
        }
        Asserts = {
            param($c)
            $presence = @(Get-QaMatches $c 'xr: user presence: ') | Select-Object -First 1
            Add-QaNote $c "user presence: $(if ($presence) { ($presence.Trim() -replace '^.*xr: user presence: ', '') } else { 'no line' })"
            Test-QaPresent $c 'pace: ETERNALVR_PACE=headset'
            if ($null -eq $c.AwayAt -or $null -eq $c.BackAt) { Add-QaResult $c 'the timeline posted both changes' $false ''; return }
            $away = [double]$c.AwayAt; $back = [double]$c.BackAt
            $lines = Get-QaTimedLines $c
            $holds = @(Get-QaLowHolds $lines $away)
            $first = $holds | Select-Object -First 1
            Add-QaResult $c 'the low hold started' ($null -ne $first -and $first.Start -le $back) $(if ($first) { "at $($first.Start) s" } else { "no '$script:QaUnseenStart' line" })
            if (-not $first) { return }
            $last = $holds | Where-Object { $null -ne $_.End -and $_.End -ge $back } | Select-Object -First 1
            Add-QaResult $c 'the low hold ended after focus came back' ($null -ne $last) $(if ($last) { "at $($last.End) s" } else { 'no end line after the focus came back' })
            if (-not $last) { return }
            $during = @($holds | Where-Object { $null -ne $_.End -and $_.End -le $last.End })
            if ($during.Count -gt 1) { Add-QaNote $c "$($during.Count) low holds in the away stretch (a focus change in between)" }
            $seconds = ($during | Measure-Object Seconds -Sum).Sum
            $images = ($during | Measure-Object Images -Sum).Sum
            $rate = if ($seconds -gt 0) { $images / $seconds } else { 0.0 }
            Add-QaResult $c 'held 20 s or more' ($seconds -ge 20.0) ('{0:N1} s in {1} hold(s)' -f $seconds, $during.Count)
            Add-QaResult $c "held at $script:QaUnseenMaxRate images a second or fewer" ($rate -le $script:QaUnseenMaxRate) ('{0} image(s), {1:N2} a second' -f $images, $rate)
            # Within a second of the session states the posts gave.
            $visible = $lines | Where-Object { $_.T -ge $away -and $_.Text -eq 'xr: session state 4' } | Select-Object -First 1
            $focus = $lines | Where-Object { $_.T -ge $back -and $_.Text -eq 'xr: session state 5' } | Select-Object -First 1
            Add-QaResult $c 'the hold started within 1 s of VISIBLE' ($visible -and [Math]::Abs($first.Start - $visible.T) -le 1.0) $(if ($visible) { '{0:N3} s' -f ($first.Start - $visible.T) } else { 'no state 4 after the post' })
            Add-QaResult $c 'the hold ended within 1 s of FOCUSED' ($focus -and [Math]::Abs($last.End - $focus.T) -le 1.0) $(if ($focus) { '{0:N3} s' -f ($last.End - $focus.T) } else { 'no state 5 after the post' })
            $after = $lines | Where-Object { $_.T -ge $last.End -and $_.Text -match $script:QaUnseenAfter } | Select-Object -First 1
            if ($after) { [void]($after.Text -match $script:QaUnseenAfter) }
            $afterSeconds = if ($after) { [double]$Matches[1] } else { 0.0 }
            $afterImages = if ($after) { [int]$Matches[2] } else { 0 }
            Add-QaResult $c 'back at its own rate within a second' ($after -and $afterSeconds -le 1.5 -and $afterImages -ge $script:QaUnseenMinRate) `
                $(if ($after) { "$afterImages image(s) in $afterSeconds s" } else { 'no line after the hold' })
            # The 10 s lines inside a hold: the pace line names it, a rates window shows the rate.
            $inHold = { param($t) @($during | Where-Object { $t -gt $_.Start -and $t -le $_.End }).Count -gt 0 }
            $paced = @($lines | Where-Object { (& $inHold $_.T) -and $_.Text -match '^pace: held low for \d+ s so far \(the session is VISIBLE, taken as nobody looking' })
            Add-QaResult $c 'a 10 s pace line names the low hold' ($paced.Count -ge 1) "$($paced.Count) line(s)"
            $inside = @($during | ForEach-Object { Get-QaRatesWindows $lines $_.Start $_.End })
            Add-QaResult $c "a 10 s rates window inside the hold at $script:QaUnseenMaxRate pairs and ticks a second or fewer" `
                ($inside.Count -ge 1 -and @($inside | Where-Object { $_.Pairs -gt $script:QaUnseenMaxRate -or $_.Ticks -gt $script:QaUnseenMaxRate }).Count -eq 0) `
                (($inside | ForEach-Object { "$($_.Pairs) pairs, $($_.Ticks) ticks" }) -join '; ')
            $later = @(Get-QaRatesWindows $lines $last.End ([double]::MaxValue)) | Select-Object -First 1
            Add-QaResult $c "a 10 s rates window after it at $script:QaUnseenMinRate pairs a second or more" ($later -and $later.Pairs -ge $script:QaUnseenMinRate) `
                $(if ($later) { "$($later.Pairs) pairs, $($later.Ticks) ticks" } else { 'no whole window after the hold' })
            # The hold is not a stall, and the hold switch decides nothing through it.
            $stalls = @($lines | Where-Object { (& $inHold $_.T) -and $_.Text.StartsWith('stall: ') -and $_.Text -match 'between game presents' })
            Add-QaResult $c 'no stall line in the hold' ($stalls.Count -eq 0) $(if ($stalls.Count) { $stalls[0].Text } else { '' })
            $switched = @($lines | Where-Object { $_.T -gt $first.Start -and $_.T -le ($last.End + 2.0) -and $_.Text -match $script:QaUnseenSwitch })
            Add-QaResult $c 'no hold switch change in the hold' ($switched.Count -eq 0) $(if ($switched.Count) { $switched[0].Text } else { '' })
        }
    }
)

# Before the launcher's end-to-end suite, which stays last.
$e2e = @($script:QaScenarios | Where-Object { $_.Kind -eq 'e2e' })
$script:QaScenarios = @($script:QaScenarios | Where-Object { $_.Kind -ne 'e2e' }) + $unseenScenarios + $e2e
