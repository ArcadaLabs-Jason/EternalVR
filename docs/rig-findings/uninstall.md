# Uninstall: what the package leaves behind (T-093, T-105, REQ-16, ROADMAP M10 "Uninstall")

What a player's install, use and uninstall of the release zip write on a PC, and how the clean-machine check is run.
Evidence labels: [code] read from the launcher and layer sources at `eee2bb2`, [rig] seen on the development PC,
[sandbox] seen in Windows Sandbox (a fresh Windows with no Steam and no game), [inferred] a reading of the others.

## 1. What the package writes

- **The unzipped folder.** The launcher treats it as read-only, except for an in-place update the player agreed to
  (T-123), which replaces the package's own files in it (`launcher/src/EternalVR.Launcher.Core/Update/UpdatePackage.cs`)
  and stages the download under the data folder. [code]
- **`%LOCALAPPDATA%\EternalVR\`**, the data folder (`DataPaths.cs`): `launcher.ini`, `logs\` (`launcher.log` and one
  folder of layer logs per session), `snapshots\`, `save-backups\`, `controls\`, `profiles\`, `dlss\` (DLSS DLLs
  downloaded from NVIDIA on request), `updates\`, `headset.txt`, `render-cap.txt`, `profile-repair.txt`, and the
  markers `SESSION_PENDING` (a restore not completed) and `REGISTRATION_PENDING` (see below). [code]
- **The registry: nothing on the default route.** The layer reaches the game through `VK_ADD_IMPLICIT_LAYER_PATH` in
  the game process's environment only (T-113). The HKCU route (`--register-hkcu`, a command-line option the window
  does not offer) writes one DWORD under `HKCU\SOFTWARE\Khronos\Vulkan\ImplicitLayers`, named by the manifest path,
  after writing `REGISTRATION_PENDING` with the same path; both are removed after the session and at every launcher
  start, before anything else (`HkcuLayerRegistration.cs`, `SessionRunner.Recover`). Up to `eee2bb2` that clean-up
  removed the value only: on a PC without them, the keys `HKCU\SOFTWARE\Khronos`, `...\Vulkan` and
  `...\Vulkan\ImplicitLayers` that registering had created stayed behind, empty (harmless to the Vulkan loader, but not
  the pre-install state; seen on the development PC, section 4). Since 2026-10-13 the marker also lists the keys the
  registration created (`created key: <key>` lines, `Safety/RegistrationMarker.cs`; an older launcher skips them as
  names of values that do not exist) and the clean-up deletes each of them once it is empty, deepest first; a key
  that existed before, or holds anything else, stays. The launcher only reads the Vulkan and OpenXR layer keys
  (preflight's inventory) and never writes HKLM. [code]
- **OpenXR:** no API layer and no runtime registered. A runtime other than the system's is passed as
  `XR_RUNTIME_JSON`, Virtual Desktop's compatibility layer is switched off with its `DISABLE_...` variable, both in the
  game's environment only. [code]
- **Environment, shortcuts, services, scheduled tasks:** none. No code creates a shortcut, service or task; the only
  `SetEnvironmentVariable` calls are the OpenXR probe's, on its own process around one call and set back (the launcher
  e2e suite checks there are no others). [code]
- **The game folder:** never written (T-109; M4.5's hashed listing). The game's own settings files are snapshotted
  before a VR session and restored after it; that is the session's job, not the uninstall's. [code]
- **Not ours:** Windows records that a program ran (Prefetch, MUICache, the background activity monitor); those are
  the operating system's, written for any program, and are not counted. [inferred]

## 2. Uninstall as documented

`docs/release/INSTALL.md` "Uninstalling": open the launcher once and close it (it completes a pending restore and
removes a stale registration), delete the unzipped folder, delete `%LOCALAPPDATA%\EternalVR`. The launcher has no
uninstall action of its own: T-093 asks for one, T-113 lists it under "Not in v0", and the M10 criterion names it.
[code]

## 3. The clean-machine check

`tools/rig/uninstall/start-sandbox.ps1 -Package <folder with the release zip> -Evidence <new folder>` writes a Windows
Sandbox configuration (the package folder and the scripts mapped read-only, the evidence folder writable,
networking off) and opens it; `uninstall-check.ps1` runs at logon. It snapshots (`uninstall-snapshot.ps1`) the
Vulkan and OpenXR layer keys of HKCU and HKLM (both views), the user and machine environment, Run, RunOnce, App
Paths and Uninstall entries, shortcuts, scheduled tasks, services and the EternalVR data folders, plus whole exports
of `HKCU\Software` and `HKLM\SOFTWARE` and a listing of the profile's AppData, ProgramData and TEMP; then:

1. the zip against its `.sha256` and every file against `SHA256SUMS.txt`, extracted to Downloads;
2. the launcher window opened (a first run) and closed;
3. `--dry-run`;
4. the launcher's own HKCU registration (`HkcuLayerRegistration.Register`, called through reflection in a process
   of its own, as a `--register-hkcu` session calls it) left as a session killed mid-way leaves it, then
   `--dry-run`: the start-up clean-up must remove the value, the marker and any key the registration created;
5. INSTALL.md's three uninstall steps;

with a diff of every step against the snapshot taken before the install. The pass condition is the diff after step
5: nothing added or removed but the operating system's own records. `-HostTest` on `uninstall-check.ps1` runs the
mechanics on a PC that is not clean (no window, a scratch data folder, no hive exports). It does write that PC's HKCU:
the registration of step 4 adds a value (naming a manifest that does not exist) and creates any missing Khronos key;
a `finally` block then removes that exact value and the keys that were missing before, if empty, whatever the
launcher did. Without `-HostTest` the script refuses to run unless the user is the sandbox's (`WDAGUtilityAccount`),
since it exports whole hives and deletes `%LOCALAPPDATA%\EternalVR` and its folder in Downloads.

What needs the game and Steam and so stays with the rig: VR sessions (snapshot and restore, the session finisher;
the launcher e2e suite with its stand-in game and the package QA's launches), the game folder listing, and the
criterion's "flat settings equal the pre-VR snapshot".

## 4. Results

- **Clean machine, 2026-10-16** (Windows Sandbox, the published v0.1.50 zip `EternalVR-alpha-0.1.50-2ac77a1.zip`;
  evidence `<workspace>\sandbox-0150b\evidence\run.log`, the diffs and the two window shots; [sandbox]):
  the zip matched its published SHA-256 and all 31 files their `SHA256SUMS.txt` lines; the launcher window opened
  on the first run (`02-first-run.png`); `--dry-run` changed nothing in the targeted snapshot; the registration of
  step 4 created `SOFTWARE\Khronos`, `SOFTWARE\Khronos\Vulkan` and `ImplicitLayers` (none existed), and the next
  `--dry-run` removed the value, the marker and all three keys (targeted snapshot 0 added, 0 removed); after the
  uninstall steps the EternalVR file listing and the targeted registry snapshot equal the pre-install ones. What the
  diffs still show is Windows' own: `UserAssist` and `MuiCache` entries naming the launcher (Windows records every
  program it runs), `HKLM\SOFTWARE\Microsoft\Tracing\EternalVR_RASAPI32` and `EternalVR_RASMANCS` (written by
  Windows' networking stack when the update check loads it; HKLM because the sandbox user is an administrator), and
  the performance counter names Windows rewrites on a program's first run. An earlier run the same evening never
  opened the launcher window (Windows PowerShell 5.1 refuses an empty `-ArgumentList`; fixed in the kit, which now
  fails the run when the launcher does not start). Left in the kit: in the sandbox's logon session the launcher's
  window title reads empty, so the kit stops the launcher instead of closing its window; harmless here, since
  step 3 deletes the launcher's data folder.
- **Mechanics on the development PC, 2026-10-13** (`uninstall-check.ps1 -HostTest`, the `eee2bb2` test package's
  zip and launcher; evidence `<workspace>\UNINSTALL-20261013\host-test2\run.log`; [rig]): the zip
  matched its `.sha256` and all 31 files their `SHA256SUMS.txt` lines; `--dry-run` with a scratch data folder changed
  nothing in the targeted snapshot; `HKCU\SOFTWARE\Khronos\Vulkan\ImplicitLayers` was missing (its two parents
  existed, empty), the launcher's own `Register` (called through reflection, as a `--register-hkcu` session calls it)
  created it and wrote the marker and the value, and the next `--dry-run` logged `removed the HKCU layer registration`
  and left neither the value nor the marker, but left the key it had created, empty: the leftover of section 1. The
  script's own clean-up then removed that key, and after the uninstall steps the targeted snapshot equalled the one
  before the install. The same script without `-HostTest` on that PC refused with exit code 2 and wrote nothing.
- **The fix, against the real registry** [code]: `HkcuLayerRegistrationTests` compiles the launcher's
  `HkcuLayerRegistration.cs` into the launcher test project and runs it under a scratch
  `HKCU\SOFTWARE\EternalVR-test-<guid>` (removed after each test, passed or failed): the keys a registration created
  are deleted deepest first, a created key holding another value is kept (and so are its parents), a key that existed
  before is kept even when empty, and a marker naming a key that is not the layer key or a parent of it leaves that
  key alone. Each check fails when its part of the clean-up is taken out (no key deletion; every `created key:` line
  accepted; no emptiness check). The sandbox run of 2026-10-16 shows it on a clean machine (above).
