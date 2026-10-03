# The rig QA suite's scenarios (qa-suite.ps1), dot-sourced after qa-common.ps1. Each one is a hashtable:
#   Name      what -Only and the report call it
#   Kind      'game' (a simulator run through Invoke-QaGameScenario) or 'e2e' (the launcher's suite)
#   Proves    one line for the report and the README
#   Map       the map argument (default e1m2_battle); Env: NAME=VALUE pairs for the game; Args: game
#             command-line arguments after launch-ht.ps1's own (optional)
#   EyeCaptures  every how many pairs ETERNALVR_CAPTURE_EYES captures into <stage>-eyes (optional)
#   Input     the scripted input file's first contents (ETERNALVR_TEST_INPUT); $null for none
#   Timeline  { param($c) ... } runs once the player is in the map; Wait-QaSeconds, Write-QaInput, Save-QaShot
#   Asserts   { param($c) ... } runs on the layer log after the stop; Test-QaPresent/Absent/Count

$script:QaRestPose = @('left.aim = 0, 30', 'left.position = -0.2, -0.45, -0.25',
                       'right.aim = 0, 30', 'right.position = 0.2, -0.45, -0.25')

# Both hands up past the head line ($y metres above the head), moving up at 3 m/s.
function Get-QaThrowPose([string]$Y) {
    return @('left.aim = 0, -60', "left.position = -0.2, $Y, -0.2", 'left.velocity = 0, 3, 0',
             'right.aim = 0, -60', "right.position = 0.2, $Y, -0.2", 'right.velocity = 0, 3, 0')
}

# The eye test's captures (ETERNALVR_VRS_TEST=eyetest with ETERNALVR_CAPTURE_EYES into <stage>-eyes): eye L's
# left half and eye R's right half show 4x4 blocks, the other halves none (qa-blockiness.ps1). A block is 4
# render pixels: 4 captured pixels at the eye image's size, more at DLSS's smaller render size (the last
# `seq-taa: resize to WxH (upscaled WxH)` line). Then the render passes' frames (stereo_seq/pass_frames.hpp):
# no parity break or contradicted recording, and few passes kept at full rate for want of a frame, so a run
# whose passes mostly fell back to full rate cannot pass on the few that were foveated.
function Test-QaEyeTestBlocks($Ctx) {
    $period = 4
    $resize = Get-QaMatches $Ctx 'seq-taa: resize to \d+x\d+ \(upscaled \d+x\d+\)' -Regex | Select-Object -Last 1
    if ($resize -match 'resize to (\d+)x\d+ \(upscaled (\d+)x\d+\)') {
        $period = [int][Math]::Round(4.0 * [int]$Matches[2] / [int]$Matches[1])
    }
    $dir = "$($Ctx.Stage)-eyes"
    $m = $null
    $failed = ''
    try { $m = & (Join-Path $script:QaRig 'qa\qa-blockiness.ps1') -Dir $dir -Period $period } catch { $failed = $_.Exception.Message }
    if (-not $m) {
        # The script throws "no capture pairs ..." when the folder has none; anything else is the measure's own error.
        $why = if (-not $failed -or $failed -like 'no capture pairs*') { "no capture pairs in $dir" } else { "measuring failed: $failed" }
        Add-QaResult $Ctx 'eye captures measured' $false $why
    } else {
        $detail = '{0} pair(s), period {1}: eye L left {2:N2} right {3:N2}; eye R left {4:N2} right {5:N2}' -f $m.Images,
            $period, $m.LeftL, $m.RightL, $m.LeftR, $m.RightR
        Add-QaResult $Ctx "eye L's left half coarse (block-edge ratio 1.20 or more)" ($m.LeftL -ge 1.2) $detail
        Add-QaResult $Ctx "eye L's right half not (1.10 or less)" ($m.RightL -le 1.1) $detail
        Add-QaResult $Ctx "eye R's right half coarse (1.20 or more)" ($m.RightR -ge 1.2) $detail
        Add-QaResult $Ctx "eye R's left half not (1.10 or less)" ($m.LeftR -le 1.1) $detail
    }
    Test-QaPassFrames $Ctx
}

# The `vrs: render pass frames:` lines (every 200,000 passes, counts since the start): the last has no parity
# break and no recording contradicted later (the guesses, used only with ETERNALVR_TEST_VRS_PARITY=1, held),
# and the passes kept at full rate for want of a frame are a small share. Rig runs on 2026-10-02
# (qa-foveation-eyes and -dlss, the fov-eyes build): 37,246 and 36,764 not known, all before the map (18.9%
# and 18.6% of the first line's passes, none added between the first and the second line: 0 of 200,000 in the
# map, no pass there by recording or parity). So between the last two lines (in the map) at most 5%, 10,000
# passes, under a second of the rig's 12,400 a second. With one line only (a slower machine) the share
# includes start-up, so it is a note.
function Test-QaPassFrames($Ctx) {
    $lines = @(Get-QaMatches $Ctx 'vrs: render pass frames:')
    if ($lines.Count -eq 0) { Add-QaResult $Ctx 'render pass frames logged' $false 'no vrs: render pass frames line'; return }
    $last = $lines[-1]
    $text = $last.Trim() -replace '^.*?vrs: ', ''
    if ($last -match '(\d+) parity break') {
        Add-QaResult $Ctx 'render pass frames: no parity break' ([int64]$Matches[1] -eq 0) $text
    } else {
        Add-QaResult $Ctx 'render pass frames: parity breaks logged' $false $text
    }
    if ($last -match '(\d+) recording\(s\) contradicted later') {
        Add-QaResult $Ctx 'render pass frames: no recording contradicted later' ([int64]$Matches[1] -eq 0) $text
    } else {
        Add-QaResult $Ctx 'render pass frames: contradictions logged' $false $text
    }
    $counts = @($lines | ForEach-Object {
        if ($_ -match '(\d+) by the render-view job''s counter, (\d+) by their command buffer''s recording, (\d+) by its parity, (\d+) by neither; (\d+) at full rate for it') {
            , @([int64]$Matches[1], [int64]$Matches[2], [int64]$Matches[3], [int64]$Matches[4], [int64]$Matches[5])
        }
    })
    if ($counts.Count -eq 0) { Add-QaResult $Ctx 'render pass frames: counts read' $false $text; return }
    $b = $counts[-1]
    $a = if ($counts.Count -ge 2) { $counts[-2] } else { @(0, 0, 0, 0, 0) }
    $d = @(0..4 | ForEach-Object { $b[$_] - $a[$_] })
    $all = $d[0] + $d[1] + $d[2] + $d[3]
    $share = if ($all -gt 0) { $d[4] / $all } else { 1.0 }
    $detail = '{0} of {1} pass(es) at full rate for want of a frame ({2:N1}%): {3} agreed, {4} by recording, {5} by parity, {6} by neither' -f `
        $d[4], $all, ($share * 100), $d[0], $d[1], $d[2], $d[3]
    if ($counts.Count -ge 2) {
        Add-QaResult $Ctx 'render pass frames: full rate for want of a frame 5% or less between the last two lines' ($share -le 0.05) $detail
    } else {
        Add-QaNote $Ctx "one render pass frames line only (start-up included, not bounded): $detail"
    }
    if ($last -match '(\d+) pass\(es\) the tag in flight would have given the other eye') {
        Add-QaNote $Ctx "the tag in flight would have given the other eye to $($Matches[1]) pass(es)"
    }
}

# The game's view in the game frames after a held-back press of $Action went out (action_aim_hook.cpp): within
# 3 degrees of the source's angles (both from the body) in one of them, and away from the weapon hand's.
function Test-QaActionView($Ctx, [string]$Action, [string]$Source) {
    $pattern = "action aim: $Action press, game frame (\d) after it went out: game view (\S+) (\S+) \(pitch, yaw from the body\); head (\S+) (\S+), weapon hand (-?[\d.]+) (-?[\d.]+), off hand (-?[\d.]+) (-?[\d.]+); the view follows the (.+)$"
    $hit = $null
    foreach ($line in @(Get-QaMatches $Ctx "action aim: $Action press, game frame")) {
        if ($line -notmatch $pattern) { continue }
        $m = $Matches
        $want = if ($Source -eq 'head') { @([double]$m[4], [double]$m[5]) } else { @([double]$m[8], [double]$m[9]) }
        $view = @([double]$m[2], [double]$m[3])
        $yaw = [Math]::Abs((($view[1] - $want[1]) % 360 + 540) % 360 - 180)
        $handYaw = [Math]::Abs((($view[1] - [double]$m[7]) % 360 + 540) % 360 - 180)
        if ([Math]::Abs($view[0] - $want[0]) -le 3 -and $yaw -le 3 -and $handYaw -gt 10) { $hit = $line.Trim(); break }
    }
    Add-QaResult $Ctx "$Action press reached the game with the view on the $Source" ($null -ne $hit) $(if ($hit) { $hit -replace '^.*?action aim: ', '' } else { 'no matching game frame line' })
}

$script:QaTokenSchedule ='3:god|3.5:sync_printInteractionAndAnimationName 1|4:g_debugTriggers 1|' +
    '5:setviewpos -34.98 -250.31 33.2 45|6:where|7:selectDebugEntity'
$script:QaInputRead = "test input '.*' read \(0 issue\(s\)\)"

$script:QaScenarios = @(
    @{
        Name = 'baseline'; Kind = 'game'
        Proves = 'Route S stereo reaches the map on the simulator, shows eye pairs and logs no error for 40 s'
        Env = @()
        Timeline = { param($c) Wait-QaSeconds $c 40; Save-QaShot $c 'end' }
        Asserts = {
            param($c)
            Test-QaPresent $c 'stereo: eye views'
            Test-QaPresent $c 'rates: game'
            # Case-sensitive: 'view-to-hand error' and similar lower-case words are normal log text.
            Test-QaAbsent $c 'ERROR|exception|crash' -Regex -CaseSensitive
        }
    },
    @{
        Name = 'foveation'; Kind = 'game'
        Proves = 'Balanced foveated rendering shades the eyes and keeps the 8192x8192 target (not an eye) at full rate'
        Env = @('ETERNALVR_FOVEATION=balanced')
        Timeline = { param($c) Wait-QaSeconds $c 20; Save-QaShot $c 'end' }
        Asserts = {
            param($c)
            Test-QaPresent $c 'vrs: eye 0, \d+x\d+' -Regex
            Test-QaPresent $c 'render target 8192x8192 is not in the eye'
            Test-QaAbsent $c 'vrs: eye 0, 8192x8192'
        }
    },
    @{
        Name = 'foveation-eyes'; Kind = 'game'
        Proves = "Each eye's render passes get that eye's rate image (eye test, the suite's TAA): eye L's left half and eye R's right half of the captures are coarse, the other halves not"
        Env = @('ETERNALVR_VRS_TEST=eyetest'); EyeCaptures = 400
        Timeline = { param($c) Wait-QaSeconds $c 40; Save-QaShot $c 'end' }
        Asserts = {
            param($c)
            Test-QaPresent $c "eye test: eye L's left half and eye R's right half at 4x4"
            Test-QaEyeTestBlocks $c
        }
    },
    @{
        Name = 'foveation-eyes-dlss'; Kind = 'game'
        Proves = "The same with DLSS Quality per eye (its smaller render size, eye R's own DLSS feature; with -DlssDll the newer DLL)"
        Env = @('ETERNALVR_VRS_TEST=eyetest', 'ETERNALVR_STEREO_DLSS=1', 'ETERNALVR_STEREO_DLSS_QUALITY=quality')
        EyeCaptures = 400
        Args = @('+r_antialiasing 2')
        Timeline = { param($c) Wait-QaSeconds $c 40; Save-QaShot $c 'end' }
        Asserts = {
            param($c)
            Test-QaPresent $c "eye R's DLSS feature for the game's feature"
            Test-QaPresent $c 'seq-taa: resize to \d+x\d+ \(upscaled' -Regex
            Test-QaEyeTestBlocks $c
        }
    },
    @{
        Name = 'seated-jump'; Kind = 'game'
        Proves = 'Seated hands-up jump: a low throw (hands 0.10 m over the head) does not jump, a high one (0.25 m) jumps once'
        Env = @('ETERNALVR_HANDS_JUMP=1', 'ETERNALVR_POSTURE=seated')
        Input = $script:QaRestPose
        Timeline = {
            param($c)
            Wait-QaSeconds $c 12
            Write-QaInput $c (Get-QaThrowPose '0.10'); Wait-QaSeconds $c 0.7
            Write-QaInput $c $script:QaRestPose; Wait-QaSeconds $c 3
            Write-QaInput $c (Get-QaThrowPose '0.25'); Wait-QaSeconds $c 0.7
            Write-QaInput $c $script:QaRestPose; Wait-QaSeconds $c 4
            Save-QaShot $c 'after-jumps'
        }
        Asserts = {
            param($c)
            Test-QaPresent $c $script:QaInputRead -Regex
            Test-QaCount $c 'gesture: hands-up jump (seated height)' 1
        }
    },
    @{
        Name = 'arm-pose'; Kind = 'game'
        Proves = 'The right arm and gun follow a scripted hand pose (screenshot for comparison with the references)'
        Env = @()
        Input = @('right.position = 0.35, -0.15, -0.45', 'right.aim = 45, 0')
        Timeline = { param($c) Wait-QaSeconds $c 15; Save-QaShot $c 'arm-pose' }
        Asserts = { param($c) Test-QaPresent $c $script:QaInputRead -Regex }
    },
    @{
        Name = 'token-pickup'; Kind = 'game'
        Proves = 'Use on the Praetor Suit token picks it up as a sync animation, not a glory kill'
        Map = 'game/sp/e1m3_cult/e1m3_cult -checkpoint cp_03_shoot_gate'
        Env = @('ETERNALVR_AIM=hand', 'ETERNALVR_GLORY_KILLS=steady', 'ETERNALVR_OFFHAND_TRACE=1',
                "ETERNALVR_DEBUG_COMMANDS=$script:QaTokenSchedule")
        Input = @()
        Timeline = {
            param($c)
            Wait-QaSeconds $c 6   # the schedule's last step (7 s) has run: in the use trigger, facing the token
            $picked = $null
            foreach ($pitch in -15, -30, 0) {
                Write-QaInput $c @("right.aim = 0, $pitch"); Wait-QaSeconds $c 1
                Write-QaInput $c @("right.aim = 0, $pitch", 'right.click = 1'); Wait-QaSeconds $c 0.3
                Write-QaInput $c @("right.aim = 0, $pitch"); Wait-QaSeconds $c 2.5
                if ((Get-QaLogText $c) -match 'sync starts|controllers: sync') { $picked = $pitch; break }
            }
            if ($null -ne $picked) {
                Add-QaNote $c "Use at pitch $picked started the sync"
                Wait-QaSeconds $c 4; Save-QaShot $c 'after-pickup'
            } else {
                Add-QaNote $c 'no sync after Use at pitch -15, -30 and 0'
                Save-QaShot $c 'no-pickup'
            }
            Write-QaInput $c @(); Wait-QaSeconds $c 3
        }
        Asserts = {
            param($c)
            Test-QaPresent $c "controllers: sync 'interact/preator_suit_token"
            Test-QaPresent $c '(not a kill)'
            Test-QaAbsent $c 'glory: kill'
        }
    },
    @{
        Name = 'action-aim'; Kind = 'game'
        Proves = 'Melee aim with the off hand and equipment aim with the head (issue #11): with the weapon hand pointing 90 degrees away, each melee press is held back until the view took the off hand, reaches the game with the view on it, Use still picks up the Praetor Suit token, the view goes back to the weapon hand, and the equipment launcher launches along the head without moving the view'
        Map = 'game/sp/e1m3_cult/e1m3_cult -checkpoint cp_03_shoot_gate'
        Env = @('ETERNALVR_AIM=hand', 'ETERNALVR_MELEE_AIM=offhand', 'ETERNALVR_EQUIPMENT_AIM=head',
                "ETERNALVR_DEBUG_COMMANDS=$script:QaTokenSchedule")
        # Both hands ahead until the schedule has placed the player: setviewpos turns the view the weapon
        # hand aims, so the body faces the token only with the weapon hand straight ahead.
        Input = @('right.aim = 0, 0', 'left.aim = 0, 0')
        Timeline = {
            param($c)
            Wait-QaSeconds $c 6   # the schedule's last step (7 s) has run: in the use trigger, facing the token
            $away = @('right.aim = 90, 0', 'left.aim = -90, 0')
            $atToken = @('right.aim = 90, 0', 'left.aim = 0, -15')
            # Both hands away from the token. The token's Use is its trigger box, not where the view points (rig,
            # 2026-10-02): this press picks it up already.
            Write-QaInput $c ($away + 'right.click = 1'); Wait-QaSeconds $c 0.3
            Write-QaInput $c $away; Wait-QaSeconds $c 3
            # The off hand at the token, the weapon hand still 90 degrees to the left.
            Write-QaInput $c $atToken; Wait-QaSeconds $c 1
            Write-QaInput $c ($atToken + 'right.click = 1'); Wait-QaSeconds $c 0.3
            Write-QaInput $c $atToken; Wait-QaSeconds $c 6
            Save-QaShot $c 'after-use'
            # The equipment launcher, then the Flame Belch held for a burst: both aim with the head.
            Write-QaInput $c ($away + 'left.trigger = 1'); Wait-QaSeconds $c 0.3
            Write-QaInput $c $away; Wait-QaSeconds $c 2
            Write-QaInput $c ($away + 'left.grip = 1'); Wait-QaSeconds $c 1.5
            Write-QaInput $c $away; Wait-QaSeconds $c 3
        }
        Asserts = {
            param($c)
            Test-QaPresent $c 'melee aim off hand, equipment aim head'
            Test-QaPresent $c "controllers: sync 'interact/preator_suit_token"
            Test-QaPresent $c 'action aim: the view follows the off hand for melee'
            Test-QaPresent $c 'action aim: melee press held back \d+ command\(s\), [\d.]+ ms, until the view took its target' -Regex
            Test-QaActionView $c 'melee' 'off hand'
            Test-QaPresent $c 'equipment launch hook at RVA'
            Test-QaPresent $c 'equipment launch \(slot \d\): .* along the head' -Regex
            Test-QaAbsent $c 'action aim: the view follows the head for equipment'
            Test-QaPresent $c 'action aim: a Flame Belch shot while flame_belch held'
            Test-QaPresent $c 'action aim: the view follows the weapon hand'
            Test-QaAbsent $c 'then sent anyway'
        }
    },
    @{
        Name = 'launcher-e2e'; Kind = 'e2e'
        Proves = "The launcher's end-to-end suite (launcher\tests\e2e.ps1, stand-in game) passes against a Release build"
    }
)
