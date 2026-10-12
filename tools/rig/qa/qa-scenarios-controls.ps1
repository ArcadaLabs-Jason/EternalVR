# Control map scenarios beyond the thumb rests: a player's file with a later version's key (T-127,
# docs/VR_CONTROLLERS.md, "Forward-compatible control maps"). Dot-sourced by qa-suite.ps1 after
# qa-scenarios-wheel.ps1, whose helpers it uses (New-QaRestControls, Invoke-QaRestTap, Test-QaActionLines,
# Get-QaHeldItems): added to $script:QaScenarios before the launcher's end-to-end suite.

# A key no version has (a made-up input), beside the real binding of restbutton-tap.
$script:QaLaterKey = '"left.futurebutton.tap" = "weapon_slot_3"'

$controlMapScenarios = @(
    @{
        Name = 'fwdmaps-skip'; Kind = 'game'
        Proves = 'A player''s file with a key this version does not know is used without that line: the Touch file binds left.thumbrest.tap to weapon_slot_2 and has a made-up left.futurebutton.tap; the log names the skipped line, the file replaces the built-in data, and a 0.4 s touch of the left rest presses weapon_slot_2 once and the heavy cannon comes up'
        Env = @($script:QaRestHold, 'ETERNALVR_CONTROLLERS_TRACE=1')
        Prepare = { param($c) New-QaRestControls $c ($script:QaLaterKey + "`n" + $script:QaRestTap) }
        Input = @()
        Timeline = {
            param($c)
            Wait-QaSeconds $c 12
            Invoke-QaRestTap $c
            Save-QaShot $c 'after'
            [void](Wait-QaLog $c 'left rest button, last 10 s:' 20)
        }
        Asserts = {
            param($c)
            Test-QaPresent $c "controllers: $($c.ControlsFile): [map.right] 'left.futurebutton.tap' (= 'weapon_slot_3') is not a binding this version knows (a later version's, or a typo); this entry is skipped"
            Test-QaCount $c 'is not a binding this version knows (a later version''s, or a typo); this entry is skipped' 1
            Test-QaPresent $c "controller data '$($c.ControlsFile)' replaces the built-in data of its profile"
            Test-QaAbsent $c 'has issues; the built-in data is used'
            Test-QaAbsent $c 'the control map has conflicts'
            Test-QaPresent $c 'controllers: the left thumb rest is bound (tap = weapon_slot_2)'
            Test-QaActionLines $c @('weapon_slot_2')
            $held = Get-QaHeldItems $c 'controllers: action weapon_slot_2'
            $first = @($held.After)[0]
            Add-QaResult $c 'the heavy cannon came up after the tap' ($first -match 'heavy_cannon' -and $held.Before -ne $first) "before '$($held.Before)', after: $($held.After -join ', ')"
        }
    }
)

# Before the launcher's end-to-end suite, which stays last.
$e2e = @($script:QaScenarios | Where-Object { $_.Kind -eq 'e2e' })
$script:QaScenarios = @($script:QaScenarios | Where-Object { $_.Kind -ne 'e2e' }) + $controlMapScenarios + $e2e
