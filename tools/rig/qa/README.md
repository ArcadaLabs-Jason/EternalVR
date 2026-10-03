# Rig QA suite

Simulator runs of a layer build with scripted input, screenshots and layer-log assertions, plus the
launcher's end-to-end suite. One report per run.

```
powershell -NoProfile -ExecutionPolicy Bypass -File tools\rig\qa\qa-suite.ps1 `
    [-LayerSrc <folder with EternalVR.dll, VK_LAYER_ETERNALVR.json, openxr_loader.dll>] `
    [-Out <dir>] [-Only baseline,token-pickup] [-LauncherBin <launcher>\src\EternalVR.Launcher\bin\Release] `
    [-DlssDll <newer nvngx_dlss.dll>]
```

`-DlssDll` runs every game scenario on the launcher's newer-DLSS route (`ETERNALVR_DLSS_DLL`, preset K), which
installs two more inline hooks; run the suite once without and once with it before a release or a headset
package. Every game scenario also fails on a `no free hook slot` or `no free inline hook slot` line.

Defaults: `-LayerSrc` is `build\windows-msvc\src\vkcore` of this tree, `-Out` is
`E:\Code\resources\Testing\QA-<yyyyMMdd-HHmm>`. Exit code 0 all passed, 1 a scenario failed, 2 refused or
stopped.

Needs: Steam running, the virtual display driver, OpenXR-Simulator at
`<workspace>\tools\bin\openxr-simulator\openxr_simulator_rig.json`, the 100% save at
`<workspace>\tmp-release\save100\resigned-jason`. procdump (`tools\bin\procdump`) is optional.

## Safety

- `<workspace>\tmp-vr\STOP-RIG` ends the suite before the next scenario and during any wait
  (the running game is stopped and the save restored first). It is not seen while `launch-ht.ps1` runs
  (up to about two minutes).
- A running `DOOMEternalx64vk.exe` refuses a game scenario.
- After every game run, also on failure: `stop.ps1 -Run <run> -RestoreCloudFiles`, then the 100% save is
  copied into `GAME-AUTOSAVE0`, touched and checked (`game_duration.dat` 958137 bytes). A failed restore
  ends the suite.

## What each run does

A game scenario stages the build into `tmp-vr\rs\qa-<name>` (layer log in `qa-<name>-logs`), writes the
scripted input file `qa-<name>-input.txt` (`ETERNALVR_TEST_INPUT`, docs/VR_CONTROLLERS.md), starts Route S
stereo through `launch-ht.ps1` (simulator, 1280x720 window and mirror on the virtual display), waits up to
200 s for `the schedule's clock starts` or `aim: head aim on` in the layer log, runs its timeline, stops,
restores the save, then checks the layer log (the stage's logs, else the run folder's).

## Scenarios

| Name | Proves |
|---|---|
| baseline | e1m2 stereo for 40 s: `stereo: eye views` and `rates: game` logged, no `ERROR`, `exception` or `crash` (case-sensitive; `view-to-hand error` is normal text) |
| foveation | `ETERNALVR_FOVEATION=balanced`: the eyes get a VRS pattern, the 8192x8192 target is logged as not in the eye's space and never gets an eye pattern |
| foveation-eyes | `ETERNALVR_VRS_TEST=eyetest` (eye L's left half and eye R's right half at 4x4) with eye captures every 400 pairs for 40 s, the suite's TAA: in the captures (`qa-blockiness.ps1`) eye L's left half and eye R's right half have a block-edge ratio of 1.20 or more, the other halves 1.10 or less (a script error is reported as `measuring failed: ...`, apart from no captures); the last `vrs: render pass frames:` line has no parity break and no recording contradicted later, and the passes kept at full rate for want of a frame are 5% or less between the last two such lines (in the map; 0% on the rig, those all before it); with one line only, the share is a note |
| foveation-eyes-dlss | The same with DLSS Quality per eye (`ETERNALVR_STEREO_DLSS=1`, `_QUALITY=quality`, `+r_antialiasing 2`; the newer DLL with `-DlssDll`): eye R's DLSS feature made, the render size logged, the block measured at the render size (6 captured pixels at Quality) |
| seated-jump | `ETERNALVR_HANDS_JUMP=1`, `ETERNALVR_POSTURE=seated`: a throw to 0.10 m over the head does not jump, one to 0.25 m does: exactly one `gesture: hands-up jump (seated height)` |
| arm-pose | The right hand at (0.35, -0.15, -0.45), aim yaw 45: screenshot `arm-pose-arm-pose.png` to compare with the references by eye (no image check yet) |
| token-pickup | e1m3 `cp_03_shoot_gate`, debug schedule into the token's use trigger, Use under hand aim at pitch -15, -30, 0 until a sync starts: `controllers: sync 'interact/preator_suit_token...' (not a kill)`, no `glory: kill`; screenshot 4 s after |
| launcher-e2e | `launcher\tests\e2e.ps1 -Root <Out>\e2e -Runtime <simulator json>` prints `E2E OK` (skipped without a Release build) |

Every game scenario also checks: in the map, stopped and cleaned up, save restored, no crash dump.

## Output

`<Out>\report.md` (one row per scenario, every check, screenshots, run folder, duration, and
`QA: N of M scenarios passed`), `<Out>\qa.log`, `<Out>\<scenario>-<checkpoint>.png` (virtual display) and
`-sim.png` (simulator window), `<Out>\<scenario>\` with `eternalvr.log`, `launch.txt`, `stop.txt`.

## The block measure

`qa-blockiness.ps1 -Dir <captures> [-Period 4] [-Skip 3]` reads `ETERNALVR_CAPTURE_EYES` pairs
(`*-L.png`, `*-R.png`) and prints, per eye and image half, the block-edge step ratio: the mean step between
neighbouring pixels at the boundaries of the strongest position modulo `-Period` over the mean at the other
positions, horizontal and vertical averaged, over the captures after the first `-Skip`. About 1 without
blocks (1.02 to 1.05 on the rig), above it with 4x4 shading. `-Period` is the 4x4 block in captured pixels:
4 at the eye image's own size, 6 with DLSS Quality (render size two thirds of it). Rig runs of the eye test on
2026-10-02 (merge-int build): TAA eye L 1.38 / 1.05, eye R 1.03 / 1.37 (left / right half, period 4); DLSS
Quality eye L 1.91 / 1.04, eye R 1.05 / 1.53 (period 6). On a fixed 4-pixel grid from each half's first
column (the earlier script) the same DLSS captures gave eye L 1.11 / 0.98 and eye R 0.96 / 0.96, although eye
R's right half is visibly blocky: that grid meets the 6-pixel blocks' edges at one boundary in three or none,
by where the half starts against the blocks.

## Adding a scenario

Add a hashtable to `$script:QaScenarios` in `qa-scenarios.ps1`: `Name`, `Kind = 'game'`, `Proves`,
`Env`, optional `Map`, `Args` (game command-line arguments), `EyeCaptures` (every how many pairs eye captures go to
`<stage>-eyes`) and `Input` (initial input lines), `Timeline = { param($c) ... }` and
`Asserts = { param($c) ... }`. Helpers (`qa-common.ps1`): `Wait-QaSeconds $c <s>`, `Wait-QaLog $c <regex>
<timeout>`, `Write-QaInput $c @(lines)`, `Save-QaShot $c <checkpoint>`, `Get-QaLogText $c`,
`Add-QaNote`, `Test-QaPresent`/`Test-QaAbsent $c <text> [-Regex [-CaseSensitive]]`, `Test-QaCount $c <text> <n>`.
Check the syntax without running anything:
`[System.Management.Automation.Language.Parser]::ParseFile(<file>, [ref]$null, [ref]$errors)`.
