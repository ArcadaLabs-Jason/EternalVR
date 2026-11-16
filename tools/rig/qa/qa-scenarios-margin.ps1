# The edge margin (ETERNALVR_EDGE_MARGIN, docs/VR_STEREO.md "Edge margin"; the launcher's Edge margin). Dot-sourced by
# qa-suite.ps1 after qa-scenarios-depth.ps1: added to $script:QaScenarios before the launcher's end-to-end suite.
#
# OpenXR-Simulator composes the projection layer with the FOV it is given and does no reprojection, so a fast turn's
# black edges cannot be shown on it. What the scenario checks is everything up to the runtime: each eye's FOV widened
# by the margin's factor, the projection layer submitted with exactly that FOV, the automatic render size grown by the
# same factor (the centre's pixels keep their size), the image holding the scene in the margin (the engine renders
# the whole wider image: no black or empty band), and the desktop window cut to the view.
#
#   edge-margin  e1m2's start with Medium (x1.20): the lines above, and both-eye captures (every 30 pairs) whose
#                bands outside the view's rectangle (the margin, from the log's FOVs) hold the scene: on the left and
#                right of each eye a fifth of the pixels lit or more and their mean a fifth of the view's or more
#                (the map's fade-in is left out: each eye's captures before its view first reaches 85% of its median
#                brightness). The
#                render size is the launcher's auto, so the simulator's 1280x1400 grows to 1536x1680

$script:QaMarginRenders = 'edge margin: eye (\d) renders left ([\d.-]+) right ([\d.-]+) up ([\d.-]+) down ([\d.-]+) deg for its view''s ([\d.-]+) / ([\d.-]+) / ([\d.-]+) / ([\d.-]+)'
$script:QaMarginSubmitted = 'edge margin: the projection layer''s eye (\d) is submitted with left ([\d.-]+) right ([\d.-]+) up ([\d.-]+) down ([\d.-]+) deg over its (\d+)x(\d+) half'
$script:QaMarginCaptureEvery = 30

# Each band of a capture outside a rectangle (its margin) against the inside: the mean of the pixels' largest channel
# (0 to 255) and the share of pixels whose largest channel is 16 or more, inside and in the left, right, top and
# bottom bands (the full-height left and right bands, the top and bottom bands between them).
if (-not ('QaMarginBands' -as [type])) {
    Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class QaMarginBands {
    // [width, height, inside mean, inside lit, left mean, left lit, right mean, right lit, top mean, top lit,
    //  bottom mean, bottom lit]; the rectangle as fractions of the image (x, y from the top-left, width, height).
    public static double[] Measure(string path, double fx, double fy, double fw, double fh) {
        using (var b = new Bitmap(path)) {
            int w = b.Width, h = b.Height;
            int x0 = (int)Math.Round(fx * w), y0 = (int)Math.Round(fy * h);
            int x1 = (int)Math.Round((fx + fw) * w), y1 = (int)Math.Round((fy + fh) * h);
            var d = b.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.ReadOnly, PixelFormat.Format24bppRgb);
            byte[] p;
            int stride = d.Stride;
            try { p = new byte[stride * h]; Marshal.Copy(d.Scan0, p, 0, p.Length); } finally { b.UnlockBits(d); }
            var sum = new double[5];
            var lit = new long[5];
            var n = new long[5];
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    int k = x < x0 ? 1 : x >= x1 ? 2 : y < y0 ? 3 : y >= y1 ? 4 : 0;
                    int o = y * stride + x * 3;
                    int v = Math.Max(p[o], Math.Max(p[o + 1], p[o + 2]));
                    sum[k] += v; n[k]++; if (v >= 16) lit[k]++;
                }
            }
            var r = new double[12];
            r[0] = w; r[1] = h;
            for (int k = 0; k < 5; k++) {
                r[2 + 2 * k] = n[k] > 0 ? sum[k] / n[k] : -1;
                r[3 + 2 * k] = n[k] > 0 ? (double)lit[k] / n[k] : -1;
            }
            return r;
        }
    }
}
'@
}

# The view's rectangle in an eye's image, as fractions (x, y from the top-left, width, height), from the degrees of
# one `edge margin: eye N renders` line: the view's tangents within the image's.
function Get-QaMarginRect([double[]]$Degrees) {
    $t = @($Degrees | ForEach-Object { [Math]::Tan($_ * [Math]::PI / 180.0) })
    # 0..3: the image's left, right, up, down; 4..7: the view's.
    $w = $t[1] - $t[0]; $h = $t[2] - $t[3]
    return [pscustomobject]@{ X = ($t[4] - $t[0]) / $w; Y = ($t[2] - $t[6]) / $h; W = ($t[5] - $t[4]) / $w; H = ($t[6] - $t[7]) / $h }
}

function Get-QaMarginEyes($Ctx) {
    $eyes = @{}
    foreach ($line in @(Get-QaMatches $Ctx $script:QaMarginRenders -Regex)) {
        if ($line -notmatch $script:QaMarginRenders) { continue }
        $deg = @(2..9 | ForEach-Object { [double]::Parse($Matches[$_], [Globalization.CultureInfo]::InvariantCulture) })
        $eyes[[int]$Matches[1]] = [pscustomobject]@{ Degrees = $deg; Rect = (Get-QaMarginRect $deg) }
    }
    return $eyes
}

$script:QaMarginAsserts = {
    param($c)
    Test-QaPresent $c 'edge margin: x1.20 (medium): each eye renders'
    Test-QaAbsent $c 'cannot be widened'
    Test-QaAbsent $c 'xr: xrEndFrame failed'
    Test-QaAbsent $c 'ERROR|exception|crash' -Regex -CaseSensitive
    $eyes = Get-QaMarginEyes $c
    Add-QaResult $c 'both eyes widened' ($eyes.ContainsKey(0) -and $eyes.ContainsKey(1)) "$($eyes.Count) eye line(s)"
    # x1.20 in tangent: each side's tangent 1.2 times the view's.
    foreach ($e in @($eyes.Keys | Sort-Object)) {
        $d = $eyes[$e].Degrees
        $ratios = @(0..3 | ForEach-Object { [Math]::Tan($d[$_] * [Math]::PI / 180.0) / [Math]::Tan($d[$_ + 4] * [Math]::PI / 180.0) })
        $bad = @($ratios | Where-Object { [Math]::Abs($_ - 1.2) -gt 0.002 })
        Add-QaResult $c "eye ${e}: every tangent x1.20" ($bad.Count -eq 0) (($ratios | ForEach-Object { '{0:N4}' -f $_ }) -join ' / ')
    }
    # The layer's views carry the rendered FOV.
    $submitted = @{}
    foreach ($line in @(Get-QaMatches $c $script:QaMarginSubmitted -Regex)) {
        if ($line -match $script:QaMarginSubmitted) {
            $submitted[[int]$Matches[1]] = @(2..5 | ForEach-Object { [double]::Parse($Matches[$_], [Globalization.CultureInfo]::InvariantCulture) })
        }
    }
    foreach ($e in 0, 1) {
        $ok = $submitted.ContainsKey($e) -and $eyes.ContainsKey($e) -and
            @(0..3 | Where-Object { [Math]::Abs($submitted[$e][$_] - $eyes[$e].Degrees[$_]) -gt 0.011 }).Count -eq 0
        $detail = if ($submitted.ContainsKey($e)) { ($submitted[$e] -join ' / ') } else { 'no submitted line' }
        Add-QaResult $c "eye ${e}: the projection layer is submitted with the widened FOV" $ok $detail
    }
    # The automatic size grows by the factor: the simulator's 1280x1400 becomes 1536x1680.
    Test-QaPresent $c 'render size auto x1\.00, edge margin x1\.20 gives 1536x1680' -Regex
    # The window shows the view: its box is 1/1.2 of the image on each side.
    $mirror = Get-QaMatches $c 'mirror: the window shows the eye''s view without the edge margin: (\d+)x(\d+) at (\d+),(\d+) of the (\d+)x(\d+) image' -Regex | Select-Object -First 1
    $boxOk = $false
    if ($mirror -match '(\d+)x(\d+) at (\d+),(\d+) of the (\d+)x(\d+) image') {
        $boxOk = [Math]::Abs([double]$Matches[1] / [double]$Matches[5] - 1 / 1.2) -lt 0.01 -and
                 [Math]::Abs([double]$Matches[2] / [double]$Matches[6] - 1 / 1.2) -lt 0.01
    }
    Add-QaResult $c 'the desktop window is cut to the view (1/1.2 of each side)' $boxOk "$(if ($mirror) { $mirror.Trim() } else { 'not logged' })"
    # The captures: the scene in the margin.
    $files = @(Get-ChildItem -LiteralPath "$($c.Stage)-eyes" -Filter '*.png' -File -ErrorAction SilentlyContinue | Sort-Object Name)
    $all = @()
    foreach ($f in $files) {
        $e = if ($f.Name -like '*-L.png') { 0 } elseif ($f.Name -like '*-R.png') { 1 } else { -1 }
        if ($e -lt 0 -or -not $eyes.ContainsKey($e)) { continue }
        $r = $eyes[$e].Rect
        $all += [pscustomobject]@{ File = $f; Eye = $e; M = [QaMarginBands]::Measure($f.FullName, $r.X, $r.Y, $r.W, $r.H) }
    }
    # The map's fade-in (black, then darker than the scene) is left out: each eye's leading captures, until its view
    # first reaches 85% of that eye's median brightness. Every capture after that counts, dim ones included; a black
    # margin stays black in all of them.
    $median = @{}
    foreach ($e in @($all | ForEach-Object { $_.Eye } | Sort-Object -Unique)) {
        $views = @($all | Where-Object { $_.Eye -eq $e } | ForEach-Object { $_.M[2] } | Sort-Object)
        $median[$e] = $views[[int][Math]::Floor($views.Count / 2)]
    }
    $settled = @{}
    $measured = 0
    $short = @()
    foreach ($a in $all) {
        $f = $a.File; $e = $a.Eye; $m = $a.M
        $insideMean = $m[2]
        if (-not $settled[$e]) {
            if ($insideMean -lt 0.85 * $median[$e]) { continue }
            $settled[$e] = $true
        }
        ++$measured
        foreach ($band in @(@('left', 4), @('right', 6))) {
            $mean = $m[$band[1]]; $lit = $m[$band[1] + 1]
            if ($lit -lt 0.2 -or $mean -lt 0.2 * $insideMean) {
                $short += ('{0} eye {1} {2}: mean {3:N1} (view {4:N1}), {5:P0} lit' -f $f.Name, $e, $band[0], $mean, $insideMean, $lit)
            }
        }
        if ($measured -le 2) {
            Add-QaNote $c ('{0}: {1}x{2}; view mean {3:N1}; margin left {4:N1} ({5:P0} lit), right {6:N1} ({7:P0}), top {8:N1} ({9:P0}), bottom {10:N1} ({11:P0})' -f
                $f.Name, $m[0], $m[1], $m[2], $m[4], $m[5], $m[6], $m[7], $m[8], $m[9], $m[10], $m[11])
        }
    }
    Add-QaNote $c ('eye captures: {0} measured, {1} of the fade-in left out (before the view first reached 85% of its eye''s median, {2})' -f
        $measured, ($all.Count - $measured), (($median.Keys | Sort-Object | ForEach-Object { 'eye {0} {1:N1}' -f $_, $median[$_] }) -join ', '))
    Add-QaResult $c 'eye captures measured (6 or more images)' ($measured -ge 6) "$measured image(s)"
    Add-QaResult $c 'the margin holds the scene: left and right bands a fifth lit or more, with a fifth of the view''s brightness or more' ($measured -gt 0 -and $short.Count -eq 0) $(if ($short.Count) { ($short | Select-Object -First 4) -join '; ' } else { "$measured image(s)" })
}

$marginScenarios = @(
    @{
        Name = 'edge-margin'; Kind = 'game'
        Proves = 'The edge margin at Medium (ETERNALVR_EDGE_MARGIN=medium, as the launcher passes it): each eye''s FOV tangents x1.20, the projection layer submitted with that FOV, the automatic render size grown x1.20, the scene in the margin of both eyes'' captures, and the desktop window cut to the view'
        Env = @('ETERNALVR_EDGE_MARGIN=medium', 'ETERNALVR_RENDER_SIZE=auto', 'ETERNALVR_RENDER_SCALE=1.00')
        EyeCaptures = $script:QaMarginCaptureEvery
        Input = $script:QaRestPose
        Timeline = { param($c) Wait-QaSeconds $c 25; Save-QaShot $c 'window'; Save-QaXrShot $c 'end' }
        Asserts = $script:QaMarginAsserts
    }
)

# Before the launcher's end-to-end suite, which stays last.
$e2e = @($script:QaScenarios | Where-Object { $_.Kind -eq 'e2e' })
$script:QaScenarios = @($script:QaScenarios | Where-Object { $_.Kind -ne 'e2e' }) + $marginScenarios + $e2e
