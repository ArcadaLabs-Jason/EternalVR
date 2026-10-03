# Alpha release checklist

What I do before a zip goes to a tester, and where the M4.5 release track (`docs/ROADMAP.md`) stands for
it. Not shipped in the zip.

Status as of the `alpha-package` branch (2026-09-27), read from the code, the docs and the rig findings;
nothing here was re-run for this list. **Done** has evidence in the repo, **not done** is missing or
known to be incomplete, **unverified** may be fine but nothing records it.

## Before sharing a zip

1. [ ] Merge what should ship into `main`, then check out that commit with no local changes.
2. [ ] `powershell -NoProfile -ExecutionPolicy Bypass -File tools\release\make-release.ps1`
       builds the layer (preset `windows-msvc-release`) and the launcher, runs both test suites, and
       writes `EternalVR-alpha-<version>-<sha>.zip`, a `-symbols.zip` and a `.sha256` for each into
       `<workspace>\tmp-release\`. It refuses a dirty checkout.
       The launcher's updater (`launcher/src/EternalVR.Launcher.Core/Update/`) relies on this shape: a tag
       `vX.Y.Z` on the public repo, the zip uploaded as `EternalVR-*.zip` (the symbols zip is never uploaded)
       with its `.sha256` next to it, one top folder holding `SHA256SUMS.txt` for every other file, and the
       launcher's file version equal to the tag.
3. [ ] **Play the zip's own build once.** Rig runs so far used the Debug layer (`windows-msvc`); this is
       an optimised build. Unzip it outside the checkout, start the launcher from there, and play 15
       minutes: a save loaded, a fight, pause and resume, a menu with the pointer, quit. Check the
       session log shows the guard armed and no errors, and that the settings were restored afterwards.
4. [ ] Continue a save (not a new campaign) during that run; make sure the new-campaign fullscreen
       problem is still the only one of its kind (`KNOWN-ISSUES.md`).
5. [ ] Add anything found to "More known issues" in `docs/release/KNOWN-ISSUES.md`, commit, and
       repackage (step 2) so the zip carries it.
6. [ ] Keep the `-symbols.zip` with the zip's hash; it is what turns a faulting offset in a tester's log
       into a function name.
7. [ ] Upload the zip to VirusTotal. If an engine flags `EternalVR.dll` (Microsoft Defender above all),
       submit it as a false positive (https://www.microsoft.com/wdsi/filesubmission) before sharing, and
       note the result in the release notes.
8. [ ] Once per release, on a clean PC or VM (no developer tools, no Visual C++ Redistributable, the game
       started once through Steam): extract the zip, start the launcher, reach the headset. This is the
       check for missing runtime files and for the first-run messages.
9. [ ] Publish it as a GitHub Release on the private repository (tag `v<version>`; release notes with
       what changed, the known issues' headline and the supported game build), with the zip and its
       `.sha256` attached. Ask testers not to pass it on.

## M4.5 release track

| # | Item | Status | Notes |
|---|---|---|---|
| 1 | Multiplayer safety re-verified on the exact build that leaves the rig | **Not done** | The guard is built and verified on Debug builds (`docs/rig-findings/mp-guard.md`); the launcher refuses multiplayer arguments (`launcher/data/refused-args.txt`). Nothing re-verifies it on the packaged build, and the hashed game-folder listing around a launcher session is not recorded. Step 3 above covers the guard-armed part only. |
| 2 | Launcher v0: per-process environment, chosen runtime, Steam first, HKCU registration, runtime and headset up, restore on exit, named hand-off error | **Partly done** | Environment route, runtime choice, Steam checks, restore and the hand-off message are done and were run on the rig (`docs/rig-findings/launcher-live.md`). The HKCU registration exists behind `--register-hkcu` and is untested (T-113); SteamVR discovery is in the code but unverified on the rig. Before a stereo launch with the render size on, a probe asks the runtime for the headset and the window asks before launching without one (`SessionRunner.cs`); a mono launch has no such check. |
| 3 | Crash safety: a killed launcher's next start completes the restore; no stale registration | **Done** for the environment route | Launcher killed mid-session, next start restored the keys (`launcher-live.md`). The registration half is unverified, since the registration route is not used. |
| 4 | Settings and saves: snapshot and key restore, killed game, save-point defence, discovery on a clean machine, save backups and "Restore saves" | **Partly done** | Snapshot, key restore, killed-game restore and save backups are done on the rig. "Restore saves" is built and unit-tested; a live restore is unverified. The save-point defence (flat launch from Steam after a killed session) and the clean-machine discovery check are not recorded. |
| 5 | Adapter steering by LUID on a two-GPU machine | **Not done** | The layer logs and checks the device LUID but does not reorder devices. |
| 6 | Launcher preflight v0 | **Done** | Steam, elevation, data folder, Program Files warning, HAGS, anti-cheat files in the game folder, known and conflicting layers (Virtual Desktop's API layer handled) are all in `Preflight.cs`. Anti-cheat modules inside the game process are the layer's tripwire; unverified here. |
| 7 | Coexistence with RTSS, OBS and ReShade | **Not done** | The known-layer list is data (`launcher/data/known-layers.txt`) and ReShade is disabled, but RTSS and OBS are still marked "not yet tested". |
| 8 | Export report | **Partly done** | **Export report...** (and `--export-report`) writes a redacted zip whose manifest is documented in `TROUBLESHOOTING.md` (`ReportManifest.cs`), with the HAGS state, the file list and size shown before saving; collection, redaction and the manifest are unit-tested. No GitHub issue template asks for it yet. |
| 9 | User data and updates | **Partly done** | All user data is under `%LOCALAPPDATA%\EternalVR\`; a newer `schema_version` is refused and left unchanged (unit-tested). VR settings profiles (`profiles\<name>.ini`) and the player's own controller maps (`controls\`, one set per profile) exist; an older `schema_version` is rewritten on the first save, without a backup. The update check and in-place install exist; the N to N+1 upgrade is untested. The launcher refuses a layer whose release version differs from its own (`VersionCheck.cs`, unit-tested; the layer's version is its DLL's version resource), and only warns for a Debug (`-dev`) build or a DLL without a version. |
| 10 | CI: clang-format, rig-script suite on windows-latest, clang-tidy job, 600-line and module-boundary checks | **Not done** | clang-format and the 600-line check (`tools/check_file_size.py`) run. The rig-script suite, clang-tidy and the module-boundary check are not in `ci.yml`. The release script's source check (`-Validate`) now runs on windows-latest. |
| 11 | No stall or deadlock: 30-minute simulator soak and 30-minute headset session | **Unverified** | No soak record in the repo. |
| 12 | SteamVR run on the Quest 3, or the gap recorded | **Not done** | Recorded as untested in `KNOWN-ISSUES.md`, which covers the release-notes part of the fallback. |
| 13 | Tester channel: trusted collaborators with `main` protected; recruitment started | **Not done** | `main` is not protected (GitHub reports `protected: false`). Recruitment: unverified. This zip route is the interim channel. |
| 14 | Early AMD check (offline patcher tool) | **Not done** | No patcher tool yet. |
| 15 | No self-hosted GitHub runner | **Done** | GitHub reports 0 runners for the repository. |

## Notes for this alpha

- The optimised layer still reads the developer test variables (`ETERNALVR_TEST_INPUT`,
  `ETERNALVR_GUARD_TEST_TRIP_MS` and the like); there is no development-only build switch. The launcher
  never sets them, so a tester would only meet them by setting them by hand.
- The zip carries no game files and nothing derived from the game other than the supported exe's hash in
  `data\known-builds.txt`.
- The layer links the C++ runtime statically: `EternalVR.dll` imports only system DLLs (d3d12, dxgi,
  version, kernel32, user32), and the bundled `openxr_loader.dll` only advapi32 and kernel32. Step 8 is the
  check on a clean machine.
