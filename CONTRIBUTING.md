# Contributing to EternalVR

Thanks for helping out. This file covers how to build and test, the code standards, and how we write
commits. The design itself is in `docs/ARCHITECTURE.md`; read at least sections 2 and 15 before
sending a change.

## Building and testing

You need CMake 3.24 or newer, Ninja and a C++20 compiler (recent Clang, GCC or MSVC).

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

The portable modules and their tests build on Linux, macOS and Windows. The Vulkan layer DLL only
builds on Windows; from a Visual Studio developer prompt use the `windows-msvc` preset instead of
`dev`. From a plain PowerShell or other shell, run the whole sequence in one `cmd`, so the Visual
Studio environment carries to the build (running `vcvars64.bat` on its own sets it only in a child
process):

```
cmd /c "call <VS>\VC\Auxiliary\Build\vcvars64.bat && cmake --preset windows-msvc && cmake --build --preset windows-msvc && ctest --preset windows-msvc"
```

where `<VS>` is the Visual Studio or Build Tools installation folder. From Git Bash write `cmd //c`,
because Git Bash turns a lone `/c` into a path. In PowerShell the alternative is
`Import-Module <VS>\Common7\Tools\Microsoft.VisualStudio.DevShell.dll` followed by
`Enter-VsDevShell -VsInstallPath <VS> -DevCmdArguments '-arch=x64'`. A `release` preset is available
for optimised builds.

Dependencies are fetched by CMake at configure time and pinned to exact versions, so no manual setup
is needed.

The launcher is C# and is not part of the CMake build. Once it exists (the release track, M4.5) it
builds with `dotnet build launcher/EternalVR.sln`, and the portable `EternalVR.Launcher.Core` tests run
with `dotnet test` on any OS. The net48 projects need the .NET Framework 4.8 reference assemblies (the
Developer Pack, or the `Microsoft.NETFramework.ReferenceAssemblies` package). The rig scripts in
`tools/rig/` are Windows PowerShell 5.1 scripts, need no build, and run with
`powershell -NoProfile -ExecutionPolicy Bypass -File` (`docs/RIG_BRINGUP.md` section 6; tests in `tools/rig/tests/`). The environment
variables the layer reads are listed in `docs/ARCHITECTURE.md` section 15.

## Code standards

- C++20. Code is formatted with `clang-format` 18.1.8 (config in `.clang-format`; CI checks it
  with exactly that version on pushes to `main` and on pull requests, since other versions format
  some constructs differently). `clang-tidy` is configured (`.clang-tidy`); run it locally. It is
  advisory until its CI job lands in the release track (M4.5), where it must pass with zero
  warnings. Compiler warnings are errors on every compiler.
- One responsibility per file. Keep files small; around 300 lines is a good upper bound, and
  anything past about 600 should be split (a CI check enforces 600 from M4.5).
- Namespaces are `evr::` plus the module's own name, the last meaningful part of its path; the game
  name is dropped because only one game is supported. So `common` is `evr`, `xr_math` is
  `evr::xr_math`, `platform/settings` is `evr::settings`, `features/input` is `evr::input`,
  `game/eternal` is `evr::game` and `engine/eternal/resolver` is `evr::resolver`.
- Prefer clear names to comments. Comments explain *why*: the constraint, the spec rule, the bug
  that motivated a choice. Don't restate what the code says.
- Engine internals (offsets, hooks, type names) only in `src/engine/eternal`; game-design data
  (actions, default bindings, player dimensions) only in `src/game/eternal`. Features stay generic
  and take game data as plain values.
- Portable modules (`common`, `xr_math`, `platform/settings`, `features/*`, `game/eternal`, the
  resolver) must not include Windows, Vulkan or OpenXR headers. They are plain logic and fully
  unit-tested.
- Every hook and resolver keeps its decision logic in a unit-testable part, separate from the code
  that touches the game.
- Tests live under `tests/`, mirroring `src/`, and use doctest. New logic comes with tests.
- Third-party code: MIT or similarly licensed code (for example from REFramework) may be
  adapted, with an origin header in the file and an entry in `THIRD_PARTY_NOTICES.md`. GPL code,
  including UEVR and OptiScaler, is reference only and must not be copied. OptiScaler is never
  bundled either: the launcher downloads its official release at run time.

Before sending a change, run:

```sh
pip install clang-format==18.1.8   # once, ideally in a virtual environment
clang-format -i $(git ls-files '*.cpp' '*.hpp')
clang-format --dry-run --Werror $(git ls-files '*.cpp' '*.hpp')   # the check CI runs
python3 tools/check_includes.py
cmake --build --preset dev && ctest --preset dev
```

## Game files and rig artifacts

Never commit, and never attach to CI artifacts or releases:
- game binaries (any DOOM Eternal exe, DLL or data file), NVIDIA DLLs or OptiScaler files;
- anything derived from the game: type-info, string or RTTI dumps, disassembly, GPU captures
  (GFXReconstruct, RenderDoc, Nsight), Fossilize databases, extracted assets or shaders;
- logs, screenshots or crash dumps that show personal data (account names, user-name paths, Steam
  IDs).

These stay on the rig in the artifact root on the development drive (`docs/RIG_BRINGUP.md` section 8). If you copy rig output to
your own machine for analysis, keep it under `rig-local/` in the checkout, which is ignored.
`.gitignore` catches the common file types, but text dumps (type-info, string or RTTI listings) cannot
be caught by a pattern; this rule is what covers them. Write-ups in `docs/rig-findings/` record only
the facts we need (names, offsets, counts). Published third-party
material (for example the cvar list and Meathook's name lists) is different, but it is not committed
either: `tools/fetch_references.sh` fetches it into `reference/`, which git ignores apart from its index,
`reference/MANIFEST.md` (source and licence of each item). A fact the code or the docs rely on is
summarised in our own words under `docs/notes/`.

## Commits

- One logical change per commit. Refactors and behaviour changes go in separate commits.
- Subject line in the imperative, at most about 72 characters, prefixed with the module when it
  helps: `resolver: reject sections that extend past end of file`.
- The body says why the change is needed and anything a reviewer should know, such as rig
  measurements or spec references. Wrap at 72 columns.
- The tree builds and all tests pass at every commit.
