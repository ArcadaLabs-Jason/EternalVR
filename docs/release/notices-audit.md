# Notices audit (REQ-18, REQ-19, M10)

Date: 2026-11-09. Scope: the 0.1.89 release zip (`EternalVR-alpha-0.1.89-97b7b00`, repository commit 29d7fa47
for the sources) against `THIRD_PARTY_NOTICES.md`, `LICENSE`, the README, `cmake/*.cmake`, the launcher projects and
`tools/release/make-release.ps1`.

## What the zip holds

| File | Whose code |
|---|---|
| `EternalVR.Launcher.exe`, `EternalVR.Launcher.Core.dll`, `EternalVR.Launcher.exe.config` | Ours. Managed, .NET Framework 4.8 (part of Windows). No NuGet package, no merged or embedded assembly; imports only system DLLs. Embedded images: our icon and header, plus three brand marks (listed in the notices). |
| `layer/EternalVR.dll` | Ours, with statically linked safetyhook 0.7.0 (Boost), Zydis 4.0.0 and Zycore 1.4.1 (MIT), Vulkan and OpenXR headers (compiled in), and the static Microsoft C/C++ runtime. Imports only system DLLs, `openxr_loader.dll` and, by name at run time, `nvngx_dlss.dll`. |
| `layer/openxr_loader.dll` | Khronos, unmodified (SHA-256 identical to the x64 DLL in `OpenXR.Loader.1.1.63.nupkg`). Contains JsonCpp 1.9.6. |
| `layer/VK_LAYER_ETERNALVR.json`, `data/*.txt`, `data/controllers/*.toml` | Ours. |
| `README-ALPHA.md`, `docs/*.md`, `BUILD-INFO.txt`, `SHA256SUMS.txt` | Ours. |
| `LICENSE`, `THIRD_PARTY_NOTICES.md` | The MIT licence and the notices, both packaged by `make-release.ps1` (`Get-RelDocs`) and present in the 0.1.89 zip. |

## Inventory

| Component | Version | Licence | Shipped how | Notice present |
|---|---|---|---|---|
| safetyhook | 0.7.0 | Boost 1.0 | statically linked (amalgamated source) | yes, full text |
| Zydis | 4.0.0 | MIT | inside the safetyhook archive, linked | yes, full text |
| Zycore | 1.4.1 | MIT | inside the safetyhook archive, linked | yes, full text |
| Vulkan-Headers | 1.4.357 | Apache-2.0 OR MIT (used as MIT) | compiled in (headers) | yes, MIT text and copyright lines |
| OpenXR headers | 1.1.63 | Apache-2.0 OR MIT (used as MIT) | compiled in (headers) | yes |
| OpenXR loader | 1.1.63 | Apache-2.0 OR MIT (distributed as MIT) | file `layer\openxr_loader.dll` | yes, MIT text; Apache-2.0 text also reproduced |
| JsonCpp | 1.9.6 | public domain / MIT | inside `openxr_loader.dll` | yes, full licence file (version added by this audit) |
| Microsoft C/C++ runtime and STL | VS 2022 Build Tools | Visual Studio terms; STL Apache-2.0 WITH LLVM-exception, Ryu parts Boost | static in `EternalVR.dll` | yes (statement; the exception waives carrying the notice) |
| DE Advanced Options | commit f636868 | BSD 2-Clause | one byte signature in `EternalVR.dll` | yes, full text |
| Discord, GitHub, Ko-fi marks | n/a | brand terms | embedded in the launcher | yes |
| doctest | 2.4.12 | MIT | not shipped (layer tests) | listed as build and test only |
| xUnit 2.9.2, xunit.runner.visualstudio 2.8.2, Microsoft.NET.Test.Sdk 17.11.1 | as named | Apache-2.0 / MIT | not shipped (launcher tests) | listed as build and test only |
| Microsoft.NETFramework.ReferenceAssemblies | 1.0.3 | MIT | not shipped (compile-time references) | listed as build and test only |
| NVIDIA DLSS DLL (`nvngx_dlss.dll`) | 310.9.1 / 310.9.16 | NVIDIA's licence | not in the zip | listed under "Not redistributed" |
| .NET Framework 4.8; Windows and driver DLLs | n/a | Microsoft / driver vendor | not in the zip | listed under "Not redistributed" (system DLLs added by this audit) |

No `third_party/` or `extern/` directory exists; no other FetchContent or find_package dependency; no
NuGet package reaches the launcher's output. Checked against the fetched sources of an earlier build
(`safetyhook-src`, `doctest-src`, the `OpenXR.Loader.1.1.63` package, `Vulkan-Headers-1.4.357`) and the OpenXR-SDK
release-1.1.63 tag on GitHub.

## DLSS policy

The zip ships no NVIDIA code. The player can ask the launcher to download `nvngx_dlss.dll` from NVIDIA's own
GitHub repository (`data/dlss-downloads.txt`: file URL, size, SHA-256 and the licence URL per release); the dialog links
NVIDIA's licence and downloads only after "Accept and download". EternalVR does not host or redistribute the file, so
NVIDIA's redistribution terms do not apply to the zip. The layer loads the file the player chose.

## Gaps found

1. JsonCpp's version was not stated (the licence text was present). Added: 1.9.6.
2. The notices did not say that Windows and driver DLLs the layer loads are not in the zip, nor that the layer only
   loads, and the zip does not contain, a DLSS DLL. Added to "Not redistributed".
3. README's file list described `THIRD_PARTY_NOTICES.md` as notices for adapted code only; it now says libraries
   and files in the release.

Not found: a shipped component without a notice, a notice for a component no longer shipped, a wrong version
(safetyhook, Zydis, Zycore, OpenXR, Vulkan-Headers and doctest match `cmake/`), a missing licence text, a required
NOTICE file (none of the shipped libraries has one; the STL's is waived by its exception), or a notice file the
zip omits. `make-release.ps1` already refuses an unlisted third-party binary and a pinned version without a notices
section, so it was not changed.

## Changed

- `THIRD_PARTY_NOTICES.md`: JsonCpp 1.9.6 and the Android-only directories note; system DLLs and DLSS wording under
  "Not redistributed".
- `README.md`: the notices line.
- `docs/ROADMAP.md`: the notices criterion in the M10 checklist records this audit.

## For the owner

- Not confirmed here: that JsonCpp 1.9.6 is what the compiled DLL contains (taken from the release tag's source, the
  DLL carries no version string that was checked), and legal reading of the Visual Studio licence for the statically
  linked runtime (recorded as in the earlier audit, 2026-10-21).
- The M10 bullet "the notices audit and a clang-tidy check are done, and the code-signing choice is made" stays open
  for the code-signing choice (open question 7); the notices part is done.
