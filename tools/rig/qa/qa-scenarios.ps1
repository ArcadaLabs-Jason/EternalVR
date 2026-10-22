# The rig QA suite's scenarios (qa-suite.ps1), dot-sourced after qa-common.ps1. Each one is a hashtable:
#   Name      what -Only and the report call it
#   Kind      'game' (a simulator run through Invoke-QaGameScenario) or 'e2e' (the launcher's suite)
#   Proves    one line for the report and the README
#   Map       the map argument (default e1m2_battle); Env: NAME=VALUE pairs for the game; Args: game
#             command-line arguments after launch-ht.ps1's own (optional)
#   EyeCaptures  every how many pairs ETERNALVR_CAPTURE_EYES captures into <stage>-eyes (optional)
#   UiCaptures   every how many GUI captures ETERNALVR_CAPTURE_UI writes into <stage>-ui (optional)
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
# The block-edge ratio of one capture depends on the TAA jitter phase it lands on (r_TAANumSubSamples 32):
# every 400 pairs hit only 2 of the 32 phases, fixed by where the run started, so either eye's coarse half
# read anywhere from 1.15 to 1.33 between runs (crucible's runs on the 3080 Ti, 2026-10-03). Captures every
# $script:QaEyeCaptureEvery pairs (odd, so any 32 captures in a row land on each of the 32 phases once) and at
# least $script:QaEyeMinPairs of them (one whole cycle) average over the phases: 49 pairs in 40 s there.
$script:QaEyeCaptureEvery = 81
$script:QaEyeMinPairs = 32
function Test-QaEyeTestBlocks($Ctx) {
    Test-QaEyeTestHalves $Ctx
    Test-QaPassFrames $Ctx
}

# The halves of the eye test's captures; with -Since/-Until only the pairs written in that stretch of the run,
# and each result named with -Window first (the stretches before and after a map is loaded again).
function Test-QaEyeTestHalves($Ctx, $Since = $null, $Until = $null, [string]$Window = '') {
    $at = if ($Window) { "${Window}: " } else { '' }
    $period = 4
    $resize = Get-QaMatches $Ctx 'seq-taa: resize to \d+x\d+ \(upscaled \d+x\d+\)' -Regex | Select-Object -Last 1
    if ($resize -match 'resize to (\d+)x\d+ \(upscaled (\d+)x\d+\)') {
        $period = [int][Math]::Round(4.0 * [int]$Matches[2] / [int]$Matches[1])
    }
    $dir = "$($Ctx.Stage)-eyes"
    $m = $null
    $failed = ''
    try { $m = & (Join-Path $script:QaRig 'qa\qa-blockiness.ps1') -Dir $dir -Period $period -Since $Since -Until $Until } catch { $failed = $_.Exception.Message }
    if (-not $m) {
        # The script throws "no capture pairs ..." when the folder has none; anything else is the measure's own error.
        $why = if (-not $failed -or $failed -like 'no capture pairs*') { "no capture pairs in $dir" } else { "measuring failed: $failed" }
        Add-QaResult $Ctx "${at}eye captures measured" $false $why
    } else {
        $detail = '{0} pair(s), period {1}: eye L left {2:N2} right {3:N2}; eye R left {4:N2} right {5:N2}' -f $m.Images,
            $period, $m.LeftL, $m.RightL, $m.LeftR, $m.RightR
        Add-QaResult $Ctx "${at}eye captures measured ($script:QaEyeMinPairs pairs or more)" ($m.Images -ge $script:QaEyeMinPairs) $detail
        Add-QaResult $Ctx "${at}eye L's left half coarse (block-edge ratio 1.20 or more)" ($m.LeftL -ge 1.2) $detail
        Add-QaResult $Ctx "${at}eye L's right half not (1.10 or less)" ($m.RightL -le 1.1) $detail
        Add-QaResult $Ctx "${at}eye R's right half coarse (1.20 or more)" ($m.RightR -ge 1.2) $detail
        Add-QaResult $Ctx "${at}eye R's left half not (1.10 or less)" ($m.LeftR -le 1.1) $detail
    }
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

# Tutorials off first: a Steam profile that has not seen the level's tutorial popups (a new rig's account) got one
# over the equipment and Flame Belch presses, which then went to the popup (the second test PC, 2026-10-03).
$script:QaTokenSchedule ='3:god;g_setting_hud_auto_dismiss_tutorials 1;g_setting_tutorials 0|3.5:sync_printInteractionAndAnimationName 1|4:g_debugTriggers 1|' +
    '5:setviewpos -34.98 -250.31 33.2 45|6:where|7:selectDebugEntity'
$script:QaInputRead = "test input '.*' read \(0 issue\(s\)\)"

# The game's crosshair under hand aim (ui_layer/crosshair.hpp, ETERNALVR_UI_CROSSHAIR), at the Praetor Suit token's
# checkpoint with both hands pointing ahead. Shots: yaw 225 (map markers next to the middle of the view), yaw 45 (the
# token's name above it), the ability indicators after a Flame Belch and a frag, the chainsaw's NO TARGET, the Heavy
# Cannon with each of its mods held and firing, the pause menu, back in play, and the death screen and the reload's
# fade after the schedule's kill. Compare the -xr-both.png shots (the simulator's composed frame: the HUD quad as
# the headset gets it) between `hold` and `mask`.
$script:QaCrosshairSpot = 'setviewpos -34.98 -250.31 33.2'
$script:QaCrosshairSchedule = '3:g_setting_hud_auto_dismiss_tutorials 1;g_setting_tutorials 0|4:where|' +
    "5:$script:QaCrosshairSpot 225|11:$script:QaCrosshairSpot 45|50:kill"
$script:QaCrosshairRest = @('right.aim = 0, 0', 'left.aim = 0, 0')
# The layer log's lines from the first one matching $From (all lines when $From is empty; none when nothing matches)
# up to, not including, the first one after it matching $Until (to the end when none does).
function Get-QaLogSpan($Ctx, [string]$From, [string]$Until) {
    $lines = @((Get-QaLogText $Ctx) -split "`r?`n")
    $start = 0
    if ($From) {
        $start = -1
        for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i] -match $From) { $start = $i; break } }
        if ($start -lt 0) { return @() }
    }
    $end = $lines.Count
    for ($i = $start + 1; $i -lt $lines.Count; $i++) { if ($lines[$i] -match $Until) { $end = $i; break } }
    if ($end -le $start) { return @() }
    return @($lines[$start..($end - 1)])
}

# The hold stays confirmed until the schedule's kill (the death screen and the checkpoint reload after it may stop the
# GUI copies, and a request then lapses as intended): one confirmation, no loss after it, and every 10 s line from the
# first confirmed one on says `confirmed now` (a `not confirmed now` only before it, at the start).
function Test-QaHoldStays($Ctx) {
    $span = @(Get-QaLogSpan $Ctx '' 'debug-commands: [\d.]+ s: kill')
    $confirmed = @($span | Where-Object { $_.Contains('ui: crosshair hold confirmed') }).Count
    Add-QaResult $Ctx 'before the kill: exactly 1x ui: crosshair hold confirmed' ($confirmed -eq 1) "$confirmed line(s)"
    $lost = @($span | Where-Object { $_.Contains('ui: crosshair hold not confirmed') })
    Add-QaResult $Ctx 'before the kill: absent: ui: crosshair hold not confirmed' ($lost.Count -eq 0) "$($lost.Count) line(s)"
    $states = @($span | ForEach-Object {
        if ($_ -match 'for the crosshair hold \((not confirmed|confirmed) now') { $Matches[1] } })
    $first = [Array]::IndexOf($states, 'confirmed')
    $after = if ($first -ge 0) { @($states[$first..($states.Count - 1)] | Where-Object { $_ -ne 'confirmed' }).Count } else { -1 }
    Add-QaResult $Ctx 'before the kill: the 10 s lines say confirmed from the first confirmed one on' `
        ($first -ge 0 -and $after -eq 0) "$($states.Count) line(s) of $($span.Count): $($states -join ', ')"
}

# The Precision Bolt's zoom decl line (T-136): the gun stays, no scope GUI, and the zoomed scope reticle swapped for
# the weapon's own (the swap is what keeps the HUD under the crosshair hold).
$script:QaBoltLook = "zoom look: 'weapon/player/heavy_cannon_bolt_action[^']*': hands hidden on zoom [01] -> 0, after \d+ -> 0 ms, " +
    "scope GUI [1-6] -> 0, zoomed reticle 'weaponreticle/[^']+_zoomed' -> 'weaponreticle/[^']+'"

function Test-QaScopeLook($Ctx) {
    Test-QaPresent $Ctx $script:QaBoltLook -Regex
    Test-QaPresent $Ctx 'zoom look: scoped zoom starts'
}

# The layer logs the first 20 scoped zoom starts and ends together (vkcore/zoom_look.cpp kLoggedZooms) and the reticle
# cvar's first write and 12 more (runtime_cvars_reticle.cpp kLoggedTurns, each line counting all writes so far).
$script:QaLoggedZooms = 20

# Under head aim the hold writes only for a scoped zoom, and each zoom ends with the game's crosshair back: every `-> 0`
# write of hud_reticle_styleOverride comes after a `scoped zoom starts` with no `scoped zoom ends` between them, the
# last start has an end after it and a write back (`0 -> ...`, `... and no scoped zoom`) after that end, and neither
# log hit its cap (a capped log could hide a stuck hold).
function Test-QaHoldOnlyScoped($Ctx) {
    $lines = @((Get-QaLogText $Ctx) -split "`r?`n")
    $scoped = $false
    $writes = 0
    $outside = 0
    $zoomLines = 0
    $lastStart = -1
    $lastEnd = -1
    $lastBack = -1
    $capped = $false
    for ($i = 0; $i -lt $lines.Count; ++$i) {
        $line = $lines[$i]
        if ($line.Contains('zoom look: scoped zoom starts')) { $scoped = $true; $zoomLines++; $lastStart = $i }
        elseif ($line.Contains('zoom look: scoped zoom ends')) { $scoped = $false; $zoomLines++; $lastEnd = $i }
        if ($line.Contains('cvars: later writes are counted, not logged')) { $capped = $true }
        if ($line -match 'cvars: hud_reticle_styleOverride .*\(\d+ write\(s\)\)') {
            if ($line -match 'hud_reticle_styleOverride -?\d+ -> 0 \(reads 0\)') {
                $writes++
                if (-not $scoped) { $outside++ }
            } elseif ($line -match 'hud_reticle_styleOverride 0 -> -?\d+ .*and no scoped zoom') {
                $lastBack = $i
            }
        }
    }
    Add-QaResult $Ctx 'head aim: hud_reticle_styleOverride written to 0 only in a scoped zoom' ($writes -gt 0 -and $outside -eq 0) `
        "$writes write(s), $outside outside a scoped zoom"
    Add-QaResult $Ctx 'head aim: the last scoped zoom ended, and the crosshair was written back after it' `
        ($lastStart -ge 0 -and $lastEnd -gt $lastStart -and $lastBack -gt $lastEnd) "start line $lastStart, end line $lastEnd, write back line $lastBack"
    Add-QaResult $Ctx 'head aim: the zoom and cvar logs are complete (under their caps, every write logged)' `
        ($zoomLines -lt $script:QaLoggedZooms -and -not $capped) "$zoomLines zoom line(s) of $script:QaLoggedZooms, cvar log capped: $capped"
}

function New-QaCrosshairScenario([string]$Mode, [string]$Aim) {
    $name = if ($Aim -eq 'head') { "crosshair-$Mode-head" } else { "crosshair-$Mode" }
    $proves = switch ($name) {
        'crosshair-hold' { 'ETERNALVR_UI_CROSSHAIR=hold: hud_reticle_styleOverride 0 reaches the game under hand aim, the hold is confirmed and the GUI copy keeps its centre (map markers, the ability indicators and the chainsaw text visible; the game''s crosshair not)' }
        'crosshair-mask' { 'ETERNALVR_UI_CROSSHAIR=mask (the earlier behaviour, the positive control): the centre square of the GUI copy is cleared under hand aim and no cvar is held for it' }
        default { 'ETERNALVR_UI_CROSSHAIR=hold under head aim: the game''s crosshair stays, held at none only while the Precision Bolt is zoomed (no scope ring, T-136)' }
    }
    return @{
        Name = $name; Kind = 'game'; Proves = $proves
        Map = 'game/sp/e1m3_cult/e1m3_cult -checkpoint cp_03_shoot_gate'
        # The render size makes the GUI target the eye's near-square size, as in a headset (1280x1400 on the
        # simulator): at the suite's 1280x720 window the masked square is half the angle players see.
        Env = @("ETERNALVR_AIM=$Aim", "ETERNALVR_UI_CROSSHAIR=$Mode", 'ETERNALVR_RENDER_SIZE=auto',
                "ETERNALVR_DEBUG_COMMANDS=$script:QaCrosshairSchedule")
        Input = $script:QaCrosshairRest
        Timeline = {
            param($c)
            $rest = $script:QaCrosshairRest
            foreach ($yaw in 225, 45) {
                if (Wait-QaLog $c "debug-commands: [\d.]+ s: $script:QaCrosshairSpot $yaw" 40) {
                    Wait-QaSeconds $c 2.5; Save-QaShot $c "yaw$yaw"; Save-QaXrShot $c "yaw$yaw"
                } else { Add-QaNote $c "the schedule's setviewpos to yaw $yaw was not logged" }
            }
            Write-QaInput $c ($rest + 'left.grip = 1'); Wait-QaSeconds $c 0.3
            Write-QaInput $c $rest; Wait-QaSeconds $c 1.5
            Write-QaInput $c ($rest + 'left.trigger = 1'); Wait-QaSeconds $c 0.3
            Write-QaInput $c $rest; Wait-QaSeconds $c 1
            Save-QaShot $c 'abilities'; Save-QaXrShot $c 'abilities'
            Write-QaInput $c ($rest + 'right.stick = 0, 1'); Wait-QaSeconds $c 0.4
            Save-QaShot $c 'chainsaw'; Save-QaXrShot $c 'chainsaw'
            Write-QaInput $c $rest; Wait-QaSeconds $c 2
            # The Heavy Cannon (quick switch from the Combat Shotgun) with its mod held and firing, then with its other
            # mod (one of them is the Precision Bolt's scope).
            Write-QaInput $c ($rest + 'right.stick = 0, -1'); Wait-QaSeconds $c 0.15
            Write-QaInput $c $rest; Wait-QaSeconds $c 2
            foreach ($mod in 'a', 'b') {
                Write-QaInput $c ($rest + 'right.grip = 1'); Wait-QaSeconds $c 1.5
                Save-QaShot $c "mod-$mod"; Save-QaXrShot $c "mod-$mod"
                Write-QaInput $c ($rest + @('right.grip = 1', 'right.trigger = 1')); Wait-QaSeconds $c 0.15
                Save-QaShot $c "mod-$mod-fire"; Save-QaXrShot $c "mod-$mod-fire"
                Write-QaInput $c ($rest + 'right.grip = 1'); Wait-QaSeconds $c 1
                Write-QaInput $c $rest; Wait-QaSeconds $c 1.5
                if ($mod -eq 'a') {
                    Write-QaInput $c ($rest + 'left.secondary = 1'); Wait-QaSeconds $c 0.15
                    Write-QaInput $c $rest; Wait-QaSeconds $c 2
                }
            }
            Write-QaInput $c ($rest + 'left.menu = 1'); Wait-QaSeconds $c 0.3
            Write-QaInput $c $rest; Wait-QaSeconds $c 2.5
            Save-QaShot $c 'pause'; Save-QaXrShot $c 'pause'
            Write-QaInput $c ($rest + 'left.menu = 1'); Wait-QaSeconds $c 0.3
            Write-QaInput $c $rest; Wait-QaSeconds $c 3
            Save-QaShot $c 'resumed'; Save-QaXrShot $c 'resumed'
            # The death screen and the checkpoint reload's fade, last (full-screen layers: the mask must be off for them).
            if (Wait-QaLog $c 'debug-commands: [\d.]+ s: kill' 40) {
                $at = 0.0
                foreach ($t in 0.5, 1.5, 4, 8, 12) {
                    Wait-QaSeconds $c ($t - $at); $at = $t
                    Save-QaShot $c "death-$($t)s"; Save-QaXrShot $c "death-$($t)s"
                }
            } else { Add-QaNote $c 'the schedule''s kill was not logged' }
        }
        Asserts = {
            param($c)
            Test-QaPresent $c $script:QaInputRead -Regex
            switch ($c.Name) {
                'crosshair-hold' {
                    Test-QaPresent $c "the game's crosshair under hand aim: hold"
                    Test-QaPresent $c 'held at run time: .*hud_reticle_styleOverride 0 while hand aim is on' -Regex
                    Test-QaPresent $c 'hud_reticle_styleOverride -?\d+ -> 0 \(reads 0\); hand aim' -Regex
                    Test-QaHoldStays $c
                    Test-QaScopeLook $c
                }
                'crosshair-mask' {
                    Test-QaPresent $c "the game's crosshair under hand aim: mask"
                    Test-QaAbsent $c 'hud_reticle_styleOverride'
                }
                'crosshair-hold-head' {
                    Test-QaPresent $c "the game's crosshair under hand aim: hold"
                    Test-QaHoldOnlyScoped $c
                    Test-QaAbsent $c 'ui: crosshair hold confirmed'
                    Test-QaScopeLook $c
                }
            }
        }
    }
}

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
        Env = @('ETERNALVR_VRS_TEST=eyetest'); EyeCaptures = $script:QaEyeCaptureEvery
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
        EyeCaptures = $script:QaEyeCaptureEvery
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
        Proves = 'Melee aim with the off hand and equipment aim with the head (issue #11): with the weapon hand pointing 90 degrees away, each melee press is held back until the view took the off hand, reaches the game with the view on it, Use still picks up the Praetor Suit token, the view goes back to the weapon hand, the equipment launcher launches along the head without moving the view, and the shoulder launcher is drawn at the head with the Flame Belch''s plume going along the head (screenshot belch)'
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
            # The plume 0.3 s into the burst: ahead (the head), not 90 degrees to the left (the weapon hand).
            Write-QaInput $c ($away + 'left.grip = 1'); Wait-QaSeconds $c 0.3
            Save-QaShot $c 'belch'; Wait-QaSeconds $c 1.2
            Write-QaInput $c $away; Wait-QaSeconds $c 3
        }
        Asserts = {
            param($c)
            Test-QaPresent $c 'melee aim off hand, equipment aim head'
            Test-QaPresent $c "controllers: sync 'interact/preator_suit_token"
            Test-QaPresent $c 'action aim: the view follows the off hand for melee'
            Test-QaPresent $c 'action aim: melee press held back \d+ command\(s\), [\d.]+ ms, until the view took its target' -Regex
            # The punch's fists are left on the game's view (on the off hand), not kept at the weapon hand (issue #15).
            Test-QaPresent $c "viewmodel: the fists follow the game's view, on the melee aim's off hand"
            Test-QaActionView $c 'melee' 'off hand'
            Test-QaPresent $c 'equipment launch hook at RVA'
            Test-QaPresent $c 'equipment launch \(slot \d\): .* along the head' -Regex
            Test-QaAbsent $c 'action aim: the view follows the head for equipment'
            Test-QaPresent $c 'action aim: a Flame Belch shot while flame_belch held'
            Test-QaPresent $c 'Flame Belch axis hook at RVA'
            Test-QaPresent $c 'Flame Belch plume axis \(.*\) -> \(.*\) along the head' -Regex
            # The launcher the flames and the frag leave is drawn at the head, not at the weapon hand (issue #14).
            Test-QaPresent $c 'shoulder launcher hook at RVA'
            Test-QaPresent $c 'shoulder launcher \(slot 5, .*\): .* at the head, along the head' -Regex
            Test-QaPresent $c 'equipment launch \(slot \d\): .* along the head, from the launcher at the head' -Regex
            Test-QaPresent $c 'action aim: the view follows the weapon hand'
            Test-QaAbsent $c 'then sent anyway'
        }
    },
    @{
        Name = 'scope-aim'; Kind = 'game'
        Proves = 'The Heavy Cannon under hand aim, unscoped and scoped (its weapon mod held): every shot starts on the weapon hand''s ray, the aim dot is traced with the weapon trace''s own shape, and nothing faults'
        Map = 'game/sp/e1m3_cult/e1m3_cult -checkpoint cp_03_shoot_gate'
        Env = @('ETERNALVR_AIM=hand', "ETERNALVR_DEBUG_COMMANDS=$script:QaTokenSchedule")
        Input = @('right.aim = 0, 0', 'left.aim = 0, 0')
        Timeline = {
            param($c)
            Wait-QaSeconds $c 6
            # Quick switch (turn stick down, tapped): the Combat Shotgun to the Heavy Cannon.
            $aim = @('right.aim = 10, -5', 'left.aim = 0, 0')
            Write-QaInput $c ($aim + 'right.stick = 0, -1'); Wait-QaSeconds $c 0.15
            Write-QaInput $c $aim; Wait-QaSeconds $c 2
            Write-QaInput $c ($aim + 'right.trigger = 1'); Wait-QaSeconds $c 0.2
            Write-QaInput $c $aim; Wait-QaSeconds $c 1.5
            # Scoped: the weapon mod held, then a shot.
            Write-QaInput $c ($aim + 'right.grip = 1'); Wait-QaSeconds $c 1.5
            Save-QaShot $c 'scoped'
            Write-QaInput $c ($aim + @('right.grip = 1', 'right.trigger = 1')); Wait-QaSeconds $c 0.2
            Write-QaInput $c ($aim + 'right.grip = 1'); Wait-QaSeconds $c 1.5
            Write-QaInput $c $aim; Wait-QaSeconds $c 2
        }
        Asserts = {
            param($c)
            Test-QaPresent $c 'shots are traced with the weapon trace''s shape'
            Test-QaPresent $c 'controllers: shot: game start'
            Test-QaPresent $c 'ui: the aim dot at'
        }
    },
    @{
        Name = 'crosshair-hold-demon'; Kind = 'game'
        Proves = 'ETERNALVR_UI_CROSSHAIR=hold, hand aim with ETERNALVR_DEMON_AIM=head: the hold hides the game''s crosshair, then piloting the Revenant (head aim for the demon) gives hud_reticle_styleOverride its earlier value back and the game''s crosshair shows'
        Map = 'game/sp/e1m3_cult/e1m3_cult -checkpoint cp_12_possession'
        Env = @('ETERNALVR_AIM=hand', 'ETERNALVR_DEMON_AIM=head', 'ETERNALVR_UI_CROSSHAIR=hold', 'ETERNALVR_RENDER_SIZE=auto',
                'ETERNALVR_DEBUG_COMMANDS=3:god|3.5:g_debugTriggers 1|4:trigger cinematic_trigger_trigger_1|6:setviewpos 160.5 20.8 -5.0 90|7:where|8:selectDebugEntity')
        Input = $script:QaCrosshairRest
        Timeline = {
            param($c)
            Wait-QaSeconds $c 9
            Save-QaShot $c 'hand'; Save-QaXrShot $c 'hand'
            # Into the Revenant's pod: the weapon hand on the use proxy, then Use (the stick click).
            $aim = @('right.aim = 0, -40', 'left.aim = 0, 0')
            Write-QaInput $c $aim; Wait-QaSeconds $c 1.2
            Write-QaInput $c ($aim + 'right.click = 1'); Wait-QaSeconds $c 0.3
            Write-QaInput $c $aim
            if (Wait-QaLog $c 'controllers: piloting a demon' 30) {
                Wait-QaSeconds $c 3; Save-QaShot $c 'demon'; Save-QaXrShot $c 'demon'
            } else { Add-QaNote $c 'no piloting a demon line within 30 s of Use' }
            Wait-QaSeconds $c 3
        }
        Asserts = {
            param($c)
            Test-QaPresent $c $script:QaInputRead -Regex
            Test-QaPresent $c 'hud_reticle_styleOverride -?\d+ -> 0 \(reads 0\); hand aim' -Regex
            Test-QaPresent $c 'controllers: piloting a demon'
            # Given back for the aim switch (not a lapsed request), and not hidden again while piloting.
            Test-QaPresent $c 'hud_reticle_styleOverride 0 -> -1 \(reads -1\); the hold''s request ended \(no hand aim\)' -Regex
            $piloting = @(Get-QaLogSpan $c 'controllers: piloting a demon' "controllers: back in the Slayer's body")
            $hides = @($piloting | Where-Object { $_ -match 'hud_reticle_styleOverride -?\d+ -> 0 \(reads 0\); hand aim' })
            Add-QaResult $c 'while piloting: no hud_reticle_styleOverride -> 0 write' ($piloting.Count -gt 0 -and $hides.Count -eq 0) `
                "$($hides.Count) of $($piloting.Count) line(s)"
            # That check sees only logged writes: the first write and the next 12 are logged (kLoggedTurns = 12 in runtime_cvars_reticle.cpp),
            # later ones only counted. A last logged count under 13 means every write was logged.
            $counts = @([regex]::Matches((Get-QaLogText $c), 'hud_reticle_styleOverride -?\d+ -> [^\r\n]*\((\d+) write\(s\)\)') |
                ForEach-Object { [int]$_.Groups[1].Value })
            $last = if ($counts.Count) { ($counts | Measure-Object -Maximum).Maximum } else { 0 }
            Add-QaResult $c 'every hud_reticle_styleOverride write logged (fewer than 13)' ($counts.Count -gt 0 -and $last -lt 13) `
                "last logged count $last"
        }
    },
    (New-QaCrosshairScenario 'hold' 'hand'),
    (New-QaCrosshairScenario 'mask' 'hand'),
    (New-QaCrosshairScenario 'hold' 'head'),
    @{
        Name = 'launcher-e2e'; Kind = 'e2e'
        Proves = "The launcher's end-to-end suite (launcher\tests\e2e.ps1, stand-in game) passes against a Release build"
    }
)
