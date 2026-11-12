# The camera of a pickup animation (docs/rig-findings/camera-animations.md section 6, ETERNALVR_PICKUP_CAMERA). The
# Praetor Suit token at e1m3's cp_03_shoot_gate is the pickup the rig reaches the normal way (the token-pickup
# scenario's spot): its sync animation ('interact/preator_suit_token/...') cuts the game's camera to its own start, turns
# it about 6 degrees down and 9 up over about 3 s. ETERNALVR_TEST_CAMERA_TRACE logs the game's camera, the player's
# view angles and the view the layer wrote in every game frame of a forced view. The simulator's head stays still, so
# the written view's pitch is the head's (0) plus what the layer adds. Dot-sourced by qa-suite.ps1 after
# qa-scenarios-fxsync.ps1: added to $script:QaScenarios before the launcher's end-to-end suite.
#
#   pickup-camera       the default (ETERNALVR_PICKUP_CAMERA unset): the pickup runs and the written view stays on the
#                       head (its pitch within 0.5 degrees all along), and nothing plays the camera (no 'pickup:' line
#                       of the layer's own; the bHaptics bridge logs 'bhaptics: pickup:')
#   pickup-camera-play  ETERNALVR_PICKUP_CAMERA=play: the written view turns with the game's camera (its pitch over 4
#                       degrees of range), never steps more than 4 degrees from one game frame to the next (the
#                       camera's cuts left out, the end eased; the token's steps are under 0.7 degrees, 4 leaves room
#                       for a frame a shot stalled), and is back on the head by the last trace line (120 game views
#                       after the forced view, longer than the 0.3 s ease at any frame rate up to about 400 a second)

# The trace line (camera_anim_hook.cpp traceView): the game's camera, the player's angles, the written view.
$script:QaCamTrace = 'camera: trace (\d+): game \((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\) angles \((-?[\d.]+) (-?[\d.]+)\) written \((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\)'

# Over the trace lines: the game camera's pitch range in the frames 5 degrees or more off the player's angles (the
# pickup's), the written pitch's range, its largest step between two lines and its last value.
function Get-QaPickupTrace($Ctx) {
    $r = [ordered]@{ Lines = 0; Turned = 0; GameMin = 999.0; GameMax = -999.0; WrittenMin = 999.0; WrittenMax = -999.0
                     MaxStep = 0.0; Last = 0.0 }
    $prev = $null
    foreach ($m in [regex]::Matches((Get-QaLogText $Ctx), $script:QaCamTrace)) {
        $g = $m.Groups
        $r.Lines++
        $game = [double]$g[2].Value; $angles = [double]$g[5].Value; $written = [double]$g[7].Value
        if ([Math]::Abs($game - $angles) -ge 5) {
            $r.Turned++
            $r.GameMin = [Math]::Min($r.GameMin, $game); $r.GameMax = [Math]::Max($r.GameMax, $game)
        }
        $r.WrittenMin = [Math]::Min($r.WrittenMin, $written); $r.WrittenMax = [Math]::Max($r.WrittenMax, $written)
        if ($null -ne $prev) { $r.MaxStep = [Math]::Max($r.MaxStep, [Math]::Abs($written - $prev)) }
        $prev = $written; $r.Last = $written
    }
    $r.GameRange = if ($r.Turned -gt 0) { $r.GameMax - $r.GameMin } else { 0.0 }
    $r.WrittenRange = if ($r.Lines -gt 0) { $r.WrittenMax - $r.WrittenMin } else { 0.0 }
    return $r
}

function Get-QaPickupTraceText($t) {
    return ('{0} line(s), {1} in the pickup; game pitch {2:N1} to {3:N1}; written pitch {4:N1} to {5:N1}, largest step {6:N2}, last {7:N2}' -f
        $t.Lines, $t.Turned, $t.GameMin, $t.GameMax, $t.WrittenMin, $t.WrittenMax, $t.MaxStep, $t.Last)
}

function New-QaPickupScenario([string]$Name, [string]$Proves, [string[]]$ExtraEnv, [scriptblock]$Asserts) {
    return @{
        Name = $Name; Kind = 'game'
        Proves = $Proves
        Map = 'game/sp/e1m3_cult/e1m3_cult -checkpoint cp_03_shoot_gate'
        Env = @('ETERNALVR_AIM=hand', 'ETERNALVR_TEST_CAMERA_TRACE=1', "ETERNALVR_DEBUG_COMMANDS=$script:QaTokenSchedule") + $ExtraEnv
        Input = @()
        Timeline = {
            param($c)
            Wait-QaSeconds $c 6   # the schedule's last step (7 s) has run: in the use trigger, facing the token
            $picked = $null
            foreach ($pitch in -15, -30, 0) {
                Write-QaInput $c @("right.aim = 0, $pitch"); Wait-QaSeconds $c 1
                Save-QaShot $c "before-$(-$pitch)"
                Write-QaInput $c @("right.aim = 0, $pitch", 'right.click = 1'); Wait-QaSeconds $c 0.3
                Write-QaInput $c @("right.aim = 0, $pitch")
                # The pickup's 3 s: a shot about every 0.4 s.
                for ($i = 1; $i -le 7; $i++) { Wait-QaSeconds $c 0.1; Save-QaShot $c "during-$(-$pitch)-$i" }
                Wait-QaSeconds $c 1
                if ((Get-QaLogText $c) -match "controllers: sync 'interact/") { $picked = $pitch; break }
            }
            if ($null -ne $picked) { Add-QaNote $c "Use at pitch $picked started the sync" }
            else { Add-QaNote $c 'no sync after Use at pitch -15, -30 and 0' }
            Wait-QaSeconds $c 3; Save-QaShot $c 'after'
            Write-QaInput $c @(); Wait-QaSeconds $c 2
        }
        Asserts = $Asserts
    }
}

$pickupScenarios = @(
    (New-QaPickupScenario 'pickup-camera' 'The default leaves a pickup camera''s turn out: the written view stays on the head while the game''s camera turns' @() {
        param($c)
        Test-QaPresent $c "controllers: sync 'interact/preator_suit_token"
        Test-QaAbsent $c '\] pickup: (ETERNALVR_PICKUP_CAMERA|camera turn)' -Regex
        $t = Get-QaPickupTrace $c
        $text = Get-QaPickupTraceText $t
        Add-QaResult $c 'trace: the game''s camera turned in the pickup' ($t.Turned -gt 0 -and $t.GameRange -ge 4) $text
        Add-QaResult $c 'trace: the written view stayed on the head (pitch within 0.5 deg)' ($t.Lines -gt 0 -and $t.WrittenRange -le 0.5) $text
    }),
    (New-QaPickupScenario 'pickup-camera-play' 'ETERNALVR_PICKUP_CAMERA=play plays a pickup camera''s motion on top of the head without a jump, and the view is back on the head after it' @('ETERNALVR_PICKUP_CAMERA=play') {
        param($c)
        Test-QaPresent $c "controllers: sync 'interact/preator_suit_token"
        Test-QaPresent $c 'pickup: ETERNALVR_PICKUP_CAMERA=play'
        Test-QaPresent $c 'pickup: camera turn \d+ starts' -Regex
        Test-QaPresent $c 'pickup: camera turn \d+ ends after' -Regex
        $t = Get-QaPickupTrace $c
        $text = Get-QaPickupTraceText $t
        Add-QaResult $c 'trace: the game''s camera turned in the pickup' ($t.Turned -gt 0 -and $t.GameRange -ge 4) $text
        Add-QaResult $c 'trace: the written view turned with it (pitch range over 4 deg)' ($t.WrittenRange -ge 4) $text
        Add-QaResult $c 'trace: no step over 4 deg between two game frames' ($t.Lines -gt 0 -and $t.MaxStep -le 4) $text
        Add-QaResult $c 'trace: the view back on the head at the end (within 0.5 deg)' ($t.Lines -gt 0 -and [Math]::Abs($t.Last) -le 0.5) $text
    })
)

# Before the launcher's end-to-end suite, which stays last.
$e2e = @($script:QaScenarios | Where-Object { $_.Kind -eq 'e2e' })
$script:QaScenarios = @($script:QaScenarios | Where-Object { $_.Kind -ne 'e2e' }) + $pickupScenarios + $e2e
