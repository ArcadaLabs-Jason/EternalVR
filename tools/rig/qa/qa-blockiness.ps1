<#
.SYNOPSIS
The coarse-shading blocks in eye captures (ETERNALVR_CAPTURE_EYES): per eye and image half, the block-edge
step ratio, about 1 without blocks and above it with them.

.DESCRIPTION
Each image is read as grey levels. The mean absolute step between neighbouring pixels is taken for every
column boundary (horizontal steps) and every row boundary (vertical steps) of an image half. Shading at
4x4 repeats one value over blocks of 4 x 4 render pixels, which are -Period pixels of the captured image (4
times the image's size over the render size: 4 at the eye image's own size, 6 with DLSS Quality). Grouped
by their position modulo -Period, the boundaries on block edges step more than the others: the ratio of the
largest group's mean to the mean of the other groups. The largest group is the block edges whatever the
blocks' phase (DLSS jitters it); without blocks the groups differ by noise only. The half's ratio is the
mean of the horizontal and the vertical one, averaged over the captures after the first -Skip.

Run by the foveation-eyes scenarios (qa-scenarios.ps1), or on any capture folder; prints Images, LeftL,
RightL (eye L's left and right half), LeftR and RightR:
  powershell -NoProfile -ExecutionPolicy Bypass -File tools\rig\qa\qa-blockiness.ps1 -Dir <folder> [-Period 6] [-Skip 3]

.PARAMETER Dir
A folder of <name>-L.png and <name>-R.png pairs (eye L and eye R of one stereo pair).

.PARAMETER Period
The 4x4 block's size in captured pixels (default 4).

.PARAMETER Skip
Captures left out at the start, sorted by name (default 3: the first ones can predate the rate images).
#>
param(
    [string]$Dir = '',
    [int]$Period = 4,
    [int]$Skip = 3
)

if (-not ('QaBlocks' -as [type])) {
    Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class QaBlocks {
    // Grey levels (0.299 R + 0.587 G + 0.114 B), row-major.
    public static float[] Grey(string path, out int width, out int height) {
        using (var bmp = new Bitmap(path)) {
            width = bmp.Width; height = bmp.Height;
            var data = bmp.LockBits(new Rectangle(0, 0, width, height), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            try {
                var bytes = new byte[data.Stride * height];
                Marshal.Copy(data.Scan0, bytes, 0, bytes.Length);
                var grey = new float[width * height];
                for (int y = 0; y < height; ++y) {
                    int row = y * data.Stride;
                    for (int x = 0; x < width; ++x) {
                        int p = row + x * 4; // B, G, R, A
                        grey[y * width + x] = 0.114f * bytes[p] + 0.587f * bytes[p + 1] + 0.299f * bytes[p + 2];
                    }
                }
                return grey;
            } finally { bmp.UnlockBits(data); }
        }
    }
    // The largest group's mean step over the other groups' mean, the boundaries grouped by index modulo period.
    static double Ratio(double[] steps, int period) {
        int n = steps.Length / period * period;
        if (period < 2 || n == 0) return 1.0;
        var sums = new double[period];
        for (int i = 0; i < n; ++i) sums[i % period] += steps[i];
        int best = 0;
        for (int k = 1; k < period; ++k) if (sums[k] > sums[best]) best = k;
        double others = 0;
        for (int k = 0; k < period; ++k) if (k != best) others += sums[k];
        others /= period - 1;
        return others > 0 ? sums[best] / others : 1.0;
    }
    // The ratio of columns [x0, x1) of the image: the mean of the horizontal and the vertical one.
    public static double HalfRatio(float[] g, int width, int height, int x0, int x1, int period) {
        var across = new double[Math.Max(0, x1 - x0 - 1)];
        for (int x = x0; x < x1 - 1; ++x) {
            double sum = 0;
            for (int y = 0; y < height; ++y) sum += Math.Abs(g[y * width + x + 1] - g[y * width + x]);
            across[x - x0] = sum / height;
        }
        var down = new double[Math.Max(0, height - 1)];
        for (int y = 0; y < height - 1; ++y) {
            double sum = 0;
            for (int x = x0; x < x1; ++x) sum += Math.Abs(g[(y + 1) * width + x] - g[y * width + x]);
            down[y] = sum / (x1 - x0);
        }
        return (Ratio(across, period) + Ratio(down, period)) / 2;
    }
}
'@
}

# Per eye and half: Images, LeftL, RightL (eye L's left and right half), LeftR, RightR; $null without captures.
function Measure-QaBlockiness([string]$Dir, [int]$Period = 4, [int]$Skip = 3) {
    $pairs = @(Get-ChildItem -LiteralPath $Dir -Filter '*-L.png' -File -ErrorAction SilentlyContinue | Sort-Object Name |
        Where-Object { Test-Path -LiteralPath ($_.FullName -replace '-L\.png$', '-R.png') } | Select-Object -Skip $Skip)
    if ($pairs.Count -eq 0) { return $null }
    $sum = @{ LeftL = 0.0; RightL = 0.0; LeftR = 0.0; RightR = 0.0 }
    foreach ($p in $pairs) {
        foreach ($eye in 'L', 'R') {
            $path = if ($eye -eq 'L') { $p.FullName } else { $p.FullName -replace '-L\.png$', '-R.png' }
            $w = 0; $h = 0
            $g = [QaBlocks]::Grey($path, [ref]$w, [ref]$h)
            $half = [int][Math]::Floor($w / 2)
            $sum["Left$eye"] += [QaBlocks]::HalfRatio($g, $w, $h, 0, $half, $Period)
            $sum["Right$eye"] += [QaBlocks]::HalfRatio($g, $w, $h, $half, $w, $Period)
        }
    }
    $n = $pairs.Count
    return [pscustomobject]@{ Images = $n; LeftL = $sum.LeftL / $n; RightL = $sum.RightL / $n
                              LeftR = $sum.LeftR / $n; RightR = $sum.RightR / $n }
}

if (-not $Dir) { throw 'no -Dir (a folder of <name>-L.png and <name>-R.png capture pairs)' }
$m = Measure-QaBlockiness $Dir $Period $Skip
if (-not $m) { throw "no capture pairs in $Dir after the first $Skip" }
$m
