# The Super Shotgun's Meat Hook under each aim (docs/VR_CONTROLLERS.md, "The Meat Hook"). The game takes the hook's
# target from its own view (the player's view angles) within the hook's cone and range, and draws its lock-on mark on
# the target with the HUD's world-attached markers; the layer logs the target as the game picks it
# (vkcore/meathook_watch.cpp: `meat hook: target <decl> (its origin at yaw Y pitch P, D m) (new), hooked <decl>, ...;
# the game's view pitch P yaw Y`). By name only (-Only), like the glory kills it borrows its start from. Dot-sourced
# by qa-suite.ps1 after qa-scenarios-takeover.ps1, whose e1m1 barge wave it uses.
#
# The run: e1m1's barge with its first encounter started (qa-scenarios-takeover.ps1), god, everything given (`give
# all`: the Super Shotgun with its Meat Hook), the demons held where they are put (`ai_freezeMovement 1`), the Super
# Shotgun up (weapon_slot_5 on a touch of A). Two trials, each from the barge's door (`setviewpos`): a zombie brought
# onto the floor about 9.5 m ahead along the game's view (`nextActiveAI;ai_teleportToPlayer trace`, the hand 10
# degrees down), the hand then raised to its body (3 degrees down), the hook pressed (the right grip, weapon_mod), the
# zombie removed after (`removeAI`). The simulator's head stays still, looking along the body.
#
#   meathook-hand   hand aim (the launcher's default). Trial 'hand': the hand 20 degrees left of the head when the
#                   zombie is brought and after, with tracking noise on it (0.5 mm and 0.5 degrees each frame), so the
#                   zombie stands where the gun points and 20 degrees off where the head looks: the game takes it as
#                   the target (its view on the gun's line, the head 20 degrees away) within 0.3 s of the hand
#                   reaching it, keeps it to the press despite the noise, and the hook connects (`hooked`). Trial
#                   'head': the zombie brought straight ahead, the hand then 25 degrees left of it for 1.2 s: no
#                   target in the head's direction meanwhile (the head looks at the zombie, the gun away); the hand
#                   back on it: the target within 0.3 s, the hook connects. The lock-on mark: the run's first HUD
#                   marker on its own quad in the same 0.1 s as its first target.
#   meathook-head   the same under head aim, the control: the zombie is brought in front of the head and the game
#                   takes it as the target whatever the hand does (20 or 25 degrees away), and the hook connects on
#                   each press.

$script:QaHookDoor = 'setviewpos 36 -1590 18.75 90'
# Each trial's clock: the player back at the door, a zombie brought 1 s later, removed 12 s after that.
$script:QaHookTrialAt = @(40, 60)
$script:QaHookSchedule = ((@(($script:QaGloryWave -f 20, 21, 24, 25)) + '27:give all' + '30:ai_freezeMovement 1' +
    @($script:QaHookTrialAt | ForEach-Object { '{0}:{1}|{2}:nextActiveAI;ai_teleportToPlayer trace|{3}:removeAI' -f $_, $script:QaHookDoor, ($_ + 1), ($_ + 13) })) -join '|')
$script:QaHookRest = 'right.position = 0.2, -0.3, -0.35'
# The Super Shotgun on a touch of A (weapon_slot_5, where the game's wheel has it).
$script:QaHookSlot = '"right.primary_touch.tap" = "weapon_slot_5"'
# The hand's pitch when the zombie is brought (the trace puts it on the floor about 9.5 m ahead), and on its body.
$script:QaHookPlacePitch = -10
$script:QaHookBodyPitch = -3
$script:QaHookNoise = 'right.noise = 0.5, 0.5'
# The game's target follows the hand: the hand's yaw left of the head in each step.
$script:QaHookTrials = @(
    @{ Name = 'hand'; Place = 20; Steps = @(,@{ Mark = 'hand-on'; Yaw = 20; Seconds = 1.2; Extra = @($script:QaHookNoise) }) },
    @{ Name = 'head'; Place = 0; Steps = @(@{ Mark = 'head-away'; Yaw = 25; Seconds = 1.2 }, @{ Mark = 'head-on'; Yaw = 0; Seconds = 1.2 }) })
# A target taken this soon after the hand's input is read counts as at once (a few game frames).
$script:QaHookPromptSeconds = 0.3
# Off by more than this from the game's view, a target is not where the view points (the cone is about 10 degrees
# across, the zombie's points spread about it).
$script:QaHookOnView = 10

$script:QaHookTimeline = {
    param($c)
    $c['HookMarks'] = @{}
    $rest = $script:QaHookRest
    $p0 = $script:QaHookPlacePitch
    Write-QaInput $c @("right.aim = 0, $p0", $rest)
    if (-not (Wait-QaLog $c 'debug-commands: 27\.0 s: give all' 120)) { Add-QaNote $c 'no give all line' }
    Wait-QaSeconds $c 2
    Write-QaInput $c @("right.aim = 0, $p0", $rest, 'right.primary_touch = 1'); Wait-QaSeconds $c 0.4
    Write-QaInput $c @("right.aim = 0, $p0", $rest); Wait-QaSeconds $c 3
    for ($i = 0; $i -lt $script:QaHookTrials.Count; $i++) {
        $t = $script:QaHookTrials[$i]
        $at = $script:QaHookTrialAt[$i] + 1
        Write-QaInput $c @("right.aim = $($t.Place), $p0", $rest)
        if (-not (Wait-QaLog $c ('debug-commands: {0}\.0 s: nextActiveAI' -f $at) 120)) { Add-QaNote $c "trial $($t.Name): the zombie at $at s was not logged"; continue }
        Wait-QaSeconds $c 0.5
        $lines = @()
        foreach ($s in $t.Steps) {
            $lines = @("right.aim = $($s.Yaw), $script:QaHookBodyPitch", $rest) + @($s.Extra | Where-Object { $_ })
            $c.HookMarks[$s.Mark] = Get-Date
            Write-QaInput $c $lines; Wait-QaSeconds $c $s.Seconds
        }
        Save-QaXrShot $c "$($t.Name)-aim"
        $c.HookMarks["$($t.Name)-press"] = Get-Date
        Write-QaInput $c ($lines + 'right.grip = 1'); Wait-QaSeconds $c 0.3
        Write-QaInput $c $lines; Wait-QaSeconds $c 0.6
        Save-QaXrShot $c "$($t.Name)-pull"
        Wait-QaSeconds $c 3
        Write-QaInput $c @("right.aim = 0, $p0", $rest)
    }
    Wait-QaSeconds $c 11
}

$script:QaHookLine = '^\[\s*([\d.]+)\] \[\s*\d+\] meat hook: target (none|\S+)(?: \(its origin at yaw (-?[\d.]+) pitch (-?[\d.]+), ([\d.]+) m\))?( \(new\))?, hooked (none|\S+), available (yes|no) \(the last state held [\d.]+ s\); the game''s view pitch (-?[\d.]+) yaw (-?[\d.]+)'

# The probe's lines as objects: T, Target ('' for none), OriginYaw (NaN without), New, Hooked ('' for none), ViewYaw.
function Get-QaHookLines($Ctx) {
    $out = New-Object System.Collections.ArrayList
    foreach ($m in [regex]::Matches((Get-QaLogText $Ctx), $script:QaHookLine, 'Multiline')) {
        $g = $m.Groups
        [void]$out.Add([pscustomobject]@{ T = [double]$g[1].Value; Target = $(if ($g[2].Value -eq 'none') { '' } else { $g[2].Value })
            OriginYaw = $(if ($g[3].Success) { [double]$g[3].Value } else { [double]::NaN }); New = $g[6].Success
            Hooked = $(if ($g[7].Value -eq 'none') { '' } else { $g[7].Value }); ViewYaw = [double]$g[10].Value })
    }
    return $out
}

# A wall-clock mark of the timeline as layer-log seconds: the first `controllers: test input ... read` line after it
# (the layer's own time for the input it marks), or NaN.
function Get-QaHookMarkTime($Ctx, [string]$Mark) {
    $at = $Ctx.HookMarks[$Mark]
    if (-not $at) { return [double]::NaN }
    $text = Get-QaLogText $Ctx
    if ($text -notmatch 'log start (\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d+) local') { return [double]::NaN }
    $since = ($at - [datetime]::ParseExact($Matches[1], 'yyyy-MM-dd HH:mm:ss.fff', $null)).TotalSeconds
    foreach ($m in [regex]::Matches($text, '^\[\s*([\d.]+)\] \[\s*\d+\] controllers: test input ''[^'']*'' read', 'Multiline')) {
        $t = [double]$m.Groups[1].Value
        if ($t -ge $since - 0.05) { return $t }
    }
    return [double]::NaN
}

function Get-QaHookOff([double]$A, [double]$B) { return [Math]::Abs((Get-QaTqWrap ($A - $B))) }

# A step's window (from its mark to the next one): the first new target in it within the game's view, the time it
# took from the step's input, whether it held to the window's end, and whether a hook connected after the press.
function Measure-QaHookStep($Ctx, $Lines, [string]$Mark, [string]$Until, [string]$Press) {
    $from = Get-QaHookMarkTime $Ctx $Mark
    $to = Get-QaHookMarkTime $Ctx $Until
    $r = [ordered]@{ From = $from; Target = $null; Delay = [double]::NaN; Held = $false; Hooked = $null }
    if ([double]::IsNaN($from) -or [double]::IsNaN($to)) { return [pscustomobject]$r }
    $span = @($Lines | Where-Object { $_.T -ge $from -and $_.T -le $to })
    $r.Target = $span | Where-Object { $_.Target -and $_.New -and (Get-QaHookOff $_.OriginYaw $_.ViewYaw) -le $script:QaHookOnView } | Select-Object -First 1
    if ($r.Target) {
        $r.Delay = $r.Target.T - $from
        $r.Held = -not ($span | Where-Object { $_.T -gt $r.Target.T -and -not $_.Target })
    }
    if ($Press) {
        $p = Get-QaHookMarkTime $Ctx $Press
        if (-not [double]::IsNaN($p)) { $r.Hooked = $Lines | Where-Object { $_.Hooked -and $_.T -ge $p -and $_.T -le $p + 1.5 } | Select-Object -First 1 }
    }
    return [pscustomobject]$r
}

function Format-QaHookStep($s) {
    if (-not $s.Target) { return 'no new target in the game''s view in the step; hook {0}' -f $(if ($s.Hooked) { "connected to $($s.Hooked.Hooked)" } else { 'not connected' }) }
    return ('{0} at yaw {1:N1} (the game''s view {2:N1}), {3:N2} s after the hand''s input{4}; hook {5}' -f $s.Target.Target, $s.Target.OriginYaw, $s.Target.ViewYaw,
        $s.Delay, $(if ($s.Held) { ', held to the press' } else { ', lost before the press' }), $(if ($s.Hooked) { "connected to $($s.Hooked.Hooked)" } else { 'not connected' }))
}

function New-QaHookScenario([string]$Name, [string]$Aim, [string]$Proves, [scriptblock]$Checks) {
    return @{
        Name = $Name; Kind = 'game'; OnlyByName = $true; Aim = $Aim; Proves = $Proves
        Map = 'game/sp/e1m1_intro/e1m1_intro'
        Env = @("ETERNALVR_AIM=$Aim", 'ETERNALVR_HOLD_SECONDS=1', "ETERNALVR_DEBUG_COMMANDS=$script:QaHookSchedule")
        Prepare = { param($c) New-QaRestControls $c $script:QaHookSlot }
        Input = @("right.aim = 0, $script:QaHookPlacePitch", $script:QaHookRest)
        Timeline = $script:QaHookTimeline
        HookChecks = $Checks
        Asserts = {
            param($c)
            Test-QaPresent $c "held item 'weapon/player/double_barrel' \(mods '[^']*', 'weapon/player/double_barrel_meat_hook'\)" -Regex
            Test-QaPresent $c 'meat hook: the Super Shotgun''s hook takes targets within [\d.]+ x [\d.]+ deg of the game''s view \(horizontal x vertical\) and [\d.]+ m' -Regex
            $lines = @(Get-QaHookLines $c)
            Add-QaNote $c ('{0} meat hook line(s)' -f $lines.Count)
            # The lock-on mark: the HUD's first marker on its own quad comes with the first target (the barge holds no
            # other marker).
            $first = $lines | Where-Object { $_.Target } | Select-Object -First 1
            $mark = @(Get-QaTqEvents $c 'ui: first HUD markers on their own quads') | Select-Object -First 1
            Add-QaResult $c 'the lock-on mark: the first HUD marker on its own quad within 0.1 s of the first meat hook target' ($first -and $mark -and [Math]::Abs($mark.T - $first.T) -le 0.1) $(if ($first -and $mark) { 'target at {0:N3} s, marker at {1:N3} s' -f $first.T, $mark.T } else { "target $([bool]$first), marker $([bool]$mark)" })
            & $c.Scenario.HookChecks $c $lines
        }
    }
}

$hookScenarios = @(
    (New-QaHookScenario 'meathook-hand' 'hand' 'The Meat Hook under hand aim (the default): the game takes its target where the gun points, not where the head looks, at once and through the hand''s noise, shows its lock-on mark with it, and the hook connects' {
        param($c, $lines)
        $hand = Measure-QaHookStep $c $lines 'hand-on' 'hand-press' 'hand-press'
        Add-QaResult $c 'hand: the zombie 20 deg off the head, on the gun: taken as the target within 0.3 s of the hand''s input, held to the press through the hand''s noise, and the hook connects' ($hand.Target -and $hand.Delay -le $script:QaHookPromptSeconds -and $hand.Held -and $hand.Hooked) (Format-QaHookStep $hand)
        # The head looks at the zombie, the gun 25 degrees left: nothing taken in the head's direction (the game's view
        # less the hand's 25 degrees).
        $from = Get-QaHookMarkTime $c 'head-away'; $to = Get-QaHookMarkTime $c 'head-on'
        $atHead = @($lines | Where-Object { $_.T -ge $from -and $_.T -le $to -and $_.Target -and $_.New -and (Get-QaHookOff $_.OriginYaw ($_.ViewYaw - 25)) -le $script:QaHookOnView })
        Add-QaResult $c 'head: the head on the zombie, the gun 25 deg left: no target in the head''s direction' (-not [double]::IsNaN($from) -and -not [double]::IsNaN($to) -and $atHead.Count -eq 0) ('{0} target line(s) in the head''s direction from {1:N2} to {2:N2} s' -f $atHead.Count, $from, $to)
        $back = Measure-QaHookStep $c $lines 'head-on' 'head-press' 'head-press'
        Add-QaResult $c 'head: the gun back on the zombie: taken as the target within 0.3 s of the hand''s input, and the hook connects' ($back.Target -and $back.Delay -le $script:QaHookPromptSeconds -and $back.Hooked) (Format-QaHookStep $back)
    }),
    (New-QaHookScenario 'meathook-head' 'head' 'The Meat Hook under head aim (the control): the game takes its target where the head looks whatever the gun does, and the hook connects' {
        param($c, $lines)
        foreach ($k in @(@('hand-on', 'hand-press'), @('head-away', 'head-press'))) {
            $s = Measure-QaHookStep $c $lines $k[0] $k[1] $k[1]
            # Under head aim the zombie is brought in front of the head and stays the target while the hand moves:
            # its first line may come before the step.
            $prior = $lines | Where-Object { $_.T -lt $s.From -and $_.Target } | Select-Object -Last 1
            $held = $s.Target -or ($prior -and -not ($lines | Where-Object { $_.T -gt $prior.T -and $_.T -le $s.From + 1.2 -and -not $_.Target }))
            Add-QaResult $c "$($k[0]): the gun 20 to 25 deg away, the zombie in front of the head: the target in the game's view (the head's), and the hook connects" ($held -and $s.Hooked) ('{0}; before the step: {1}' -f (Format-QaHookStep $s), $(if ($prior) { "$($prior.Target) at yaw $($prior.OriginYaw) (view $($prior.ViewYaw))" } else { 'none' }))
        }
    }))

$e2e = @($script:QaScenarios | Where-Object { $_.Kind -eq 'e2e' })
$script:QaScenarios = @($script:QaScenarios | Where-Object { $_.Kind -ne 'e2e' }) + $hookScenarios + $e2e
