# Piloting the Cultist Base Revenant (docs/VR_CONTROLLERS.md, "Piloting a demon"): what the eyes see while the
# player pilots it. Dot-sourced by qa-suite.ps1 after qa-scenarios.ps1, whose $script:QaCrosshairRest it uses; added
# to $script:QaScenarios before the launcher's end-to-end suite.

# Into the Revenant's pod as crosshair-hold-demon gets there: god mode, the cinematic trigger, the player in front of
# the pod's use proxy; then the weapon hand on the proxy and Use (the stick click).
$script:QaPodCommands = 'ETERNALVR_DEBUG_COMMANDS=3:god|3.5:g_debugTriggers 1|4:trigger cinematic_trigger_trigger_1|6:setviewpos 160.5 20.8 -5.0 90'

# The arms as the launcher sends them by default with motion controllers: hand aim, the free off hand, the bind solve,
# the arms shown.
$script:QaPlayerArms = @('ETERNALVR_AIM=hand', 'ETERNALVR_OFFHAND=free', 'ETERNALVR_ARM_SOLVE=bind', 'ETERNALVR_ARMS=shown')

# The poses held while piloting, each captured (both eyes): the off hand raised in front of the face, and the weapon
# hand (which aims the demon under hand aim) pointing ahead, up, down and to either side while the head looks ahead.
$script:QaPilotPoses = [ordered]@{
    'ahead'      = @('right.aim = 0, 0', 'left.aim = 0, 0')
    'hand-up'    = @('right.aim = 0, 0', 'left.aim = 0, 20', 'left.position = -0.12, -0.12, -0.35')
    'aim-up'     = @('right.aim = 0, 45', 'left.aim = 0, 0')
    'aim-down'   = @('right.aim = 0, -45', 'left.aim = 0, 0')
    'aim-left'   = @('right.aim = 70, 0', 'left.aim = 0, 0')
    'aim-right'  = @('right.aim = -70, 0', 'left.aim = 0, 0')
}

# How far the cannons moved between two both-eye captures: the mean of each pixel's largest channel difference
# (0 to 255) over where a flat screen shows them, the top 35% of eye L's left fifth and of eye R's right fifth.
if (-not ('QaCorners' -as [type])) {
    Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class QaCorners {
    static byte[] Pixels(Bitmap b, out int stride) {
        var d = b.LockBits(new Rectangle(0, 0, b.Width, b.Height), ImageLockMode.ReadOnly, PixelFormat.Format24bppRgb);
        try { stride = d.Stride; var a = new byte[stride * b.Height]; Marshal.Copy(d.Scan0, a, 0, a.Length); return a; }
        finally { b.UnlockBits(d); }
    }
    public static double Diff(string first, string second) {
        using (var a = new Bitmap(first)) using (var b = new Bitmap(second)) {
            if (a.Width != b.Width || a.Height != b.Height) return -1;
            int sa, sb; var pa = Pixels(a, out sa); var pb = Pixels(b, out sb);
            int eye = a.Width / 2, w = eye / 5, top = (int)(a.Height * 0.35);
            double sum = 0; long n = 0;
            for (int y = 0; y < top; y++) {
                for (int x = 0; x < a.Width; x++) {
                    if (x >= w && x < a.Width - w) continue;
                    int d = 0;
                    for (int c = 0; c < 3; c++) d = Math.Max(d, Math.Abs(pa[y * sa + x * 3 + c] - pb[y * sb + x * 3 + c]));
                    sum += d; n++;
                }
            }
            return n > 0 ? sum / n : -1;
        }
    }
}
'@
}

# The cannons stay where a flat screen shows them in every pose of the weapon hand: each pose's capture against the
# 'ahead' one, under the limit (a run with the cannons on the hand's aim measured 34 to 47 in revenant-view).
function Test-QaCannonsStill($Ctx, [double]$Limit) {
    $ahead = Join-Path $Ctx.Out "$($Ctx.Name)-ahead-xr-both.png"
    foreach ($pose in @($script:QaPilotPoses.Keys | Where-Object { $_ -ne 'ahead' })) {
        $shot = Join-Path $Ctx.Out "$($Ctx.Name)-$pose-xr-both.png"
        $d = if ((Test-Path -LiteralPath $ahead) -and (Test-Path -LiteralPath $shot)) { [QaCorners]::Diff($ahead, $shot) } else { -1 }
        Add-QaResult $Ctx "the cannons in '$pose' where they are in 'ahead' (corner difference under $Limit)" ($d -ge 0 -and $d -lt $Limit) ('{0:N2}' -f $d)
    }
}

function Invoke-QaPodEntry($Ctx) {
    $aim = @('right.aim = 0, -40', 'left.aim = 0, 0')
    Write-QaInput $Ctx $aim; Wait-QaSeconds $Ctx 1.2
    Write-QaInput $Ctx ($aim + 'right.click = 1'); Wait-QaSeconds $Ctx 0.3
    Write-QaInput $Ctx $aim
    return (Wait-QaLog $Ctx 'controllers: piloting a demon' 30)
}

function New-QaDemonViewScenario([string]$Name, [string]$Proves, [string[]]$Env, [int]$EyeCaptures = 0) {
    return @{
        Name = $Name; Kind = 'game'
        Proves = $Proves
        Map = 'game/sp/e1m3_cult/e1m3_cult -checkpoint cp_12_possession'
        Env = @($script:QaPlayerArms) + @('ETERNALVR_RENDER_SIZE=auto', $script:QaPodCommands) + @($Env)
        EyeCaptures = $EyeCaptures
        Input = $script:QaCrosshairRest
        Timeline = {
            param($c)
            Wait-QaSeconds $c 9
            if (-not (Invoke-QaPodEntry $c)) { Add-QaNote $c 'no piloting a demon line within 30 s of Use'; return }
            Wait-QaSeconds $c 3
            foreach ($pose in $script:QaPilotPoses.Keys) {
                Write-QaInput $c $script:QaPilotPoses[$pose]; Wait-QaSeconds $c 1.5
                Save-QaXrShot $c $pose
            }
            # A rocket with the weapon hand 35 degrees to the left of where the head looks: it should go where the hand
            # points (the demon's aim), whatever the cannons' place.
            $fire = @('right.aim = 35, 0', 'left.aim = 0, 0')
            Write-QaInput $c ($fire + 'right.trigger = 1'); Wait-QaSeconds $c 0.4
            Save-QaXrShot $c 'fire-a'; Wait-QaSeconds $c 0.3; Save-QaXrShot $c 'fire-b'
            Write-QaInput $c $fire; Wait-QaSeconds $c 1
            # Turned around with the stick, back toward the pod where the Slayer stands.
            Write-QaInput $c @('right.aim = 0, 0', 'left.aim = 0, 0', 'right.stick = 1, 0'); Wait-QaSeconds $c 0.8
            Write-QaInput $c @('right.aim = 0, 0', 'left.aim = 0, 0'); Wait-QaSeconds $c 1.5
            Save-QaXrShot $c 'behind'
            Wait-QaSeconds $c 1
        }
        Asserts = {
            param($c)
            Test-QaPresent $c 'controllers: piloting a demon'
            Test-QaPresent $c 'controllers: demon cannons hook at RVA'
            Test-QaPresent $c 'controllers: demon arms hook at RVA'
            Test-QaPresent $c 'controllers: demon cannons on the view'
            Test-QaPresent $c 'controllers: demon arms on the view'
            if ($script:QaCannonLimits.ContainsKey($c.Name)) { Test-QaCannonsStill $c $script:QaCannonLimits[$c.Name] }
        }
    }
}

# The corner difference each scenario allows (Test-QaCannonsStill); none while the head nods, which moves the view.
$script:QaCannonLimits = @{ 'revenant-view' = 15; 'revenant-side' = 25 }

$demonScenarios = @(
    (New-QaDemonViewScenario 'revenant-view' 'Piloting the Revenant under the launcher''s default arms (hand aim, the free off hand): both eyes captured with the off hand raised in front of the face and the weapon hand pointing ahead, up, down and to the sides' @()),
    (New-QaDemonViewScenario 'revenant-look' 'As revenant-view with the head nodding 40 degrees up and down every 6 s (ETERNALVR_TEST_HEAD_SWAY): eye captures through the nod' @('ETERNALVR_TEST_HEAD_SWAY=0,40,6') 41),
    (New-QaDemonViewScenario 'revenant-side' 'As revenant-view with the head turned 75 degrees to the left (ETERNALVR_TEST_HEAD_SWAY with no sway)' @('ETERNALVR_TEST_HEAD_SWAY=0,0,1,75'))
)

# Rockets with the weapon hand 35 degrees left, then right, of where the head looks, each held for 1.5 s, the game's
# projectile debug drawing on (g_projectileDebug: each rocket's path stays drawn for 10 s): where they start and
# where they land, to compare between builds.
$demonScenarios += @{
    Name = 'revenant-rockets'; Kind = 'game'
    Proves = 'Piloting the Revenant under hand aim: rocket volleys with the weapon hand 35 degrees left and right of the head, the game''s projectile paths drawn, both eyes captured during and after each volley'
    Map = 'game/sp/e1m3_cult/e1m3_cult -checkpoint cp_12_possession'
    Env = @($script:QaPlayerArms) + @('ETERNALVR_RENDER_SIZE=auto', ($script:QaPodCommands + '|7:g_projectileDebug 1|7.5:g_projectileDebugLifetime 10'))
    Input = $script:QaCrosshairRest
    Timeline = {
        param($c)
        Wait-QaSeconds $c 9
        if (-not (Invoke-QaPodEntry $c)) { Add-QaNote $c 'no piloting a demon line within 30 s of Use'; return }
        Wait-QaSeconds $c 3
        foreach ($side in @(@('left', '35, 0'), @('right', '-35, 0'))) {
            $aim = @("right.aim = $($side[1])", 'left.aim = 0, 0')
            Write-QaInput $c $aim; Wait-QaSeconds $c 1
            Write-QaInput $c ($aim + 'right.trigger = 1')
            foreach ($k in 1..3) { Wait-QaSeconds $c 0.5; Save-QaXrShot $c "$($side[0])-hold$k" }
            Write-QaInput $c $aim
            Wait-QaSeconds $c 0.5; Save-QaXrShot $c "$($side[0])-after1"
            Wait-QaSeconds $c 1.5; Save-QaXrShot $c "$($side[0])-after2"
        }
    }
    Asserts = {
        param($c)
        Test-QaPresent $c 'controllers: piloting a demon'
        Test-QaPresent $c 'controllers: action fire'
    }
}

# Before the launcher's end-to-end suite, which stays last.
$e2e = @($script:QaScenarios | Where-Object { $_.Kind -eq 'e2e' })
$script:QaScenarios = @($script:QaScenarios | Where-Object { $_.Kind -ne 'e2e' }) + $demonScenarios + $e2e
