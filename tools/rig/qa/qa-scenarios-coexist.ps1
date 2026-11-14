# Overlay and capture tools next to the layer (T-095, docs/release/RELEASE-CHECKLIST.md item 7). Dot-sourced by
# qa-suite.ps1 after qa-scenarios-depth.ps1: added to $script:QaScenarios before the launcher's end-to-end suite.
#
#   coexist   -Only only (a measurement): baseline's run (e1m2's start in stereo) for 45 s with whatever overlay or
#             capture tool the rig has running (RTSS, OBS, ReShade ...; started by hand before the suite, its
#             launcher-side switch passed with -GameEnv, e.g. DISABLE_RTSS_LAYER=1; game arguments, ';'-separated,
#             in EVR_QA_COEXIST_ARGS, e.g. +r_allowBlackListedLayers 1). Checks baseline's lines (eye
#             views, rates, no error). Notes: the modules loaded in the game from outside the Windows folder (a
#             Vulkan layer or a hook DLL shows there: RTSSVkLayer64.dll, obs-vulkan64.dll, ReShade64.dll ...), the
#             game's presents, stereo pairs and XR frames a second averaged over the 10 s rates windows from 10 s
#             after the map load (the lowest pairs a second too), and the in-map timeline's shots: the virtual
#             display (the flat window, where an overlay of the game's own present shows) and the simulator's
#             composed frame (`-xr-both.png`, what a headset gets) at 15 s and 40 s. Compare two suites' notes, one
#             with the tool and one without.

$script:QaCoexistRates = 'rates: game ([\d.]+) present\(s\)/s, ([\d.]+) tick\(s\)/s, ([\d.]+) stereo pair\(s\)/s shown; XR ([\d.]+) frame\(s\)/s'

# The game's modules from outside the Windows folder, by file name.
function Get-QaCoexistModules {
    $p = Get-Process -Name ($script:QaGameProcess -replace '\.exe$', '') -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $p) { return 'no game process' }
    try {
        $win = $env:SystemRoot.TrimEnd('\') + '\'
        $names = @($p.Modules | Where-Object { $_.FileName -and -not $_.FileName.StartsWith($win, [StringComparison]::OrdinalIgnoreCase) } |
            ForEach-Object { [IO.Path]::GetFileName($_.FileName) } | Sort-Object -Unique)
        return ($names -join ', ')
    } catch { return "modules not read: $($_.Exception.Message)" }
}

function Add-QaCoexistRates($Ctx) {
    $text = Get-QaLogText $Ctx
    $lines = @($text -split "`r?`n")
    $load = -1
    for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i] -match "mp guard: map load '[^']*e1m2") { $load = $i } }
    if ($load -lt 0) { Add-QaNote $Ctx 'rates: no e1m2 map load line'; return }
    $inv = [Globalization.CultureInfo]::InvariantCulture
    $loadTime = if ($lines[$load] -match '^\[\s*([\d.]+)\]') { [double]::Parse($Matches[1], $inv) } else { $null }
    $rows = @()
    for ($i = $load + 1; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -notmatch $script:QaCoexistRates) { continue }
        $m = $Matches
        if ($null -ne $loadTime -and $lines[$i] -match '^\[\s*([\d.]+)\]' -and ([double]::Parse($Matches[1], $inv) - $loadTime) -lt 10) { continue }
        $rows += [pscustomobject]@{ Presents = [double]::Parse($m[1], $inv); Pairs = [double]::Parse($m[3], $inv); Xr = [double]::Parse($m[4], $inv) }
    }
    if ($rows.Count -eq 0) { Add-QaNote $Ctx 'rates: no 10 s window from 10 s after the map load'; return }
    $avg = { param($k) ($rows | Measure-Object -Property $k -Average).Average }
    $min = ($rows | Measure-Object -Property Pairs -Minimum).Minimum
    Add-QaNote $Ctx ("rates over {0} window(s) from 10 s after the map load: game {1:N1} presents/s, {2:N1} stereo pairs/s (lowest {3:N1}), XR {4:N1} frames/s" -f
        $rows.Count, (& $avg 'Presents'), (& $avg 'Pairs'), $min, (& $avg 'Xr'))
}

$coexistScenarios = @(
    @{
        Name = 'coexist'; Kind = 'game'; OnlyByName = $true
        Proves = 'A measurement with an overlay or capture tool running: the stereo run reaches the map and logs no error for 45 s; notes: the game''s outside modules, its rates, shots of the flat window and the headset frame'
        Env = @()
        Args = @(if ($env:EVR_QA_COEXIST_ARGS) { $env:EVR_QA_COEXIST_ARGS -split ';' | Where-Object { $_ } })
        Timeline = {
            param($c)
            Wait-QaSeconds $c 15
            Add-QaNote $c "game modules from outside the Windows folder: $(Get-QaCoexistModules)"
            Save-QaShot $c 'mid'; Save-QaXrShot $c 'mid'
            Wait-QaSeconds $c 25
            Save-QaShot $c 'end'; Save-QaXrShot $c 'end'
            Wait-QaSeconds $c 5
        }
        Asserts = {
            param($c)
            Test-QaPresent $c 'stereo: eye views'
            Test-QaPresent $c 'rates: game'
            Test-QaAbsent $c 'ERROR|exception|crash' -Regex -CaseSensitive
            Add-QaCoexistRates $c
        }
    }
)

# Before the launcher's end-to-end suite, which stays last.
$e2e = @($script:QaScenarios | Where-Object { $_.Kind -eq 'e2e' })
$script:QaScenarios = @($script:QaScenarios | Where-Object { $_.Kind -ne 'e2e' }) + $coexistScenarios + $e2e
