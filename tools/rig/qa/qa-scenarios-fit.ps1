# Controller fit scenarios (T-225, docs/VR_CONTROLLERS.md "Controller fit"): the same scripted hands with the
# simulator reporting Touch, then Valve Index controllers. Dot-sourced by qa-suite.ps1: added to
# $script:QaScenarios before the launcher's end-to-end suite. OpenXR-Simulator reports Touch whenever Touch
# bindings are suggested, else the first hand profile suggested, so ETERNALVR_TEST_PROFILES=valve_index gets it
# to report the Index; ETERNALVR_TEST_RUNTIME_NAME=SteamVR in both runs, as the fit applies on SteamVR only.
# fit-index reads fit-touch's arm dump from the same suite folder, so run them together and in this order.
# fingers-index (T-226) stands alone: the Index's off-hand fingers on the grip's curl, apart from its press.

# Both hands ahead and level at their rest positions; the launcher's angles at 0 (the layer's default).
$script:QaFitInput = @('right.aim = 0, 0', 'left.aim = 0, 0')
$script:QaFitEnv = @('ETERNALVR_AIM=hand', 'ETERNALVR_HUD=wrist', 'ETERNALVR_TEST_RUNTIME_NAME=SteamVR')

# The arm dump's last line with both grips and the shot ray (features/arm/arm_dump_line.hpp); null for none.
function Get-QaFitDump([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return $null }
    $last = $null
    foreach ($line in (Read-QaShared $Path) -split "`n") {
        if (-not $line.Trim()) { continue }
        try { $d = $line | ConvertFrom-Json } catch { continue }
        if ($null -ne $d.grip -and $null -ne $d.offgrip -and $null -ne $d.aim) { $last = $d }
    }
    return $last
}

# The off hand's finger curls (thumb, index, middle, ring, little) of the arm dump's last line that has them; null
# for none.
function Get-QaFingerCurls([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return $null }
    $lines = (Read-QaShared $Path) -split "`n"
    for ($i = $lines.Count - 1; $i -ge 0; $i--) {
        if (-not $lines[$i].Trim()) { continue }
        try { $d = $lines[$i] | ConvertFrom-Json } catch { continue }
        if ($null -ne $d.arms -and $null -ne $d.arms.L -and $null -ne $d.arms.L.fingers) { return @($d.arms.L.fingers.curl) }
    }
    return $null
}

function Get-QaFitDistance($A, $B) {
    $s = 0.0
    for ($i = 0; $i -lt 3; $i++) { $s += ([double]$A[$i] - [double]$B[$i]) * ([double]$A[$i] - [double]$B[$i]) }
    return [Math]::Sqrt($s)
}

function Get-QaFitAngle($A, $B) {
    $dot = 0.0; $la = 0.0; $lb = 0.0
    for ($i = 0; $i -lt 3; $i++) { $dot += [double]$A[$i] * [double]$B[$i]; $la += [double]$A[$i] * [double]$A[$i]; $lb += [double]$B[$i] * [double]$B[$i] }
    return [Math]::Acos([Math]::Max(-1.0, [Math]::Min(1.0, $dot / [Math]::Sqrt($la * $lb)))) * 180.0 / [Math]::PI
}

function New-QaFitScenario([string]$Name, [string[]]$ExtraEnv, [string]$Proves, [scriptblock]$Asserts) {
    return @{
        Name = $Name; Kind = 'game'; Proves = $Proves
        Env = @($script:QaFitEnv) + @($ExtraEnv)
        Input = $script:QaFitInput
        Prepare = { param($c) @("ETERNALVR_TEST_ARM_DUMP=$(Join-Path $c.Dir 'arm-dump.jsonl')") }
        Timeline = {
            param($c)
            Wait-QaSeconds $c 12
            Save-QaShot $c 'hands'
            Save-QaXrShot $c 'hands'
        }
        Asserts = $Asserts
    }
}

$fitScenarios = @(
    (New-QaFitScenario 'fit-touch' @() `
        'The reference for fit-index: Touch controllers on SteamVR get no fit, one line per hand, and the hand spaces are not made again' {
            param($c)
            Test-QaPresent $c "controllers: the right hand's oculus_touch controller (the weapon hand): no fit of its own; controller angle 0 deg (the launcher's)"
            Test-QaPresent $c "controllers: the left hand's oculus_touch controller (the off hand): no fit of its own; controller angle 0 deg (the launcher's)"
            Test-QaCount $c 'controller (the weapon hand): ' 1
            Test-QaCount $c 'controller (the off hand): ' 1
            Test-QaAbsent $c 'xrCreateActionSpace failed'
            Test-QaPresent $c 'ui: the aim dot at'
            $d = Get-QaFitDump (Join-Path $c.Dir 'arm-dump.jsonl')
            Add-QaResult $c 'the arm dump has both grips and the shot ray' ($null -ne $d) (Join-Path $c.Dir 'arm-dump.jsonl')
        }),
    (New-QaFitScenario 'fit-index' @('ETERNALVR_TEST_PROFILES=valve_index') `
        'Valve Index controllers on SteamVR (the simulator made to report them): each hand logs its fit once, the spaces are made again without a refusal, the aim dot and the wrist HUD still show, and against fit-touch the same scripted hands put both grips 8.5 cm away and the shot ray 20 degrees lower (the weapon hand''s -20)' {
            param($c)
            Test-QaPresent $c 'controllers: the runtime reports /interaction_profiles/valve/index_controller for the right hand'
            Test-QaPresent $c 'controllers: the runtime reports /interaction_profiles/valve/index_controller for the left hand'
            Test-QaPresent $c "controllers: the right hand's valve_index controller (the weapon hand): its fit moves it 8.5 cm (grip frame 0.0 4.2 -7.4 cm, aim frame 0.0 8.5 -0.7 cm) and turns it -20 deg; with the launcher's 0 deg, controller angle -20 deg"
            Test-QaPresent $c "controllers: the left hand's valve_index controller (the off hand): its fit moves it 8.5 cm (grip frame 0.0 4.2 -7.4 cm, aim frame 0.0 8.5 -0.7 cm) and turns it -40 deg; with the launcher's 0 deg, controller angle -40 deg"
            Test-QaCount $c 'controller (the weapon hand): ' 1
            Test-QaCount $c 'controller (the off hand): ' 1
            Test-QaAbsent $c 'xrCreateActionSpace failed'
            Test-QaAbsent $c 'spaces with its valve_index controller''s fit were refused'
            Test-QaPresent $c 'ui: the aim dot at'
            Test-QaAbsent $c 'HUD: no controllers \(or no' -Regex
            $d = Get-QaFitDump (Join-Path $c.Dir 'arm-dump.jsonl')
            $touch = Get-QaFitDump (Join-Path (Join-Path $c.Out 'fit-touch') 'arm-dump.jsonl')
            if ($null -eq $d -or $null -eq $touch) {
                Add-QaResult $c 'both arm dumps (this run and fit-touch, run first)' $false "this run: $($null -ne $d), fit-touch: $($null -ne $touch)"
                return
            }
            $upm = [double]$d.upm
            $gun = (Get-QaFitDistance $d.grip $touch.grip) / $upm
            $off = (Get-QaFitDistance $d.offgrip $touch.offgrip) / $upm
            $ray = Get-QaFitAngle $d.aim.dir $touch.aim.dir
            Add-QaResult $c 'the weapon hand''s grip 8.5 cm from fit-touch''s' ([Math]::Abs($gun - 0.0849) -lt 0.005) ('{0:N4} m' -f $gun)
            Add-QaResult $c 'the off hand''s grip 8.5 cm from fit-touch''s' ([Math]::Abs($off - 0.0849) -lt 0.005) ('{0:N4} m' -f $off)
            Add-QaResult $c 'the shot ray 20 degrees from fit-touch''s' ([Math]::Abs($ray - 20.0) -lt 0.5) ('{0:N2} deg' -f $ray)
            Add-QaResult $c 'the shot ray lower than fit-touch''s' ([double]$d.aim.dir[2] -lt [double]$touch.aim.dir[2]) ("z {0:N4} against {1:N4}" -f [double]$d.aim.dir[2], [double]$touch.aim.dir[2])
        }),
    # T-226: the Index's off-hand fingers close with the squeeze's value (grip_curl), apart from the grip action
    # (the force) that fires the Flame Belch. Open, then wrapped (the curl alone, 25 s: a belch would be counted in
    # a 10 s input line), then the grip's own press as the control that the input line does count a belch.
    @{
        Name = 'fingers-index'; Kind = 'game'
        Proves = 'Valve Index controllers (the simulator made to report them): the grip curl bound for the fingers on both hands; the off hand''s middle, ring and little fingers open at no curl and closed with the curl alone, the index finger left to the trigger, and no Flame Belch from it; the grip''s own press still belches'
        Env = @($script:QaFitEnv) + @('ETERNALVR_TEST_PROFILES=valve_index')
        Input = $script:QaFitInput + @('left.grip_curl = 0')
        Prepare = { param($c) @("ETERNALVR_TEST_ARM_DUMP=$(Join-Path $c.Dir 'arm-dump.jsonl')") }
        Timeline = {
            param($c)
            Wait-QaSeconds $c 12
            $c.OpenCurls = Get-QaFingerCurls (Join-Path $c.Dir 'arm-dump.jsonl')
            Write-QaInput $c ($script:QaFitInput + @('left.grip_curl = 1'))
            Wait-QaSeconds $c 25
            $c.WrappedCurls = Get-QaFingerCurls (Join-Path $c.Dir 'arm-dump.jsonl')
            $c.WrappedLog = Get-QaLogText $c
            Save-QaShot $c 'wrapped'
            Save-QaXrShot $c 'wrapped'
            # Held 2 s: the scripted input is read every 10 game frames, so a shorter press can fall between reads.
            Write-QaInput $c ($script:QaFitInput + @('left.grip_curl = 1', 'left.grip = 1'))
            Wait-QaSeconds $c 2
            Write-QaInput $c ($script:QaFitInput + @('left.grip_curl = 1'))
            Wait-QaSeconds $c 12.5
        }
        Asserts = {
            param($c)
            Test-QaPresent $c 'controllers: the runtime reports /interaction_profiles/valve/index_controller for the left hand'
            Test-QaPresent $c 'controllers: 2 grip curl binding(s) for the fingers added to the /interaction_profiles/valve/index_controller data'
            Test-QaAbsent $c 'the runtime refuses the bindings'
            $open = $c.OpenCurls; $wrapped = $c.WrappedCurls
            Add-QaResult $c 'the off hand''s fingers dumped open and wrapped' ($null -ne $open -and $null -ne $wrapped) (Join-Path $c.Dir 'arm-dump.jsonl')
            if ($null -ne $open -and $null -ne $wrapped) {
                $text = { param($k) ($k | ForEach-Object { '{0:N2}' -f [double]$_ }) -join ' ' }
                Add-QaResult $c 'at no curl the three fingers open' (@($open[2..4] | Where-Object { [double]$_ -gt 0.05 }).Count -eq 0) (& $text $open)
                Add-QaResult $c 'with the curl alone the three fingers closed' (@($wrapped[2..4] | Where-Object { [double]$_ -lt 0.95 }).Count -eq 0) (& $text $wrapped)
                Add-QaResult $c 'with the curl alone the index finger the trigger''s (open)' ([double]$wrapped[1] -lt 0.05) (& $text $wrapped)
            }
            $belch = 'controllers: input, last 10 s: .*presses mapped/sent: .*flame_belch \d'
            Add-QaResult $c 'no Flame Belch while the fingers were wrapped' (-not ($c.WrappedLog -match $belch)) ''
            Test-QaPresent $c $belch -Regex
        }
    }
)

# Before the launcher's end-to-end suite, which stays last.
$e2e = @($script:QaScenarios | Where-Object { $_.Kind -eq 'e2e' })
$script:QaScenarios = @($script:QaScenarios | Where-Object { $_.Kind -ne 'e2e' }) + $fitScenarios + $e2e
