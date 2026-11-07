# The head sweep's timing (vkcore/head_sweep.cpp, docs/rig-findings/collision-query.md "When the world may be
# queried"): a query only with the world at rest. The camera hook sweeps at once when no physics step is in flight
# and asks otherwise, the end of the step's post-step job sweeps the asks, the fire hook sweeps at once.
# Dot-sourced by qa-suite.ps1 after qa-scenarios-demon.ps1, whose order it keeps: added to $script:QaScenarios
# before the launcher's end-to-end suite.
#
#   head-sweep  hand aim at a checkpoint in play, the head leaned 0.6 m ahead and back (eased, 3 s) while the body walks
#               in turn to the four sides until the lean meets a wall, shots fired, then the pause menu for 12 s: the
#               head in geometry from the answers before and during the pause, and the counts of the stats lines

# The stats line every 10 s (head_sweep.cpp logStats).
$script:QaSweepStats = 'room: collision queries last \d+ s: camera hook (\d+) \((\d+) at once with the world at rest, (\d+) ' +
    'asked with a physics step in flight\); after the sync (\d+) time\(s\) swept (\d+), (\d+) not \(another map\), (\d+) ' +
    'time\(s\) the pending task not finished, (\d+) us in all, longest (\d+) us; fire hook (\d+) at once, (\d+) held back'
# The room summary every 10 s (room_scale.cpp logStats): the times the head went into geometry so far.
$script:QaRoomSummary = '^\[ *([\d.]+)\] \[ *\d+\] room: anchored, .* (\d+) time\(s\) in geometry'

function Get-QaSweepTotals($Ctx) {
    $t = [ordered]@{ Asks = 0; Direct = 0; Deferred = 0; Frames = 0; Swept = 0; NotSwept = 0; Unfinished = 0; Micros = 0
                     Longest = 0; Now = 0; HeldBack = 0; Lines = 0 }
    foreach ($m in [regex]::Matches((Get-QaLogText $Ctx), $script:QaSweepStats)) {
        $g = $m.Groups
        $t.Asks += [int64]$g[1].Value; $t.Direct += [int64]$g[2].Value; $t.Deferred += [int64]$g[3].Value
        $t.Frames += [int64]$g[4].Value; $t.Swept += [int64]$g[5].Value; $t.NotSwept += [int64]$g[6].Value
        $t.Unfinished += [int64]$g[7].Value; $t.Micros += [int64]$g[8].Value
        $t.Longest = [Math]::Max($t.Longest, [int64]$g[9].Value); $t.Now += [int64]$g[10].Value
        $t.HeldBack += [int64]$g[11].Value; $t.Lines++
    }
    return $t
}

$roomScenarios = @(
    @{
        Name = 'head-sweep'; Kind = 'game'
        Map = 'game/sp/e1m3_cult/e1m3_cult -checkpoint cp_03_shoot_gate'
        Proves = 'Every collision query is made with the world at rest (the step''s byte clear, its pending task finished): the camera hook''s asks made with a step in flight are swept after the sync and answered (the head leaned into a wall, the aim dot), the pause menu keeps the head in the wall (swept at once with no step), the fire hook''s sweeps are made at once, none held back, no post-step finds its task unfinished, and nothing faults'
        # The test recenter at 3 s comes one period into the offset, with the head back over the body, so the anchor
        # is the body's. Body follow off: the body stays where the walk left it while the head leans.
        Env = @('ETERNALVR_AIM=hand', 'ETERNALVR_BODY_FOLLOW=0', 'ETERNALVR_TEST_RECENTER=3',
                'ETERNALVR_TEST_HEAD_OFFSET=0,0,-0.6,3',
                'ETERNALVR_DEBUG_COMMANDS=3:god;g_setting_hud_auto_dismiss_tutorials 1;g_setting_tutorials 0')
        Input = @('right.aim = 0, 0', 'left.aim = 0, 0')
        Timeline = {
            param($c)
            $aim = @('right.aim = 0, 0', 'left.aim = 0, 0')
            Wait-QaSeconds $c 8
            # Walk ahead, then a lean period standing; a quarter turn (two 45-degree snap turns) and again, until the
            # lean reaches into a wall (the body stops 0.4 m from it, the lean goes 0.6 m).
            foreach ($side in 1..8) {
                Write-QaInput $c ($aim + 'left.stick = 0, 1'); Wait-QaSeconds $c 2.5
                Write-QaInput $c $aim; Wait-QaSeconds $c 3.2
                if ((Get-QaLogText $c) -match 'room: head in geometry') { break }
                foreach ($snap in 1..2) {
                    Write-QaInput $c ($aim + 'right.stick = 1, 0'); Wait-QaSeconds $c 0.25
                    Write-QaInput $c $aim; Wait-QaSeconds $c 0.35
                }
            }
            Save-QaShot $c 'leaning'
            foreach ($shot in 1..4) {
                Write-QaInput $c ($aim + 'right.trigger = 1'); Wait-QaSeconds $c 0.2
                Write-QaInput $c $aim; Wait-QaSeconds $c 1
            }
            Wait-QaSeconds $c 8
            # The pause menu over the lean: no physics step runs, and the head stays in the wall.
            $c.PauseFrom = (Get-QaLogText $c).Length
            Write-QaInput $c ($aim + 'left.menu = 1'); Wait-QaSeconds $c 0.3
            Write-QaInput $c $aim; Wait-QaSeconds $c 21
            Save-QaShot $c 'paused'
            $c.PauseTo = (Get-QaLogText $c).Length
            Write-QaInput $c ($aim + 'left.menu = 1'); Wait-QaSeconds $c 0.3
            Write-QaInput $c $aim; Wait-QaSeconds $c 6
        }
        Asserts = {
            param($c)
            Test-QaPresent $c 'only with the world at rest'
            Test-QaPresent $c 'room: first head sweep: fraction'
            Test-QaPresent $c 'ui: the aim dot at [\d.]+ m \(where the ray meets the world\)' -Regex
            Test-QaPresent $c 'controllers: shot: game start'
            Test-QaAbsent $c 'collision query faulted'
            Test-QaAbsent $c 'shot''s sweep from the eye to the hand was not made'
            Test-QaAbsent $c 'ERROR|exception|crash' -Regex -CaseSensitive
            # The physics object the step's byte is read from is the one whose post-step job the hook sweeps in.
            $text = Get-QaLogText $c
            $read = [regex]::Match($text, 'room: physics object at map instance \+ 0x([0-9A-F]+)')
            $post = [regex]::Match($text, 'room: the post-step job ran for the physics object at map instance \+ 0x([0-9A-F]+)')
            Add-QaResult $c 'the physics object read is the one whose post-step job sweeps' ($read.Success -and $post.Success -and $read.Groups[1].Value -eq $post.Groups[1].Value) `
                "read +0x$($read.Groups[1].Value), post-step +0x$($post.Groups[1].Value)"
            $t = Get-QaSweepTotals $c
            $detail = "camera hook $($t.Asks) ($($t.Direct) at once, $($t.Deferred) asked); after the sync $($t.Frames) time(s) swept $($t.Swept), $($t.NotSwept) not, $($t.Unfinished) task not finished, $($t.Micros) us, longest $($t.Longest) us; fire hook $($t.Now) at once, $($t.HeldBack) held back; $($t.Lines) stats line(s)"
            Add-QaResult $c 'the camera hook asked with the step in flight and the asks were swept after the sync' ($t.Deferred -gt 0 -and $t.Swept -gt 0) $detail
            Add-QaResult $c 'no post-step found its pending task unfinished' ($t.Lines -gt 0 -and $t.Unfinished -eq 0) $detail
            Add-QaResult $c 'the fire hook swept at once, none held back' ($t.Now -gt 0 -and $t.HeldBack -eq 0) $detail
            # The head's sweeps find the wall the lean reaches into.
            Test-QaPresent $c 'room: head in geometry \([\d.]+ m deep\)' -Regex
            # The pause: the camera hook swept at once (no step), and the head went into the wall again while paused
            # (the room summary's count of times in geometry rose between two summaries inside the pause).
            if ($c.PauseFrom -and $c.PauseTo) {
                $pause = $text.Substring($c.PauseFrom, [Math]::Min($c.PauseTo, $text.Length) - $c.PauseFrom)
                $direct = 0
                foreach ($m in [regex]::Matches($pause, $script:QaSweepStats)) { $direct += [int64]$m.Groups[2].Value }
                $rows = @()
                foreach ($line in ($pause -split "`r?`n")) {
                    $m = [regex]::Match($line, $script:QaRoomSummary)
                    if ($m.Success) { $rows += , @([double]$m.Groups[1].Value, [int64]$m.Groups[2].Value) }
                }
                $rose = $rows.Count -ge 2 -and $rows[-1][1] -gt $rows[0][1]
                Add-QaResult $c 'paused: the camera hook swept at once' ($direct -gt 0) "$direct at once in the pause"
                Add-QaResult $c 'paused: the head went into the wall again' $rose `
                    "$($rows.Count) summaries; times in geometry $(if ($rows.Count) { "$($rows[0][1]) -> $($rows[-1][1])" })"
            } else {
                Add-QaResult $c 'the pause ran' $false 'the timeline did not reach the pause'
            }
        }
    }
)

# Before the launcher's end-to-end suite, which stays last.
$e2e = @($script:QaScenarios | Where-Object { $_.Kind -eq 'e2e' })
$script:QaScenarios = @($script:QaScenarios | Where-Object { $_.Kind -ne 'e2e' }) + $roomScenarios + $e2e
