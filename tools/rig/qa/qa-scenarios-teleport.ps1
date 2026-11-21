# The player's teleports (docs/rig-findings/camera-takeovers.md row 12): the log's line of its own for a teleport
# (`game: teleport N: ...`, src/vkcore/body_jump_watch.cpp, comfort::TeleportWatch), whatever the settings.
# Dot-sourced by qa-suite.ps1 after qa-scenarios-takeover.ps1 (it uses that file's Get-QaTqEvents).
#
#   teleport-fall
#       e1m1 cp_06_post_yellow_key under hand aim with bHaptics and body follow off (each has a teleport line of its
#       own), god: the player put above the pit by the wall-climb tutorial's trigger by `setviewpos` twice (facing
#       180, then 0), each time falling in, and the game putting the player back on the ledge before the pit facing
#       it (rig, 2026-11-17). For each: a teleport line within 1 s of the step to within 0.5 m of the spot (the
#       debug teleport) and then, within 6 s, one to within 1.5 m of the ledge (the game's way back after the fall);
#       no `room: body follow` or `bhaptics:` teleport line. Notes: every teleport line (the view's turn with each).

# The teleport line: N, the jump's metres, from, where to, forced view, the turn and its largest step.
$script:QaTeleportLine = 'game: teleport (\d+): the player moved ([\d.]+) m in one game frame \((-?[\d.]+) (-?[\d.]+) (-?[\d.]+) -> (-?[\d.]+) (-?[\d.]+) (-?[\d.]+), (\d+) ms apart; forced view (yes|no), cutscene (yes|no)\); the view turned (-?[\d.]+) deg over it and (\d+) game view\(s\) after \(largest step (-?[\d.]+)\)'

function Get-QaTeleports($Ctx) {
    $out = New-Object System.Collections.ArrayList
    foreach ($e in @(Get-QaTqEvents $Ctx $script:QaTeleportLine)) {
        $g = $e.M.Groups
        # Group 1 is the line's time (Get-QaTqEvents).
        [void]$out.Add([pscustomobject]@{ T = $e.T; N = [int]$g[2].Value; Metres = [double]$g[3].Value
            To = @([double]$g[7].Value, [double]$g[8].Value, [double]$g[9].Value); Forced = $g[11].Value -eq 'yes'
            Turn = [double]$g[13].Value; Step = [double]$g[15].Value; Text = ($e.M.Value -replace '^.*?game: ', '') })
    }
    return $out
}

function Get-QaFlatDistance([double[]]$A, [double[]]$B) {
    return [Math]::Sqrt(($A[0] - $B[0]) * ($A[0] - $B[0]) + ($A[1] - $B[1]) * ($A[1] - $B[1]))
}

# e1m1's pit by the wall-climb tutorial's trigger (tutorial_wallclimb_trigger_1) and the ledge before it, where the
# game puts a player who fell in (rig, 2026-11-17).
$script:QaPitSpot = @(221.8, -537.3)
$script:QaPitLedge = @(224.7, -550.9, -212.2)
$script:QaTeleportSchedule = '3:god;g_setting_hud_auto_dismiss_tutorials 1;g_setting_tutorials 0;ai_freezeMovement 1|6:setviewpos 221.8 -537.3 -199.5 180|16:setviewpos 221.8 -537.3 -199.5 0|26:getviewpos'

$teleportScenarios = @(
    @{
        Name = 'teleport-fall'; Kind = 'game'; OnlyByName = $true
        Proves = 'The player''s teleports have a log line of their own with bHaptics and body follow off: put above e1m1''s pit twice (a debug teleport) and put back on the ledge by the game after each fall, each with its line and the view''s turn'
        Map = 'game/sp/e1m1_intro/e1m1_intro -checkpoint cp_06_post_yellow_key'
        Env = @('ETERNALVR_AIM=hand', 'ETERNALVR_BHAPTICS=0', 'ETERNALVR_BODY_FOLLOW=0', "ETERNALVR_DEBUG_COMMANDS=$script:QaTeleportSchedule")
        Input = $script:QaRestPose
        Timeline = {
            param($c)
            foreach ($at in 6, 16) {
                if (-not (Wait-QaLog $c ('debug-commands: {0}\.0 s: setviewpos' -f $at) 90)) { Add-QaNote $c "the setviewpos at $at s was not logged"; return }
                Wait-QaSeconds $c 0.4; Save-QaXrShot $c "above-$at"
                Wait-QaSeconds $c 3; Save-QaXrShot $c "back-$at"
            }
            Wait-QaSeconds $c 6
        }
        Asserts = {
            param($c)
            $tps = @(Get-QaTeleports $c)
            $steps = @(Get-QaTqEvents $c 'debug-commands: (6|16)\.0 s: setviewpos')
            $ok = 0; $text = @()
            foreach ($s in $steps) {
                $up = $tps | Where-Object { $_.T -ge $s.T -and $_.T -le $s.T + 1.0 -and (Get-QaFlatDistance $_.To $script:QaPitSpot) -le 0.5 } | Select-Object -First 1
                $back = if ($up) { $tps | Where-Object { $_.T -gt $up.T -and $_.T -le $up.T + 6 -and (Get-QaFlatDistance $_.To $script:QaPitLedge) -le 1.5 -and [Math]::Abs($_.To[2] - $script:QaPitLedge[2]) -le 1.5 } | Select-Object -First 1 }
                if ($up -and $back) { $ok++ }
                $text += 'step {0} s: above the pit {1}; back on the ledge {2}' -f $s.M.Groups[2].Value, $(if ($up) { "teleport $($up.N), the view turned $($up.Turn) deg" } else { 'no line' }), $(if ($back) { "teleport $($back.N), $($back.Metres) m, the view turned $($back.Turn) deg (largest step $($back.Step))" } else { 'no line' })
            }
            Add-QaResult $c 'game: teleport lines: each debug teleport above the pit (within 1 s, to within 0.5 m of the spot) and the game''s way back to the ledge after the fall (within 6 s, to within 1.5 m)' ($steps.Count -eq 2 -and $ok -eq 2) ($text -join '; ')
            Test-QaAbsent $c 'room: body follow: the origin jumped'
            Test-QaAbsent $c 'bhaptics: teleport'
            Add-QaNote $c ('teleport lines: {0}' -f (($tps | ForEach-Object { $_.Text }) -join ' | '))
        }
    }
)

# Before the launcher's end-to-end suite, which stays last.
$e2e = @($script:QaScenarios | Where-Object { $_.Kind -eq 'e2e' })
$script:QaScenarios = @($script:QaScenarios | Where-Object { $_.Kind -ne 'e2e' }) + $teleportScenarios + $e2e
