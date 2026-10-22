# The hand HUDs' scenarios (docs/VR_HANDS_HUD.md, "Where the corner blocks are"): the game's HUD Scale held
# at Large in VR (stereo_seq::stereoHudScaleCvar) and kept out of profile.bin (the suite's profile check), and
# the wrist HUD's cuts where the game drew the corner blocks. Dot-sourced by qa-suite.ps1 after
# qa-scenarios-wheel.ps1: added to $script:QaScenarios before the launcher's end-to-end suite.

# GUI captures against the hand HUD's cuts (ui_layer/hud_blocks.hpp): each drawn shape (alpha at least 16,
# 8-connected, the readback's threshold) that has pixels in a cut must have all of them in it. A shape with
# pixels on both sides is a block left partly on the head-locked HUD, or a neighbour cut in two.
if (-not ('QaHandCuts' -as [type])) {
    Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class QaHandCuts {
    // Per cut (x0, y0, x1, y1 each, flattened): [drawn pixels inside, shapes wholly inside, shapes across its
    // edge, pixels of those outside it].
    public static long[] Check(string path, int[] cuts, out int width, out int height) {
        byte[] a;
        using (var bmp = new Bitmap(path)) {
            width = bmp.Width; height = bmp.Height;
            var data = bmp.LockBits(new Rectangle(0, 0, width, height), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            try {
                var bytes = new byte[data.Stride * height];
                Marshal.Copy(data.Scan0, bytes, 0, bytes.Length);
                a = new byte[width * height];
                for (int y = 0; y < height; ++y)
                    for (int x = 0; x < width; ++x) a[y * width + x] = bytes[y * data.Stride + x * 4 + 3];
            } finally { bmp.UnlockBits(data); }
        }
        int n = cuts.Length / 4;
        var result = new long[n * 4];
        var label = new int[width * height];
        var stack = new Stack<int>();
        int next = 0;
        var inside = new long[n];
        var outside = new long[n];
        for (int start = 0; start < a.Length; ++start) {
            if (a[start] < 16 || label[start] != 0) continue;
            ++next;
            Array.Clear(inside, 0, n);
            Array.Clear(outside, 0, n);
            label[start] = next;
            stack.Push(start);
            while (stack.Count > 0) {
                int p = stack.Pop();
                int px = p % width, py = p / width;
                for (int k = 0; k < n; ++k) {
                    bool inCut = px >= cuts[4 * k] && px < cuts[4 * k + 2] && py >= cuts[4 * k + 1] && py < cuts[4 * k + 3];
                    if (inCut) ++inside[k]; else ++outside[k];
                }
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        int qx = px + dx, qy = py + dy;
                        if (qx < 0 || qy < 0 || qx >= width || qy >= height) continue;
                        int q = qy * width + qx;
                        if (a[q] >= 16 && label[q] == 0) { label[q] = next; stack.Push(q); }
                    }
            }
            for (int k = 0; k < n; ++k) {
                if (inside[k] == 0) continue;
                result[4 * k] += inside[k];
                if (outside[k] == 0) ++result[4 * k + 1];
                else { ++result[4 * k + 2]; result[4 * k + 3] += outside[k]; }
            }
        }
        return result;
    }
}
'@
}

# The band of a GUI target (ui_layer::wideContentRect), as x, y, width, height.
function Get-QaBand([int]$Width, [int]$Height) {
    $bh = [math]::Round($Width * 9.0 / 16.0)
    if ($Height -gt $bh * 1.005) { return @(0, [int][math]::Floor(($Height - $bh) / 2), $Width, [int]$bh) }
    $bw = [math]::Round($Height * 16.0 / 9.0)
    if ($Width -gt $bw * 1.005) { return @([int][math]::Floor(($Width - $bw) / 2), 0, [int]$bw, $Height) }
    return @(0, 0, $Width, $Height)
}

# The last cuts the layer logged ("... HUD cuts the corner blocks where the game drew them: health and armor GUI
# rect (x, y) w x h, ammo (x, y) w x h"): Cuts, x0, y0, x1, y1 per non-empty cut; Vitals, the health block's
# cut's right edge (0 when it is not cut); $null when none was logged.
function Get-QaHandCuts($Ctx) {
    $line = @(Get-QaMatches $Ctx 'HUD cuts the corner blocks where the game drew them' | Select-Object -Last 1)
    if ($line.Count -eq 0 -or $line[0] -notmatch 'health and armor GUI rect \((-?\d+), (-?\d+)\) (\d+) x (\d+), ammo \((-?\d+), (-?\d+)\) (\d+) x (\d+)') {
        return $null
    }
    $m = $Matches
    $cuts = @()
    foreach ($i in @(1, 5)) {
        $x = [int]$m[$i]; $y = [int]$m[$i + 1]; $w = [int]$m[$i + 2]; $h = [int]$m[$i + 3]
        if ($w -gt 0 -and $h -gt 0) { $cuts += @($x, $y, ($x + $w), ($y + $h)) }
    }
    $vitals = if ([int]$m[3] -gt 0) { [int]$m[1] + [int]$m[3] } else { 0 }
    return [pscustomobject]@{ Cuts = $cuts; Vitals = $vitals }
}

# The newest GUI captures with the HUD drawn in every cut (not a menu or a loading screen filling them): no
# drawn shape across a cut's edge. $Large: the vitals cut reaches past 0.215 of the band's width, where the
# fixed cut made for Medium ended (the Large HUD drawn).
function Test-QaHandCutCaptures($Ctx, [switch]$Large) {
    $name = 'the GUI captures show no shape across a hand HUD cut'
    $logged = Get-QaHandCuts $Ctx
    if (-not $logged -or $logged.Cuts.Count -eq 0) { Add-QaResult $Ctx $name $false 'no cut logged'; return }
    $cuts = $logged.Cuts
    $files = @(Get-ChildItem -LiteralPath "$($Ctx.Stage)-ui" -Filter 'ui-*.png' -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending)
    $checked = 0; $bad = @(); $w = 0; $h = 0
    foreach ($f in $files) {
        if ($checked -ge 5) { break }
        try { $r = [QaHandCuts]::Check($f.FullName, [int[]]$cuts, [ref]$w, [ref]$h) } catch { continue }
        $k = $cuts.Count / 4
        $full = $false
        for ($i = 0; $i -lt $k; $i++) {
            $area = ($cuts[4 * $i + 2] - $cuts[4 * $i]) * ($cuts[4 * $i + 3] - $cuts[4 * $i + 1])
            if ($r[4 * $i] -eq 0 -or $r[4 * $i] -gt 0.9 * $area) { $full = $true }
        }
        if ($full) { continue }
        ++$checked
        for ($i = 0; $i -lt $k; $i++) {
            if ($r[4 * $i + 2] -gt 0) { $bad += "$($f.Name) cut $i`: $($r[4 * $i + 2]) shape(s) across its edge, $($r[4 * $i + 3]) pixel(s) outside" }
        }
    }
    $detail = "$checked capture(s) checked against $($cuts.Count / 4) cut(s) $($cuts -join ',')"
    Add-QaResult $Ctx $name ($checked -gt 0 -and $bad.Count -eq 0) "$detail$(if ($bad.Count) { '; ' + ($bad -join '; ') })"
    if ($Large -and $w -gt 0) {
        $band = Get-QaBand $w $h
        $edge = ($logged.Vitals - $band[0]) / [double]$band[2]
        Add-QaResult $Ctx 'the health block is drawn at the Large HUD Scale' ($edge -gt 0.215) ("its cut ends at {0:N3} of the band's width" -f $edge)
    }
}

$hudScenarios = @(
    @{
        Name = 'hand-hud-large'; Kind = 'game'
        Proves = 'In VR the game''s HUD Scale is held at Large (swf_anchorProfile 2) and stays out of profile.bin (the profile check); on the wrist HUD the corner blocks of the Large HUD (the extra lives row, the equipment row) are cut where the game drew them: in the last GUI captures no drawn shape lies across a cut''s edge; an in-headset capture names the cuts'
        Map = 'game/sp/e1m3_cult/e1m3_cult -checkpoint cp_03_shoot_gate'
        Env = @('ETERNALVR_HUD=wrist')
        UiCaptures = 10
        Input = @()
        Timeline = {
            param($c)
            Wait-QaSeconds $c 14
            Save-QaShot $c 'hud'; Save-QaXrShot $c 'hud'
            # The in-headset capture (left Menu held, then a trigger): its text names the cuts.
            Write-QaInput $c @('left.menu = 1'); Wait-QaSeconds $c 0.3
            Write-QaInput $c @('left.menu = 1', 'left.trigger = 1'); Wait-QaSeconds $c 0.3
            Write-QaInput $c @(); Wait-QaSeconds $c 3
        }
        Asserts = {
            param($c)
            Test-QaPresent $c 'cvars: held at run time: .*swf_anchorProfile 2' -Regex
            Test-QaPresent $c 'profile cvar serializer hooked at RVA'
            Test-QaPresent $c 'ui: corner blocks: '
            Test-QaPresent $c 'HUD cuts the corner blocks where the game drew them'
            Test-QaHandCutCaptures $c -Large
            $dir = Join-Path $c.LogsDir 'captures'
            $txt = @(Get-ChildItem -LiteralPath $dir -Filter 'capture-*.txt' -ErrorAction SilentlyContinue) | Select-Object -Last 1
            $text = if ($txt) { Read-QaShared $txt.FullName } else { '' }
            Add-QaResult $c 'the in-headset capture names the hand HUD cuts' ($text -match 'cut for the hand HUD: \(\d+, \d+\) \d+ x \d+') "$(if ($txt) { $txt.Name } else { 'no capture' })"
        }
    }
)

# Before the launcher's end-to-end suite, which stays last.
$e2e = @($script:QaScenarios | Where-Object { $_.Kind -eq 'e2e' })
$script:QaScenarios = @($script:QaScenarios | Where-Object { $_.Kind -ne 'e2e' }) + $hudScenarios + $e2e
