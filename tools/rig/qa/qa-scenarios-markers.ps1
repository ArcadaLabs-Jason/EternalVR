# The HUD's world-attached markers on the head-locked quad (docs/rig-findings/hud-markers.md). Dot-sourced by
# qa-suite.ps1 after qa-scenarios-foveation.ps1: added to $script:QaScenarios before the launcher's end-to-end
# suite.
#
# At the e1m3 gate checkpoint the map's POI targets are triggered from the console, so their markers show, and
# the body turns a full circle twice (the head-locked quad turns with it, as with a head turn). With
# ETERNALVR_TEST_MARKER_LOG=1 the layer logs, about once a second, the HUD's view and, for each marker, its world
# point and where the HUD draws it. The check below works from those numbers alone: the direction of the
# marker's point from the HUD's view, against the direction the quad shows the drawn position in (the SWF's
# frame is the 16:9 band, the quad's size and place from the layer's line).

$script:QaMarkerSpot = '-34.98 -250.31 33.2'
$markerSteps = @('3:g_setting_hud_auto_dismiss_tutorials 1;g_setting_tutorials 0', "5:setviewpos $script:QaMarkerSpot 0",
                 ('6:trigger poi_target_poi_1;trigger poi_target_poi_7;trigger poi_target_poi_9;' +
                  'trigger poi_target_poi_10;trigger poi_target_poi_11'))
$t = 8.0
foreach ($yaw in 0..23) {
    $markerSteps += ('{0}:setviewpos {1} {2}' -f $t, $script:QaMarkerSpot, ($yaw * 15)); $t += 1.2
}
$activate = 'activatetargets poi_trigger_trigger_2;activatetargets poi_trigger_trigger_9;activatetargets poi_trigger_trigger_10'
$markerSteps += ('{0}:{1}' -f $t, $activate); $t += 1.0
foreach ($yaw in 0..23) {
    $markerSteps += ('{0}:setviewpos {1} {2}' -f $t, $script:QaMarkerSpot, ($yaw * 15 + 7)); $t += 1.2
}
$markerSteps += ('{0}:where' -f $t)
$script:QaMarkerSchedule = $markerSteps -join '|'
$script:QaMarkerRest = @('right.aim = 0, 0', 'left.aim = 0, 0')

# The angle in degrees between the directions with tangents (r0, u0) and (r1, u1).
function Get-QaAngleBetween([double]$R0, [double]$U0, [double]$R1, [double]$U1) {
    $c = ($R0 * $R1 + $U0 * $U1 + 1) / ([math]::Sqrt($R0 * $R0 + $U0 * $U0 + 1) * [math]::Sqrt($R1 * $R1 + $U1 * $U1 + 1))
    return [math]::Acos([math]::Min(1.0, $c)) * 180 / [math]::PI
}

# The markers of the test log: for each, its direction from the HUD's view (TrueYaw, TruePitch: degrees, right and
# up), the direction the quad shows its drawn position in (ShownYaw, ShownPitch), the angle between them (Error)
# and whether the HUD drew it off its frame. $Quad: WidthMetres, DistanceMetres, OffsetMetres, Band (x, y, w, h)
# of the GUI target.
function Get-QaMarkerSamples([string]$Text, $Quad) {
    $out = @()
    $band = $Quad.Band
    $height = $Quad.WidthMetres * $band[3] / $band[2]
    $rx = 'test log: marker (-?\d+) at world \([-\d. ]+\), view \((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\); drawn at \((-?[\d.]+), (-?[\d.]+)\) of (\d+) x (\d+), (on|off) the frame'
    foreach ($m in [regex]::Matches($Text, $rx)) {
        $z = [double]$m.Groups[4].Value
        if ($z -ge 0) { continue } # behind the view
        $tr = [double]$m.Groups[2].Value / -$z
        $tu = [double]$m.Groups[3].Value / -$z
        $u = [double]$m.Groups[5].Value / [double]$m.Groups[7].Value
        $v = [double]$m.Groups[6].Value / [double]$m.Groups[8].Value
        $sr = ($u - 0.5) * $Quad.WidthMetres / $Quad.DistanceMetres
        $su = ($Quad.OffsetMetres + (0.5 - $v) * $height) / $Quad.DistanceMetres
        $out += [pscustomobject]@{
            Uid = [int]$m.Groups[1].Value
            TrueYaw = [math]::Atan($tr) * 180 / [math]::PI; TruePitch = [math]::Atan($tu) * 180 / [math]::PI
            ShownYaw = [math]::Atan($sr) * 180 / [math]::PI; ShownPitch = [math]::Atan($su) * 180 / [math]::PI
            Error = (Get-QaAngleBetween $tr $tu $sr $su); Off = ($m.Groups[9].Value -eq 'off')
        }
    }
    return $out
}

# The quad and band the layer logged ('the HUD's markers follow the head-locked quad (W m wide at D m, O m up,
# showing (x, y) w x h of the TW x TH GUI target)'), or $null.
function Get-QaMarkerQuad($Ctx) {
    $line = @(Get-QaMatches $Ctx "the HUD's markers follow the head-locked quad" | Select-Object -Last 1)
    if ($line.Count -eq 0 -or $line[0] -notmatch '\(([\d.]+) m wide at ([\d.]+) m, (-?[\d.]+) m up, showing \((\d+), (\d+)\) (\d+) x (\d+) of') {
        return $null
    }
    return [pscustomobject]@{ WidthMetres = [double]$Matches[1]; DistanceMetres = [double]$Matches[2]
                              OffsetMetres = [double]$Matches[3]
                              Band = @([int]$Matches[4], [int]$Matches[5], [int]$Matches[6], [int]$Matches[7]) }
}

# Markers whose point lies inside the quad's band (out to 32 degrees across and 19.5 up and down) are drawn
# within 1 degree of it; markers in front whose point is above or below the band (more than 22 degrees) are off
# the frame (clamped to its edge), not drawn in the rows the quad does not show.
function Test-QaMarkerDirections($Ctx, $Quad) {
    $samples = @(Get-QaMarkerSamples (Get-QaLogText $Ctx) $Quad)
    $inside = @($samples | Where-Object { -not $_.Off -and [math]::Abs($_.TrueYaw) -le 32 -and [math]::Abs($_.TruePitch) -le 19.5 })
    $worst = ($inside | Measure-Object -Property Error -Maximum).Maximum
    $mean = ($inside | Measure-Object -Property Error -Average).Average
    $detail = '{0} marker sample(s) inside the band, error mean {1:N2} deg, largest {2:N2} deg ({3} sample(s) in all)' -f `
        $inside.Count, $mean, $worst, $samples.Count
    Add-QaResult $Ctx 'markers inside the band are drawn within 1 degree of their target' ($inside.Count -ge 20 -and $worst -lt 1.0) $detail
    $beyond = @($samples | Where-Object { [math]::Abs($_.TrueYaw) -le 30 -and [math]::Abs($_.TruePitch) -gt 22 -and [math]::Abs($_.TruePitch) -lt 36 })
    $onFrame = @($beyond | Where-Object { -not $_.Off })
    Add-QaResult $Ctx 'markers above or below the band are off the frame' ($beyond.Count -ge 1 -and $onFrame.Count -eq 0) `
        ('{0} sample(s) 22 to 36 deg up or down, {1} of them drawn on the frame' -f $beyond.Count, $onFrame.Count)
}

$markerScenarios = @(
    @{
        Name = 'hud-markers'; Kind = 'game'
        Proves = 'The HUD''s POI markers on the head-locked quad: drawn within 1 degree of their target''s direction inside the 16:9 band, off the frame (clamped to its edge) above and below it; the body turned a full circle twice at the e1m3 gate with the map''s POI targets triggered'
        Map = 'game/sp/e1m3_cult/e1m3_cult -checkpoint cp_03_shoot_gate'
        Env = @('ETERNALVR_AIM=hand', 'ETERNALVR_RENDER_SIZE=auto', 'ETERNALVR_TEST_MARKER_LOG=1',
                "ETERNALVR_DEBUG_COMMANDS=$script:QaMarkerSchedule")
        UiCaptures = 15
        Input = $script:QaMarkerRest
        Timeline = {
            param($c)
            if (Wait-QaLog $c '(?m)debug-commands: [\d.]+ s: setviewpos [-\d. ]+ 30\r?$' 60) {
                Wait-QaSeconds $c 0.6; Save-QaShot $c 'yaw30'; Save-QaXrShot $c 'yaw30'
            }
            if (-not (Wait-QaLog $c 'debug-commands: [\d.]+ s: where' 90)) { Add-QaNote $c "the schedule's end was not logged" }
            Wait-QaSeconds $c 1
        }
        Asserts = {
            param($c)
            Test-QaPresent $c 'hud-markers: the HUD matrix build hooked at RVA'
            Test-QaAbsent $c 'hud-markers: not installed'
            $quad = Get-QaMarkerQuad $c
            Add-QaResult $c 'the layer logged the quad the markers follow' ($null -ne $quad) ''
            if ($quad) { Test-QaMarkerDirections $c $quad }
        }
    }
)

# Before the launcher's end-to-end suite, which stays last.
$e2e = @($script:QaScenarios | Where-Object { $_.Kind -eq 'e2e' })
$script:QaScenarios = @($script:QaScenarios | Where-Object { $_.Kind -ne 'e2e' }) + $markerScenarios + $e2e
