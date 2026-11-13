# The depth layer (ETERNALVR_DEPTH_LAYER, docs/ARCHITECTURE.md section 6 "Depth", src/vkcore/presenter_depth.hpp).
# Dot-sourced by qa-suite.ps1 after qa-scenarios-pickup.ps1: added to $script:QaScenarios before the launcher's
# end-to-end suite.
#
# OpenXR-Simulator lists XR_KHR_composition_layer_depth and takes depth swapchains; it composes colour only, so what
# the scenarios check is that the depth is made and submitted (the layer's lines, no failed xrEndFrame, no fall-back
# to colour only) and that what reaches the runtime's depth image is each eye's scene: ETERNALVR_TEST_DEPTH_READBACK=1
# copies the depth swapchain's image itself (as the runtime gets it, after the layer's copy into it) back every 10 s
# and logs each eye's distances.
#
#   depth-layer       e1m2's start (the hall, the sky above it): the layer on with the start line's format and buffers,
#                     the first copy, the depth swapchain and the first frame with depth; every 10 s line after the
#                     first has 90% or more of its projection frames with depth and eyes copied, one with timed
#                     copies; no `off for this game`, no failed xrEndFrame, no error; and over the read-backs (two or
#                     more): none with an invalid texel or a distance nearer than the 0.24 m clamp, each eye with
#                     some sky in at least one, the median of their medians between 0.5 and 300 m, and the median of
#                     their equal-texel shares under 50%. The timeline waits for 3 such lines and 2 read-backs (40 s
#                     at least, 75 s at most), not a fixed time. Positive control (of the copy's eye halves, the
#                     Vulkan stage only): -GameEnv ETERNALVR_TEST_DEPTH_FAULT=1 (eye R's copy also into eye L's half)
#                     fails the last check.
#   depth-layer-dlss  the same with DLSS Quality per eye (the eyes render at about 2/3 of the output size)

$script:QaDepthTenSeconds = 'depth: last 10 s: (\d+) of (\d+) projection frame\(s\) with depth; (\d+) new image\(s\) with depth, (\d+) without; eyes copied (\d+), not copied (\d+) \(last reason: ([^)]*)\); copy GPU time ([\d.]+) ms average, ([\d.]+) ms longest \((\d+) copies timed\)'
$script:QaDepthEye = '([\d.]+)% far \(sky\), ([\d.]+)% at the nearest value, (\d+) invalid; ([\d.-]+) to ([\d.-]+) m, median ([\d.-]+) m, centre ([\d.-]+) m'
$script:QaDepthMinSeconds = 40
$script:QaDepthMaxSeconds = 75

# The read-back lines: each eye's numbers and the share of equal texels.
function Get-QaDepthReadBacks($Ctx) {
    $pattern = "depth: read back from the depth swapchain image \(every \d+ texel\(s\)\): eye L: $($script:QaDepthEye); eye R: $($script:QaDepthEye); the eyes hold the same value in ([\d.]+)% "
    foreach ($line in @(Get-QaMatches $Ctx 'depth: read back from the depth swapchain image')) {
        if ($line -notmatch $pattern) { continue }
        $eye = { param($o) [pscustomobject]@{
                Far = [double]$Matches[$o + 1]; Nearest = [double]$Matches[$o + 2]; Invalid = [int]$Matches[$o + 3]
                Min = [double]$Matches[$o + 4]; Max = [double]$Matches[$o + 5]; Median = [double]$Matches[$o + 6]
                Centre = [double]$Matches[$o + 7] } }
        [pscustomobject]@{ L = (& $eye 0); R = (& $eye 7); Same = [double]$Matches[15]; Line = $line.Trim() }
    }
}

function Get-QaMedian([double[]]$Values) {
    if ($Values.Count -eq 0) { return [double]::NaN }
    $sorted = @($Values | Sort-Object)
    return $sorted[[int][Math]::Floor($sorted.Count / 2)]
}

# Until 3 depth lines and 2 read-backs are in the log (the first line may come before the map), within the bounds.
function Wait-QaDepthLines($Ctx) {
    $start = Get-Date
    Wait-QaSeconds $Ctx $script:QaDepthMinSeconds
    while ($true) {
        $lines = @(Get-QaMatches $Ctx $script:QaDepthTenSeconds -Regex).Count
        $reads = @(Get-QaDepthReadBacks $Ctx).Count
        $waited = ((Get-Date) - $start).TotalSeconds
        if ($lines -ge 3 -and $reads -ge 2) {
            Write-QaLine ('  depth: {0} 10 s line(s) and {1} read-back(s) after {2:N0} s' -f $lines, $reads, $waited)
            return
        }
        if ($waited -ge $script:QaDepthMaxSeconds) {
            Add-QaNote $Ctx ('depth: {0} 10 s line(s), {1} read-back(s) at the {2} s cap' -f $lines, $reads, $script:QaDepthMaxSeconds)
            return
        }
        Wait-QaSeconds $Ctx ([Math]::Min(1.0, $script:QaDepthMaxSeconds - $waited))
    }
}

$script:QaDepthAsserts = {
    param($c)
    Test-QaPresent $c 'depth: depth layer on: D32_FLOAT' -Regex
    Test-QaPresent $c 'depth: first copy: _upscaledOpaqueDepth is image'
    Test-QaPresent $c 'depth: depth swapchain \d+x\d+ ' -Regex
    Test-QaPresent $c 'depth: first frame submitted with depth'
    Test-QaAbsent $c 'the depth layer is off for this game'
    Test-QaAbsent $c 'xr: xrEndFrame failed'
    Test-QaAbsent $c 'ERROR|exception|crash' -Regex -CaseSensitive
    $lines = @(Get-QaMatches $c $script:QaDepthTenSeconds -Regex)
    # The first line may hold the frames before the first eye pair (loading, the map's start).
    $later = @($lines | Select-Object -Skip 1)
    $bad = @($later | Where-Object {
            $null = $_ -match $script:QaDepthTenSeconds
            $with = [double]$Matches[1]; $all = [double]$Matches[2]
            $all -eq 0 -or $with / $all -lt 0.9 -or [int]$Matches[5] -eq 0 })
    Add-QaResult $c 'every 10 s line after the first: 90% of the projection frames or more with depth, eyes copied' ($later.Count -ge 2 -and $bad.Count -eq 0) "$($later.Count) line(s), $($bad.Count) short; last: $(if ($lines.Count) { $lines[-1].Trim() })"
    $timed = @($later | Where-Object { $null = $_ -match $script:QaDepthTenSeconds; [int]$Matches[10] -gt 0 })
    Add-QaResult $c 'the copies have their GPU time' ($timed.Count -ge 1) "$($timed.Count) line(s) with timed copies"
    $reads = @(Get-QaDepthReadBacks $c)
    Add-QaResult $c 'two read-backs or more' ($reads.Count -ge 2) "$($reads.Count)"
    if ($reads.Count -eq 0) { return }
    $eyes = @($reads | ForEach-Object { $_.L; $_.R })
    $broken = @($eyes | Where-Object { $_.Invalid -ne 0 -or $_.Min -lt 0.24 })
    Add-QaResult $c 'read-backs: no invalid texel, nothing nearer than the clamp' ($broken.Count -eq 0) "$($broken.Count) of $($eyes.Count) eye(s) off"
    $skyL = @($reads | Where-Object { $_.L.Far -gt 0 }).Count
    $skyR = @($reads | Where-Object { $_.R.Far -gt 0 }).Count
    Add-QaResult $c 'read-backs: each eye shows some sky at the far plane in one or more' ($skyL -gt 0 -and $skyR -gt 0) "eye L in $skyL, eye R in $skyR of $($reads.Count)"
    $median = Get-QaMedian @($eyes | ForEach-Object { $_.Median })
    Add-QaResult $c 'read-backs: the median distance is a scene''s (0.5 to 300 m)' ($median -ge 0.5 -and $median -le 300) ("{0:N2} m; last: {1}" -f $median, $reads[-1].Line)
    $same = Get-QaMedian @($reads | ForEach-Object { $_.Same })
    Add-QaResult $c 'read-backs: the eyes hold their own depth (median equal share outside the sky under 50%)' ($same -lt 50) ("{0:N1}% equal" -f $same)
}

$depthScenarios = @(
    @{
        Name = 'depth-layer'; Kind = 'game'
        Proves = 'The depth layer on (ETERNALVR_DEPTH_LAYER=1, as the launcher''s Depth for headset smoothing): each eye''s depth reaches the runtime with its projection view in nearly every frame, without a failed xrEndFrame or a fall-back, and the runtime''s depth image read back holds each eye''s own scene in metres (sky at the far plane, the near band clamped, the eyes different)'
        Env = @('ETERNALVR_DEPTH_LAYER=1', 'ETERNALVR_TEST_DEPTH_READBACK=1')
        Timeline = { param($c) Wait-QaDepthLines $c; Save-QaXrShot $c 'end' }
        Asserts = $script:QaDepthAsserts
    },
    @{
        Name = 'depth-layer-dlss'; Kind = 'game'
        Proves = 'The same with DLSS Quality per eye: _upscaledOpaqueDepth is the depth at the output size also when the eyes render smaller'
        Env = @('ETERNALVR_DEPTH_LAYER=1', 'ETERNALVR_TEST_DEPTH_READBACK=1', 'ETERNALVR_STEREO_DLSS=1', 'ETERNALVR_STEREO_DLSS_QUALITY=quality')
        Args = @('+r_antialiasing 2')
        Timeline = { param($c) Wait-QaDepthLines $c; Save-QaXrShot $c 'end' }
        Asserts = $script:QaDepthAsserts
    }
)

# Before the launcher's end-to-end suite, which stays last.
$e2e = @($script:QaScenarios | Where-Object { $_.Kind -eq 'e2e' })
$script:QaScenarios = @($script:QaScenarios | Where-Object { $_.Kind -ne 'e2e' }) + $depthScenarios + $e2e
