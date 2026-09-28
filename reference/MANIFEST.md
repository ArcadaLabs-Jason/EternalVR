# Reference library manifest

The project's research was done against a local library of primary documentation: specifications,
vendor guides, articles, other mods' source and read-me files, community feedback, and extracts of
game facts (cvar lists, typeinfo names, key bindings). Most of it is third-party material that this
repository cannot redistribute, so only this index is published; everything else under `reference/`
is ignored by git.

What the folder held, by category:

- **community**: player feedback on earlier DOOM Eternal VR options and on flat-to-VR tools.
- **content, design, embodiment, input, performance**: extracts of game facts (missions, settings,
  cvars, bindings) and design research (locomotion, comfort, scale), with sources.
- **foveation, upscaling**: Vulkan and OpenXR extension chapters, vendor guides and read-me files.
- **idtech7**: community reverse-engineering notes, cvar lists and typeinfo-generated headers.
- **prior-art**: notes on other VR mods and injectors.
- **openxr, vulkan, tooling**: specification chapters, SDK and tool documentation.

`tools/fetch_references.sh` re-fetches the public sources into `reference/` (the scripts it runs are
in `tools/references/`, one per topic). Items without a script were written by hand from the sources
listed below. Two of the project's own summaries now live in the tree: `docs/notes/eternal-pc-keybinds.md`
and `docs/notes/eternal-unit-scale-evidence.md`.

The tables below are the original index, kept for the source URLs and licences.

## community

| Path | What | Source URL | Fetched | License |
|---|---|---|---|---|
| reference/community/existing-eternal-vr-options-feedback.md | Player complaints and praise for Luke Ross R.E.A.L., Vk3DVision DOOM Eternal VR, vorpX/Depth3D, and DOOM VFR | https://github.com/helifax/Vk3DVision-Public/issues/3 ; https://www.vorpx.com/forums/topic/doom-eternal-desktop-viewer/ ; https://steamcommunity.com/app/782330/discussions/search/?q=vr ; https://roadtovr.com/doom-vfr-review/ ; Reddit threads listed inline | 2026-09-25 | short quotes for commentary |
| reference/community/flat2vr-tooling-feedback.md | Recurring UEVR requests (HUD adjust, crosshair, controllers without thumbrest, Linux) and PCVR Mods Installer Hub install-UX issues | https://github.com/praydog/UEVR/issues ; https://github.com/Mr-Nlce/PCVR-Mods-Installer-Hub/issues | 2026-09-25 | short quotes for commentary |
| reference/community/hardware-landscape-2026.md | Steam Hardware Survey Aug 2026 headset and GPU shares, Steam Frame launch facts | https://store.steampowered.com/hwsurvey/ ; https://store.steampowered.com/hwsurvey/videocard/ ; https://www.uploadvr.com/steamvr-usage-february-2026-steam-hardware-survey/ ; https://en.wikipedia.org/wiki/Steam_Frame | 2026-09-25 | factual figures, our wording |

## content

| Path | What | Source URL | Fetched | License |
|---|---|---|---|---|
| reference/content/eternal-mission-list.md | Every campaign mission (base, TAG1, TAG2) with runtime map path, setting, boss, Slayer Gate, VR-relevant notes; Master Levels; Horde and Battlemode map files | https://doom.fandom.com/wiki/Doom_Eternal (+ one page per level, see file header) ; https://github.com/snowzzrra/DoomEternal-AP-Mod (map_sources.json) ; EternalModInjector shell resource list ; https://github.com/loitho/doom-eternal | 2026-09-25 | CC BY-SA 3.0 extract; file names are facts |
| reference/content/eternal-modes-hub-and-progression.md | Fortress of Doom layout, Ripatorium, Slayer Gates / Secret / Escalation encounters, pickup-driven upgrade screens, runes, cheat codes, kill systems, modes | https://doom.fandom.com/wiki/Fortress_of_Doom and related pages (see header) ; https://en.wikipedia.org/wiki/Doom_Eternal ; https://news.xbox.com/en-us/2021/10/29/dive-into-horde-mode-doom-eternal/ | 2026-09-25 | CC BY-SA extract |
| reference/content/eternal-ui-typeinfo-inventory.md | HUD element, HUD menu, main-menu screen, settings data-source, HUD event and SWF asset names from the executable's typeinfo | https://github.com/brongo/m3337ho0o0ok (alltypes.h) ; https://github.com/SteamKaibz/DE_AdvancedOptionsModPublic | 2026-09-25 | identifiers (facts); Advanced Options BSD-2-Clause |
| reference/content/eternal-camera-and-sequence-types.md | Camera classes, player mechanics, traversal triggers, sync (glory kill) types, slow-mo sources, related cvars, autosplitter cutscene ids, Advanced Options player-state model | https://github.com/brongo/m3337ho0o0ok ; https://github.com/Official-KEX/doom-eternal-full-cvarlist ; https://github.com/loitho/doom-eternal | 2026-09-25 | identifiers; cvar list MIT |
| reference/content/eternal-video-files.md | Bink 2 video location, boot cvars, and every .bk2 name seen in update deltas, grouped by use | https://www.pcgamingwiki.com/wiki/Doom_Eternal ; https://wiki.eternalmods.com/books/7-miscellaneous/page/creating-video-mods ; https://github.com/mcdalcin/DoomEternalDownpatcher | 2026-09-25 | file names are facts |
| reference/content/eternal-settings-menu.md | PC settings tabs and options (launch Video tab from screenshots, later additions, Game/UI/Controls/Audio options via cvars) with the cvar behind each | https://www.dsogaming.com/pc-performance-analyses/doom-eternal-pc-performance-analysis/ ; https://www.pcgamingwiki.com/wiki/Doom_Eternal ; https://github.com/Official-KEX/doom-eternal-full-cvarlist | 2026-09-25 | labels/cvars are facts; screenshots not copied |

## design

| Path | What | Source URL | Fetched | License |
|---|---|---|---|---|
| docs/notes/eternal-pc-keybinds.md | DOOM Eternal default KB/M + gamepad bindings, every Slayer action to map | https://frondtech.com/doom-eternal-pc-keyboard-controls-and-key-bindings/ ; https://www.shacknews.com/article/117015/doom-eternal-controls-and-keybindings | 2026-09-25 | factual list, our wording |
| reference/design/eternal-mechanics-extract.md | Eternal mechanics extract: Meathook, Equipment Launcher, weak points, Crucible, Sentinel Hammer, cheats, Codex list, DLC/Horde dates | https://doom.fandom.com/wiki/ (Meat_Hook, Equipment_Launcher, Destructible_Demons, Cheat_Codes_(Doom_Eternal), Crucible, Sentinel_Hammer, Codex/Tutorials) ; https://en.wikipedia.org/wiki/Doom_Eternal ; https://news.xbox.com/en-us/2021/10/29/dive-into-horde-mode-doom-eternal/ | 2026-09-25 | CC BY-SA 3.0 (Fandom/Wikipedia extract) |
| reference/design/meta-locomotion-comfort.md | Meta locomotion best practices, comfort/usability, user-preference guidance | https://developers.meta.com/horizon/design/locomotion-best-practices/ ; https://developers.meta.com/horizon/design/locomotion-comfort-usability/ ; https://developers.meta.com/horizon/design/locomotion-user-preferences/ | 2026-09-25 | (c) Meta; paraphrased notes |
| reference/design/fov-restriction-research.md | Dynamic FOV restriction / vignette research citations and takeaways | https://www.researchgate.net/publication/301723818 ; https://dl.acm.org/doi/10.1007/s10055-020-00466-2 ; https://dl.acm.org/doi/10.1145/3562939.3565611 | 2026-09-25 | citations + paraphrase |
| reference/design/vr-locomotion-precedents.md | DOOM VFR, Hellsweeper VR, Sairento VR, Doom 3 Fully Possessed, Team Beef, Alyx/Boneworks notes | https://blog.playstation.com/2017/12/01/three-ways-to-slay-in-doom-vfr-out-today-for-playstation-vr/ ; https://www.uploadvr.com/hellsweeper-vr-review/ ; https://sairento.fandom.com/wiki/Controls_and_Locomotion ; https://compoundvr.com/games/doom-3-bfg/ | 2026-09-25 | paraphrased notes |

## embodiment

| Path | What | Source URL | Fetched | License |
|---|---|---|---|---|
| reference/embodiment/eternal-embodiment-cvars.md | DOOM Eternal cvars for player size/speed, units, hands/viewmodel, camera-moving view effects, player body, audio listener (verbatim rows) | https://github.com/Official-KEX/doom-eternal-full-cvarlist | 2026-09-25 | MIT |
| docs/notes/eternal-unit-scale-evidence.md | Evidence that id Tech 7 game units are metres (cvars, `_gu` suffix, shipped spawnPosition extents) vs inches in id Tech 4/6 | https://github.com/Official-KEX/doom-eternal-full-cvarlist ; https://github.com/snowzzrra/DoomEternal-AP-Mod | 2026-09-25 | our analysis; MIT sources |
| reference/embodiment/doom3bfg-vr-embodiment-extract.md | Doom 3 BFG VR scale/height modes, body modes, MotionMove room-scale with lean offset and eye-in-wall blank | https://github.com/CarlKenner/DOOM-3-BFG-VR (commit 4451656) | 2026-09-25 | GPL-3.0 (strings quoted only) |
| reference/embodiment/injector-prior-art-embodiment.md | UEVR world scale, sweep room-scale, UObjectHook weapon attach; REFramework head-oriented Wwise audio and RE8 hand/body IK | http://docs.uevr.io/usage/adding_6dof.html ; https://github.com/praydog/UEVR ; https://github.com/praydog/REFramework | 2026-09-25 | UEVR all rights reserved (described only); docs MIT; REFramework MIT |
| reference/embodiment/vr-scale-and-head-collision-research.md | IPD/eye-height scale perception studies; head-collision fade guidance; floor vs eye origin | https://journals.plos.org/plosone/article?id=10.1371%2Fjournal.pone.0232290 ; https://diglib.eg.org/items/40f2fe61-f43b-4a7f-9872-b8589eda8109 ; https://kholdstare.github.io/technical/2013/10/06/sense-of-scale-vr.html ; https://developers.meta.com/horizon/blog/lessons-from-the-frontlines-modern-vr-design-patterns/ ; https://link.springer.com/chapter/10.1007/978-3-030-55789-8_54 | 2026-09-25 | citations + paraphrase |

## foveation

| Path | What | Source URL | Fetched | License |
|---|---|---|---|---|
| foveation/spec/appendix-VK_EXT_fragment_density_map.adoc | Fragment density map extension appendix | https://raw.githubusercontent.com/KhronosGroup/Vulkan-Docs/main/appendices/VK_EXT_fragment_density_map.adoc | 2026-09-25 | CC-BY-4.0 |
| foveation/spec/appendix-VK_EXT_fragment_density_map2.adoc | Deferred density map reads | https://raw.githubusercontent.com/KhronosGroup/Vulkan-Docs/main/appendices/VK_EXT_fragment_density_map2.adoc | 2026-09-25 | CC-BY-4.0 |
| foveation/spec/appendix-VK_EXT_fragment_density_map_offset.adoc | Density map offset (gaze shift without rewrite) | https://raw.githubusercontent.com/KhronosGroup/Vulkan-Docs/main/appendices/VK_EXT_fragment_density_map_offset.adoc | 2026-09-25 | CC-BY-4.0 |
| foveation/spec/proposal-VK_EXT_fragment_density_map_offset.adoc | Proposal document for the above | https://raw.githubusercontent.com/KhronosGroup/Vulkan-Docs/main/proposals/VK_EXT_fragment_density_map_offset.adoc | 2026-09-25 | CC-BY-4.0 |
| foveation/spec/appendix-VK_QCOM_fragment_density_map_offset.adoc | Qualcomm predecessor of the offset extension | https://raw.githubusercontent.com/KhronosGroup/Vulkan-Docs/main/appendices/VK_QCOM_fragment_density_map_offset.adoc | 2026-09-25 | CC-BY-4.0 |
| foveation/spec/appendix-VK_NV_shading_rate_image.adoc | NVIDIA shading rate image (Turing-era VRS, mutually exclusive with KHR attachment VRS) | https://raw.githubusercontent.com/KhronosGroup/Vulkan-Docs/main/appendices/VK_NV_shading_rate_image.adoc | 2026-09-25 | CC-BY-4.0 |
| foveation/spec/openxr-varjo_quad_views.adoc | XR_VARJO_quad_views chapter | https://github.com/KhronosGroup/OpenXR-Docs/blob/main/specification/sources/chapters/extensions/varjo/varjo_quad_views.adoc | 2026-09-25 | CC-BY-4.0 |
| foveation/spec/openxr-varjo_foveated_rendering.adoc | XR_VARJO_foveated_rendering chapter (gaze-driven inset FOV) | https://github.com/KhronosGroup/OpenXR-Docs/blob/main/specification/sources/chapters/extensions/varjo/varjo_foveated_rendering.adoc | 2026-09-25 | CC-BY-4.0 |
| foveation/readmes/Quad-Views-Foveated-README.md | Quad views emulation + gaze-driven inset (mbucchia); repo last commit 2024-07-29; mbucchia/Meta-Foveated is the same repository | https://github.com/mbucchia/Quad-Views-Foveated | 2026-09-25 | MIT |
| foveation/readmes/PimaxMagic4All-README.md | Runs Pimax LibMagic (D3D11 + OpenVR, NVIDIA VRS) with other headsets' eye trackers; last commit 2026-03-28 | https://github.com/mbucchia/PimaxMagic4All | 2026-09-25 | MIT (tool); Pimax DLLs proprietary, not redistributed |
| foveation/readmes/OpenXR-Eye-Trackers-README.md | Bridges vendor eye trackers to XR_EXT_eye_gaze_interaction; now mostly superseded by native runtime support | https://github.com/mbucchia/OpenXR-Eye-Trackers | 2026-09-25 | MIT |
| foveation/readmes/vrperfkit-README.md | vrperfkit (fholger): FSR/NIS/CAS + NVIDIA VRS fixed foveation, D3D11 only | https://github.com/fholger/vrperfkit | 2026-09-25 | MIT |
| foveation/readmes/VRPerfKit_RSF-README.md | RavenSystem fork: adds radial density mask, hidden radial mask, FPS-driven dynamic radius | https://github.com/RavenSystem/VRPerfKit_RSF | 2026-09-25 | MIT |
| foveation/readmes/vrperfkit-granther-README.md | Fork adding eye tracking to vrperfkit FFR (work in progress, 2024) | https://github.com/Granther/foveated-rendering | 2026-09-25 | MIT |
| foveation/readmes/CheekyFoveatedDLSS-README.md | Foveated DLSS SR / RR / NR (DLSS 5) for Vulkan, D3D11, D3D12 games and UEVR; last commit 2026-09-25 | https://github.com/ClarkCheekyKent/CheekyFoveatedDLSS | 2026-09-25 | GPL-3.0 (study only; do not copy code into this MIT project) |
| foveation/readmes/dlss5-vr-README.md | DLSS 5 neural rendering bridge for R.E.A.L. VR mods (no foveation); last commit 2026-09-11 | https://github.com/eregnier/dlss5-vr | 2026-09-25 | MIT |
| foveation/readmes/PSVR2Toolkit-README.md | PSVR2 PC driver mod: eye tracking, calibration; eye data non-commercial only | https://github.com/BnuuySolutions/PSVR2Toolkit | 2026-09-25 | MIT (plus non-commercial terms on eye data) |
| foveation/articles/openxr-toolkit-fr.md | OpenXR Toolkit foveated rendering docs (presets, ring sizes, artifacts, upscaler interaction) | https://github.com/mbucchia/OpenXR-Toolkit/blob/gh-pages/fr.md | 2026-09-25 | MIT |
| foveation/articles/openxr-toolkit-et.md | OpenXR Toolkit eye tracking docs | https://github.com/mbucchia/OpenXR-Toolkit/blob/gh-pages/et.md | 2026-09-25 | MIT |
| foveation/articles/nvidia-turing-vrs-vrworks.md | NVIDIA, Turing Variable Rate Shading in VRWorks | https://developer.nvidia.com/blog/turing-variable-rate-shading-vrworks/ | 2026-09-25 | Copyright NVIDIA, reference only |
| foveation/articles/nvidia-adaptive-shading-deep-dive.md | NVIDIA Adaptive Shading in Wolfenstein: Youngblood (id Tech, Vulkan) | https://www.nvidia.com/en-us/geforce/news/nvidia-adaptive-shading-a-deep-dive/ | 2026-09-25 | Copyright NVIDIA, reference only |
| foveation/articles/amd-fidelityfx-variable-shading.md | AMD FidelityFX Variable Shading overview | https://gpuopen.com/fidelityfx-variable-shading/ | 2026-09-25 | Copyright AMD, reference only |
| foveation/articles/microsoft-vrcs-doom-the-dark-ages.md | Microsoft, variable rate compute shaders in DOOM: The Dark Ages (mentions Eternal's hardware VRS) | https://developer.microsoft.com/en-us/games/articles/2026/04/variable-rate-compute-shaders-doom-the-dark-ages/ | 2026-09-25 | Copyright Microsoft, reference only |
| foveation/articles/bigscreen-beyond-2e-dfr.md | Bigscreen Beyond 2e dynamic foveated rendering (early access, Dec 2025) | https://store.bigscreenvr.com/blogs/beyond/dynamic-foveated-rendering-with-bigscreen-beyond-2e | 2026-09-25 | Copyright Bigscreen, reference only |
| foveation/articles/pimax-dfr-crystal-super.md | Pimax on VRS vs quad views DFR gains | https://store.pimax.com/blogs/blogs/the-crystal-supers-secret-weapon-dynamic-foveated-rendering | 2026-09-25 | Copyright Pimax, reference only |
| foveation/articles/uploadvr-pimaxmagic4all.md | UploadVR on PimaxMagic4All (Nov 2025) | https://www.uploadvr.com/pimaxmagic4all-adds-eye-tracking-to-many-steamvr-games/ | 2026-09-25 | Copyright UploadVR, reference only |
| foveation/articles/uploadvr-msfs2024-foveated.md | UploadVR on MSFS 2024 fixed and eye-tracked foveation via quad views | https://www.uploadvr.com/microsoft-flight-simulator-2024-now-has-foveated-rendering/ | 2026-09-25 | Copyright UploadVR, reference only |
| foveation/articles/heise-dlss5-rtx40.md | heise on DLSS 5 launch, cost, GPU support (Sep 2026) | https://www.heise.de/en/news/DLSS-5-Nvidia-is-finally-bringing-neural-rendering-to-RTX-40-cards-11441795.html | 2026-09-25 | Copyright heise, reference only |
| foveation/articles/nvidia-dlss5-announcement.md | NVIDIA DLSS 5 announcement | https://nvidianews.nvidia.com/news/nvidia-dlss-5-delivers-ai-powered-breakthrough-in-visual-fidelity-for-games | 2026-09-25 | Copyright NVIDIA, reference only |
| Path | What | Source URL | Fetched | License |
|---|---|---|---|---|
| _cache/Quad-Views-Foveated/ | Source (layer.cpp has fixed vs gaze inset sizes, gaze projection) | https://github.com/mbucchia/Quad-Views-Foveated | 2026-09-25 | MIT |
| _cache/Quad-Views-Foveated.wiki/ | Per-headset eye tracking setup (Steam Frame, Quest Pro, PSVR2, Beyond 2e, ...) | https://github.com/mbucchia/Quad-Views-Foveated/wiki | 2026-09-25 | MIT (repo) |
| _cache/PimaxMagic4All/, _cache/PimaxMagic4All.wiki/ | Source and wiki | https://github.com/mbucchia/PimaxMagic4All | 2026-09-25 | MIT |
| _cache/OpenXR-Eye-Trackers/, _cache/OpenXR-Eye-Trackers.wiki/ | Source (per-vendor gaze sources: Steam Link OSC, VD shared memory, PSVR2 Toolkit IPC, Pimax, Varjo, Omnicept) and wiki | https://github.com/mbucchia/OpenXR-Eye-Trackers | 2026-09-25 | MIT |
| _cache/OpenXR-Toolkit/ | Source (vrs.cpp, VRS.hlsl: ellipse ring mask, presets, projection-center calibration) | https://github.com/mbucchia/OpenXR-Toolkit | 2026-09-25 | MIT |
| _cache/vrperfkit/, _cache/VRPerfKit_RSF/, _cache/vrperfkit-granther/ | Source (d3d11_variable_rate_shading.cpp: ring pattern around per-eye projection centre) | see readmes rows | 2026-09-25 | MIT |
| _cache/CheekyFoveatedDLSS/ | Source (center/periphery DLSS split, gaze policy, NR foveation) | https://github.com/ClarkCheekyKent/CheekyFoveatedDLSS | 2026-09-25 | GPL-3.0 |
| _cache/dlss5-vr/ | Source | https://github.com/eregnier/dlss5-vr | 2026-09-25 | MIT |
| _cache/PSVR2Toolkit/ | Source | https://github.com/BnuuySolutions/PSVR2Toolkit | 2026-09-25 | MIT + non-commercial eye data terms |
| _cache/Vulkan-Samples-fsr/ | Khronos Vulkan-Samples (samples/extensions/fragment_shading_rate, fragment_shading_rate_dynamic, fragment_density_map) | https://github.com/KhronosGroup/Vulkan-Samples | 2026-09-25 | Apache-2.0 |
| _cache/foveation/Albert2017-latency-foveated.pdf | Albert, Patney, Luebke, Kim, "Latency Requirements for Foveated Rendering in Virtual Reality", ACM TAP 2017 | https://research.nvidia.com/sites/default/files/pubs/2017-09_Latency-Requirements-for/a25-albert.pdf | 2026-09-25 | Copyright ACM/authors |
| _cache/foveation-primsrast.adoc | Vulkan-Docs rasterization chapter source (fragment shading rate and density map rules) | https://raw.githubusercontent.com/KhronosGroup/Vulkan-Docs/main/chapters/primsrast.adoc | 2026-09-25 | CC-BY-4.0 |

## idtech7

| Path | What | Source URL | Fetched | License |
|---|---|---|---|---|
| tools/references/idtech7.sh | Idempotent re-fetch of every item below | (script) | 2026-09-25 | MIT (ours) |
| reference/idtech7/signature-catalog.md | How Meathook, DE Advanced Options, EternalPatcher and speedrun tools locate engine objects (anchors, RTTI names, sigs, patches) | compiled from the repos below | 2026-09-25 | MIT (ours); cited data per source |
| reference/idtech7/bfg-source-index.md | Annotated index of DOOM 3 BFG source files relevant to VR, with what survives into id Tech 7 | https://github.com/id-Software/DOOM-3-BFG | 2026-09-25 | MIT (ours); links to GPL-3.0 code |
| reference/idtech7/docs/coenen-doom-eternal-graphics-study.md | Simon Coenen frame study: pass order, UI target, gun in pre-pass, post chain | https://simoncoenen.com/blog/programming/graphics/DoomEternalStudy | 2026-09-25 | (c) Simon Coenen, reference |
| reference/idtech7/docs/siggraph2020-rendering-doom-eternal-slides.md | Extracted text of "Rendering the Hellscape of DOOM Eternal" (Umbra + GPU culling, binning, geometry caches) | https://advances.realtimerendering.com/s2020/RenderingDoomEternal.pdf | 2026-09-25 | (c) id Software / authors, reference |
| reference/idtech7/docs/pcgh-billy-khan-idtech7-interview-2020.md | Billy Khan interview (German original) + our English notes | https://www.pcgameshardware.de/Doom-Eternal-Spiel-72610/Specials/Interview-mit-Lead-Engine-Programmer-Billy-Khan-1360708/ (via web.archive.org) | 2026-09-25 | (c) PC Games Hardware, reference |
| reference/idtech7/docs/eternalmods-wiki-re-file-formats.md | eternalmods wiki book "Reverse Engineering / File Formats" incl. chrispy's id Tech 7 RE notes (BFG lineage) | https://wiki.eternalmods.com/books/eternal-reverse-engineering-file-formats | 2026-09-25 | wiki content, reference |
| reference/idtech7/docs/eternalmods-wiki-command-console.md | Console commands, cvars, Meath00k command list | https://wiki.eternalmods.com/books/eternal-command-console | 2026-09-25 | wiki content, reference |
| reference/idtech7/docs/eternalmods-wiki-idstudio.md | idStudio / launcher / sandbox details, packaging | https://wiki.eternalmods.com/books/eternal-idstudio | 2026-09-25 | wiki content, reference |
| reference/idtech7/docs/eternalmods-wiki-miscellaneous-swf-atlan.md | SWF (HUD/menu) modding, porting to Atlan Mod Loader, entitydefs | https://wiki.eternalmods.com/books/eternal-miscellaneous | 2026-09-25 | wiki content, reference |
| reference/idtech7/docs/eternalmods-wiki-how-to-create-mods.md | Resource extraction, file extensions, EternalMod.json | https://wiki.eternalmods.com/books/eternal-how-to-create-mods | 2026-09-25 | wiki content, reference |
| reference/idtech7/docs/idstudio-official-docs-selected.md | Official idStudio docs: mods FAQ, mod packer/structure, decls, console, useful cvars, known issues, release notes 2024-08 to 2026-05 | https://idstudio.idsoftware.com/ | 2026-09-25 | (c) id Software, reference |
| reference/idtech7/typeinfo/meathook-type-names-6.66.txt | 12,062 type names from DOOM Eternal typeinfo (~6.66) | https://github.com/brongo/m3337ho0o0ok/blob/master/m34thook/alltypes.h | 2026-09-25 | none stated (game-derived names), reference |
| reference/idtech7/typeinfo/meathook-cvar-names-6.66.txt | 7,391 cvar names (~6.66) | https://github.com/brongo/m3337ho0o0ok/blob/master/m34thook/pregenerated/doom_eternal_cvars_generated.hpp | 2026-09-25 | none stated (game-derived names), reference |
| reference/idtech7/typeinfo/meathook-property-names-6.66.txt | 32,365 reflected field names (~6.66) | https://github.com/brongo/m3337ho0o0ok/blob/master/m34thook/pregenerated/doom_eternal_properties_generated.hpp | 2026-09-25 | none stated (game-derived names), reference |
| reference/idtech7/typeinfo/kex-cvarlist-2024.tsv | 7,397 cvars with description, value, type/range (TSV of the xlsx) | https://github.com/Official-KEX/doom-eternal-full-cvarlist | 2026-09-25 | MIT |
| reference/idtech7/typeinfo/vr-relevant-cvars.md | Curated VR-relevant cvar table generated from the two lists above | generated | 2026-09-25 | MIT (ours) / MIT data |
| reference/idtech7/typeinfo/advancedoptions-idLib_Vanilla-rev3.h | Typeinfo-generated partial structs with offsets and designer comments, retail exe 6.66 Rev 3 (`idPlayer`, `idHUD`, `idDeclWeapon`, `idSWF*`...) | https://github.com/SteamKaibz/DE_AdvancedOptionsModPublic/blob/f6368680c0e0fad5192f834dfe0d21232328b3bf/DE/idLib_Vanilla.h | 2026-09-25 | BSD-2-Clause |
| reference/idtech7/typeinfo/advancedoptions-idLib_Sandbox-rev3.h | Same for the sandbox exe | .../DE/idLib_Sandbox.h | 2026-09-25 | BSD-2-Clause |
| reference/idtech7/typeinfo/advancedoptions-Sigs-rev3.h | ~90 function/global signatures for Rev 3 (ProcessInput, FOV, sync, SWF render, cvar/cmd, typeinfo) | .../DE/Sigs.h | 2026-09-25 | BSD-2-Clause |
| reference/idtech7/typeinfo/advancedoptions-LICENSE.txt | License for the three files above | .../LICENSE.txt | 2026-09-25 | BSD-2-Clause |
| reference/_cache/meathook/ | Meathook v7.2 full source (e12dd75) -- study only | https://github.com/brongo/m3337ho0o0ok | 2026-09-25 | none stated |
| reference/_cache/meathook-ap/ | Meathook fork with Archipelago RPC runtime, Aug 2026 (ea9eea1) -- study only | https://github.com/snowzzrra/Meathook-AP | 2026-09-25 | none stated |
| reference/_cache/de_advancedoptions/ | DE Advanced Options Mod source, Rev 3 (f636868) | https://github.com/SteamKaibz/DE_AdvancedOptionsModPublic | 2026-09-25 | BSD-2-Clause |
| reference/_cache/ap-mod/ | DOOM Eternal Archipelago game-side repo, Rev 3.1/3.2 probes (0d7d15c) -- study only | https://github.com/snowzzrra/DoomEternal-AP-Mod | 2026-09-25 | none stated |
| reference/_cache/cvarlist/ | KEX complete cvar list xlsx (3dc4eb9) | https://github.com/Official-KEX/doom-eternal-full-cvarlist | 2026-09-25 | MIT |
| reference/_cache/eternalpatcher/ | EternalPatcher (patch-definition format) (87a1aa5) | https://github.com/dcealopez/EternalPatcher | 2026-09-25 | none stated |
| reference/_cache/eternalbasher/ | EternalBasher incl. current `EternalPatcher.def` (exe hashes to 2026-09-17, console/cvar unlock patterns) (db798b3) | https://github.com/leveste/EternalBasher | 2026-09-25 | GPL-3.0 |
| reference/_cache/restricted-cmds/ | Restricted console commands unlocker (9a0281f) | https://github.com/dpteam/DOOM_Eternal_Restricted_CMDs_Patcher_Unlocker | 2026-09-25 | GPL-3.0 |
| reference/_cache/livesplit/ | LiveSplit autosplitter with per-version RVAs to Rev 3.2 (d38745d) | https://github.com/loitho/doom-eternal | 2026-09-25 | none stated |
| reference/_cache/downpatcher/ | Steam downpatcher: depot manifests and file lists per version (49f070b) | https://github.com/mcdalcin/DoomEternalDownpatcher | 2026-09-25 | none stated |
| reference/_cache/de_internal/ | Small internal trainer with pointer paths (27a6710, 2022) -- study only | https://github.com/marcopetro/DoomEternal_Internal | 2026-09-25 | none stated |
| reference/_cache/desru/ | Speedrun utility (memory reads) (f78f602) | https://github.com/bowsr/DESRU | 2026-09-25 | GPL-3.0 |
| reference/_cache/siggraph2020/RenderingDoomEternal.pdf | SIGGRAPH 2020 slides (11 MB) | https://advances.realtimerendering.com/s2020/RenderingDoomEternal.pdf | 2026-09-25 | (c) id Software / authors |
| reference/_cache/idtech7-web/ | Raw HTML/markdown downloads used to build the docs above | (various, see fetch.sh) | 2026-09-25 | per source |

## input

| Path | What | Source | Fetched | License |
|---|---|---|---|---|
| `reference/input/pcgamingwiki-doom-eternal-input.md` | PCGamingWiki DOOM Eternal Input / Steam Input / API / Middleware rows, trimmed transcription | https://www.pcgamingwiki.com/wiki/Doom_Eternal (revision 1805165, 2026-09-23, via the MediaWiki parse API) | 2026-09-25 | CC BY-NC-SA 3.0, PCGamingWiki contributors |
| `reference/input/eternal-input-cvars.md` | 202 input-related cvars (mouse, joy_ curves, focus, aim assist, fire origin, meathook view, wheel/hold/toggle, menus/glyphs, rumble, usercmd) grouped from the KEX dump, with a 2021-presence check | https://github.com/Official-KEX/doom-eternal-full-cvarlist (3dc4eb9); Meathook cvar enum https://github.com/brongo/m3337ho0o0ok (e12dd75) | 2026-09-25 | MIT (KEX list); engine strings |
| `reference/input/eternal-usercmd-and-input-events.md` | Eternal usercmd 64-bit button mask, isKeyPressed layout, SendButtonPress / ProcessInput signatures, idUCmdTracker inhibit flags, input event queue hook and `inputEventType_t` / `keyNum_t` (incl. VR key block), `engine_t` input members, XInput1_3 import evidence | https://github.com/SteamKaibz/DE_AdvancedOptionsModPublic (f636868); https://github.com/brongo/m3337ho0o0ok (e12dd75); https://github.com/snowzzrra/DoomEternal-AP-Mod (0d7d15c) | 2026-09-25 | BSD-2 (Advanced Options, credit SteamKaibz); Meathook has no license, names/values only |
| `reference/input/doom3bfg-usercmd-reference.md` | Where to read the DOOM 3 BFG usercmd, generator, joystick curve, XInput thread and player view-angle code, plus how the BFG VR fork fired from the muzzle; notes on what carries into id Tech 7 | https://github.com/id-Software/DOOM-3-BFG ; https://github.com/CarlKenner/DOOM-3-BFG-VR (4451656) | 2026-09-25 | GPL-3.0 sources, summarised only (no code copied) |

## openxr

| Path | What | Source URL | Fetched | License |
|---|---|---|---|---|
| tools/references/openxr.sh | Idempotent re-fetch of every OpenXR/runtime item below and of the _cache clones; `REFRESH_COPIES=1` refreshes the committed copies | (script) | 2026-09-25 | MIT (ours) |
| reference/openxr/registry/xr.xml | OpenXR API registry (spec 1.1.63): every command, struct, enum, extension, interaction-profile path | https://github.com/KhronosGroup/OpenXR-Docs/blob/main/specification/registry/xr.xml | 2026-09-25 | Apache-2.0 |
| reference/openxr/spec-chapters/*.adoc | Spec 1.1.63 source chapters: instance, system, session (lifecycle), rendering (frame loop, swapchains, layers, sRGB rules), spaces, view_configurations, input (action system, interaction profiles), versions (1.1 promotions) | https://github.com/KhronosGroup/OpenXR-Docs/tree/main/specification/sources/chapters | 2026-09-25 | CC-BY-4.0 (Khronos spec source; normative-wording copyright notice applies) |
| reference/openxr/extensions/khr_*.adoc | KHR extension chapters: vulkan_enable, vulkan_enable2, vulkan_swapchain_format_list, composition_layer_depth/cylinder/equirect2/color_scale_bias, visibility_mask, maintenance1, locate_spaces, binding_modification, generic_controller, win32_convert_performance_counter_time | https://github.com/KhronosGroup/OpenXR-Docs/tree/main/specification/sources/chapters/extensions/khr | 2026-09-25 | CC-BY-4.0 |
| reference/openxr/extensions/ext_*.adoc | EXT chapters: hand_tracking, hand_interaction, eye_gaze_interaction, local_floor, palm_pose, dpad_binding, active_action_set_priority, hp_mixed_reality_controller, performance_settings, user_presence, debug_utils, frame_synthesis, haptic_parametric, view_configuration_views_change | https://github.com/KhronosGroup/OpenXR-Docs/tree/main/specification/sources/chapters/extensions/ext | 2026-09-25 | CC-BY-4.0 |
| reference/openxr/extensions/fb_*.adoc, meta_*.adoc | Vendor chapters: FB foveation (+configuration, +vulkan), swapchain_update_state, display_refresh_rate, composition_layer_settings, haptic_pcm/amplitude_envelope, touch_controller_pro; META foveation_eye_tracked, touch_controller_plus, recommended_layer_resolution, performance_metrics, vulkan_swapchain_create_info | https://github.com/KhronosGroup/OpenXR-Docs/tree/main/specification/sources/chapters/extensions | 2026-09-25 | CC-BY-4.0 |
| reference/openxr/loader/*.adoc | OpenXR loader design doc: runtime discovery (ActiveRuntime, AvailableRuntimes, XR_RUNTIME_JSON), API layer discovery/ordering (ApiLayers\Implicit, enable/disable_environment), negotiation | https://github.com/KhronosGroup/OpenXR-SDK-Source/tree/main/specification/loader | 2026-09-25 | Apache-2.0 / CC-BY-4.0 |
| reference/openxr/inventory/*.json (+ .license) | OpenXR-Inventory runtime reports: SteamVR 2.14.4, Meta PC 1.205 (normal + developer mode), WMR 112.2211, Varjo, Monado Windows, HTC Vive Cosmos | https://github.com/KhronosGroup/OpenXR-Inventory/tree/main/runtimes | 2026-09-25 | CC-BY-4.0 |
| reference/openxr/inventory/pc-runtime-extension-matrix.md | Generated extension-by-runtime matrix for PC runtimes incl. VDXR (from its source) | generated by reference/openxr/inventory/gen_matrix.py | 2026-09-25 | CC-BY-4.0 data / ours |
| reference/openxr/inventory/gen_matrix.py | Generator for the matrix above | (script) | 2026-09-25 | MIT (ours) |
| reference/openxr/vdxr/README.md | VirtualDesktopXR README (1.0/1.1 implementation, conformance note) | https://github.com/mbucchia/VirtualDesktop-OpenXR/blob/main/README.md | 2026-09-25 | MIT |
| reference/openxr/vdxr/Home.md | VDXR wiki home: runtime comparison (Oculus OpenXR vs SteamVR vs VDXR), headsets, setup, log location | https://github.com/mbucchia/VirtualDesktop-OpenXR/wiki | 2026-09-25 | wiki content, repo MIT |
| reference/openxr/vdxr/Developers.md | VDXR supported features/extensions table incl. Vulkan timeline-semaphore requirement for enable1 | https://github.com/mbucchia/VirtualDesktop-OpenXR/wiki/Developers | 2026-09-25 | wiki content, repo MIT |
| reference/openxr/vdxr/Application-Compatibility.md | VDXR per-application compatibility list | https://github.com/mbucchia/VirtualDesktop-OpenXR/wiki/Application-Compatibility | 2026-09-25 | wiki content, repo MIT |
| reference/openxr/vdxr/Oculus-Runtimes.md | VDXR wiki: Oculus/Meta runtime families (OVR, OpenXR, OVRPlugin) | https://github.com/mbucchia/VirtualDesktop-OpenXR/wiki/Oculus-%22Runtimes%22 | 2026-09-25 | wiki content, repo MIT |
| reference/openxr/vdxr/OculusXR-OVRPlugin-Compatibility-Mode.md | VDXR wiki: OVRPlugin runtime-lockout compatibility mode | https://github.com/mbucchia/VirtualDesktop-OpenXR/wiki/OculusXR-(OVRPlugin)-Compatibility-Mode | 2026-09-25 | wiki content, repo MIT |
| reference/openxr/web/openxr-tutorial-2-setup.md | OpenXR Tutorial ch.2 (Windows/Vulkan): instance, system, session setup | https://openxr-tutorial.com/windows/vulkan/2-setup.html | 2026-09-25 | Apache-2.0 / CC-BY-4.0 (KhronosGroup/OpenXR-Tutorials) |
| reference/openxr/web/openxr-tutorial-3-graphics.md | OpenXR Tutorial ch.3: swapchains, frame loop, projection layers | https://openxr-tutorial.com/windows/vulkan/3-graphics.html | 2026-09-25 | Apache-2.0 / CC-BY-4.0 |
| reference/openxr/web/openxr-tutorial-4-actions.md | OpenXR Tutorial ch.4: action sets, bindings, haptics | https://openxr-tutorial.com/windows/vulkan/4-actions.html | 2026-09-25 | Apache-2.0 / CC-BY-4.0 |
| reference/openxr/web/fredemmott-openxr-api-layer-best-practices.md | Best practices for OpenXR API layers on Windows (HKLM, ordering, signing, enable2 over enable1) | https://fredemmott.com/blog/2024/11/25/best-practices-for-openxr-api-layers.html | 2026-09-25 | (c) Fred Emmott; reference copy |
| reference/openxr/web/steamvr-openxr-supported-features-thread.md | Community-maintained SteamVR OpenXR feature list (last edited Aug 2026): supported/unsupported extensions, swapchain formats, depth only forwarded to third-party drivers | https://steamcommunity.com/app/250820/discussions/8/3121550424355682585/ | 2026-09-25 | Steam forum post; reference copy |
| reference/openxr/web/steamvr-openxr-vulkan-extension-list-thread.md | SteamVR xrGetVulkanDeviceExtensionsKHR over-reporting (VK_EXT_debug_marker, VK_NV_*) and the query-device-first workaround | https://steamcommunity.com/app/250820/discussions/8/2448217320137020268/ | 2026-09-25 | Steam forum post; reference copy |
| reference/openxr/web/openxr-toolkit-site-index.md | OpenXR Toolkit site: discontinued since 2024, not recommended, no Vulkan support; runtime/eye-tracking tables | https://github.com/mbucchia/OpenXR-Toolkit/blob/gh-pages/index.md | 2026-09-25 | MIT |
| reference/openxr/web/pimax-play-integrated-openxr.md | Pimax announcement: native OpenXR runtime + quad views in Pimax Play, successor to PimaxXR | https://store.pimax.com/blogs/blogs/announcement-pimax-play-to-get-integrated-openxr-and-quadviews-functionality | 2026-09-25 | (c) Pimax; reference copy |
| reference/openxr/web/roadtovr-wmr-removed-24h2.md | WMR removed in Windows 11 24H2 | https://roadtovr.com/windows-11-drops-wmr-support-24h2/ | 2026-09-25 | (c) Road to VR; reference copy |
| reference/openxr/web/wikipedia-steam-frame.md | Steam Frame headset (eye tracking, foveated streaming, controllers) | https://en.wikipedia.org/wiki/Steam_Frame | 2026-09-25 | CC BY-SA 4.0 |
| reference/_cache/openxr/xrspec-1.1.html | Full OpenXR 1.1.63 spec, single-file HTML (24 MB) | https://registry.khronos.org/OpenXR/specs/1.1/html/xrspec.html | 2026-09-25 | Khronos spec copyright (cache only) |
| reference/_cache/openxr/xrspec-1.1.pdf | Full OpenXR 1.1 spec PDF (20 MB) | https://registry.khronos.org/OpenXR/specs/1.1/pdf/xrspec.pdf | 2026-09-25 | Khronos spec copyright (cache only) |
| reference/_cache/openxr/openxr_loader_spec.html | Rendered loader design doc | https://registry.khronos.org/OpenXR/specs/1.1/loader.html | 2026-09-25 | Khronos (cache only) |
| reference/_cache/openxr/openxr-10-reference-guide.pdf | OpenXR 1.0 reference guide (no 1.1 card is published) | https://www.khronos.org/files/openxr-10-reference-guide.pdf | 2026-09-25 | Khronos (cache only) |
| reference/_cache/OpenXR-Docs/ | Spec sources clone | https://github.com/KhronosGroup/OpenXR-Docs | 2026-09-25 | Apache-2.0 / CC-BY-4.0 |
| reference/_cache/OpenXR-SDK-Source/ | Loader, hello_xr (graphicsplugin_vulkan.cpp), API layer samples | https://github.com/KhronosGroup/OpenXR-SDK-Source | 2026-09-25 | Apache-2.0 |
| reference/_cache/OpenXR-Inventory/ | Runtime extension inventory clone | https://github.com/KhronosGroup/OpenXR-Inventory | 2026-09-25 | CC-BY-4.0 |
| reference/_cache/VirtualDesktop-OpenXR/ (+ .wiki/) | VDXR runtime source (vulkan_interop.cpp shows D3D11 interop, required VK extensions) | https://github.com/mbucchia/VirtualDesktop-OpenXR | 2026-09-25 | MIT |
| reference/_cache/OpenXR-Toolkit/ | Discontinued API layer (reference only) | https://github.com/mbucchia/OpenXR-Toolkit | 2026-09-25 | MIT |
| reference/_cache/Quad-Views-Foveated/ | API layer emulating quad views + eye-tracked foveation on stereo runtimes | https://github.com/mbucchia/Quad-Views-Foveated | 2026-09-25 | MIT |
| reference/_cache/BotW-BetterVR/ | Vulkan-layer VR mod that runs OpenXR on D3D12 and shares Vulkan images (src/hooking/layer.cpp, src/rendering/texture.cpp) | https://github.com/Crementif/BotW-BetterVR | 2026-09-25 | MIT |

## performance

| Path | What | Source URL | Fetched | License |
|---|---|---|---|---|
| reference/performance/techpowerup-doom-eternal-launch-2020.md | Launch-build GPU FPS (1080p/1440p/4K Ultra Nightmare), VRAM by resolution, settings menu notes | https://www.techpowerup.com/review/doom-eternal-benchmark-test-performance-analysis/ | 2026-09-25 | (c) TechPowerUp; transcribed figures + paraphrase |
| reference/performance/techpowerup-gpu-reviews-doom-eternal.md | DOOM Eternal FPS for RTX 3070-4090, RX 6800-7900 XTX from three TPU GPU reviews, with test systems | https://www.techpowerup.com/review/nvidia-geforce-rtx-4080-founders-edition/15.html ; https://www.techpowerup.com/review/amd-radeon-rx-7900-xtx/15.html ; https://www.techpowerup.com/review/amd-radeon-rx-7800-xt/13.html | 2026-09-25 | (c) TechPowerUp; transcribed figures |
| reference/performance/dsogaming-rt-dlss-2021.md | RT + DLSS numbers on RTX 3080 at 4K Ultra Nightmare | https://www.dsogaming.com/pc-performance-analyses/doom-eternal-ray-tracing-dlss-benchmarks/ | 2026-09-25 | (c) DSOGaming; paraphrase |
| reference/performance/hothardware-rt-2021.md | RT VRAM cost (+1.5 GB), DLSS recovery | https://hothardware.com/reviews/doom-eternal-ray-tracing-tested | 2026-09-25 | (c) HotHardware; paraphrase |
| reference/performance/gamegpu-rt-vram-2021.md | VRAM/RAM with RT by card size and resolution; VRAM gate bypass cvar | https://en.gamegpu.com/test-gpu/action-fps-tps/doom-eternal-test-rtx | 2026-09-25 | (c) GameGPU; transcribed figures |
| reference/performance/tomshardware-gpu-cpu-2020.md | Menu VRAM requirement per preset, preset cost, CPU scaling | https://www.tomshardware.com/features/doom_eternal-graphics_cpu-performance-comparison | 2026-09-25 | (c) Future plc; paraphrase |
| reference/performance/headset-render-targets.md | Panel resolutions, refresh rates, runtime-recommended render sizes, stereo Mpix per frame | https://en.wikipedia.org/wiki/Comparison_of_virtual_reality_headsets ; https://en.wikipedia.org/wiki/Steam_Frame ; https://en.wikipedia.org/wiki/Pimax ; https://en.wikipedia.org/wiki/Bigscreen_Beyond | 2026-09-25 | CC BY-SA 4.0 (Wikipedia facts) + our tables |
| reference/performance/upscaler-execution-costs.md | DLSS SR execution time/VRAM per GPU and preset; FSR2 timing/memory | reference/_cache/upscaling/DLSS_Programming_Guide_Release.pdf ; https://github.com/GPUOpen-Effects/FidelityFX-FSR2 | 2026-09-25 | NVIDIA doc (c) NVIDIA, figures quoted; FSR2 MIT |
| reference/performance/eternal-perf-cvars.md | Tick, pacing, resolution-scale, memory and post-effect cvars with verbatim descriptions | https://github.com/Official-KEX/doom-eternal-full-cvarlist (local reference/_cache/cvarlist) | 2026-09-25 | community dump; see cache LICENSE |
| reference/performance/openxr-runtime-reprojection-support.md | XR_FB_space_warp / composition_layer_depth support per runtime | https://github.com/KhronosGroup/OpenXR-Inventory (local reference/_cache/OpenXR-Inventory) | 2026-09-25 | Apache-2.0 / CC-BY-4.0 (inventory data) |

## prior-art

| Path | What | Source URL | Fetched | License |
|---|---|---|---|---|
| reference/_cache/uevr/ | UEVR full source (commit 4ee5c6b) -- study only | https://github.com/praydog/UEVR | 2026-09-25 | All rights reserved (main code); `include/uevr/*` MIT |
| reference/_cache/uevr-docs/ | UEVR documentation source (fc19e66) | https://github.com/praydog/uevr-docs | 2026-09-25 | MIT |
| reference/_cache/reframework/ | REFramework sparse: `src/mods/VR.*`, `src/mods/vr/`, `scripts/`, README, LICENSE (d146137) | https://github.com/praydog/REFramework | 2026-09-25 | MIT |
| reference/_cache/kananlib/ | Binary analysis/scanning library used by UEVR/REFramework (43cb535) | https://github.com/cursey/kananlib | 2026-09-25 | Boost 1.0 |
| reference/_cache/safetyhook/ | Inline/mid/VMT hooking library (f44cc07) | https://github.com/cursey/safetyhook | 2026-09-25 | Boost 1.0 |
| reference/_cache/libhat/ | SIMD signature scanner (7c0a062) | https://github.com/BasedInc/libhat | 2026-09-25 | MIT |
| reference/_cache/hooking-patterns/ | Hooking.Patterns scanner (460a47b) | https://github.com/ThirteenAG/Hooking.Patterns | 2026-09-25 | MIT-style (NTAuthority) |
| reference/_cache/minhook/ | MinHook (8af6b4a) | https://github.com/TsudaKageyu/minhook | 2026-09-25 | BSD-2-Clause |
| reference/_cache/doom3bfg-vr/ | DOOM 3 BFG VR "Fully Possessed" full source (4451656) -- study only | https://github.com/CarlKenner/DOOM-3-BFG-VR | 2026-09-25 | GPL-3.0 |
| reference/_cache/doom3quest/ | Doom3Quest sparse: VR glue + GUI renderer (cfb5a20) -- study only | https://github.com/DrBeef/Doom3Quest | 2026-09-25 | GPL |
| reference/_cache/crysis-vrmod/ | Crysis VR sparse: `Code/` (2e80459) -- study only | https://github.com/fholger/crysis_vrmod | 2026-09-25 | CryENGINE 2 Mod SDK licence |
| reference/_cache/thedarkmodvr/ | The Dark Mod VR sparse: renderer/game/framework (6532205) -- study only | https://github.com/fholger/thedarkmodvr | 2026-09-25 | GPL-3.0 (code) |
| reference/_cache/vrperfkit/ | VR performance toolkit (a52f8a4) | https://github.com/fholger/vrperfkit | 2026-09-25 | MIT |
| reference/_cache/hl2vr-d3d9/ | HL2VR d3d9 proxy (ba14832) | https://github.com/DrBeef/HL2VR_d3d9 | 2026-09-25 | Apache-2.0 |
| reference/_cache/hl2vru/ | HL2VRU fork README only (7c7ec0c) | https://github.com/vittorioromeo/HL2VRU | 2026-09-25 | none stated |
| reference/_cache/vk3dvision/ | Vk3DVision public repo (README/LICENSE only; driver closed) (b368f1d) | https://github.com/helifax/Vk3DVision-Public | 2026-09-25 | BSD-3-Clause (repo) |
| reference/_cache/3dmigoto/ | 3DMigoto sparse: `DirectX11/`, `Dependencies/d3dx.ini` (8f329bd) -- study only | https://github.com/bo3b/3Dmigoto | 2026-09-25 | GPL-3.0 |
| reference/prior-art/docs/praydog-uevr-exploration.md | UEVR techniques article | https://praydog.com/reverse-engineering/2023/07/03/uevr.html | 2026-09-25 | (c) praydog, quoted for reference |
| reference/prior-art/docs/aixxe-safetyhook-midhooks.md | SafetyHook mid-function hooks article | https://aixxe.net/2022/12/safetyhook-midfn-hooking | 2026-09-25 | (c) author, reference |
| reference/prior-art/docs/lukeross-gta5-real-readme.md | R.E.A.L. mod README (AER, HUD, cutscenes) | https://github.com/LukeRoss00/gta5-real-mod | 2026-09-25 | none stated, reference |
| reference/prior-art/docs/mixed-news-real-vr-aer.md | Article on R.E.A.L. VR / AER v2 | https://mixed-news.com/en/real-vr-mod-dlss-ray-reconstruction/ | 2026-09-25 | (c) MIXED, reference |
| reference/prior-art/docs/vorpx-support-faq.md | vorpX FAQ (EdgePeek, Geometry vs Z3D) | https://www.vorpx.com/support-faq/ | 2026-09-25 | (c) vorpX, reference |
| reference/prior-art/docs/vorpx-znormal-vs-zadaptive.md | vorpX forum thread on Z3D modes | https://www.vorpx.com/forums/topic/difference-between-z-normal-and-z-adaptive/ | 2026-09-25 | (c) authors, reference |
| reference/prior-art/docs/helixmod-geo11-announcement.md | geo-11 announcement | https://helixmod.blogspot.com/2022/06/announcing-new-geo-11-3d-driver.html | 2026-09-25 | (c) authors, reference |
| reference/prior-art/docs/vk3dvision-game-fixes.md | Vk3DVision game fix list (DOOM Eternal VR entry) | https://3dsurroundgaming.com/Vk3DVisionGames.html | 2026-09-25 | (c) Helifax, reference |
| reference/prior-art/docs/vk3dvision-doom-eternal-vr-notes.md | Our notes on the Vk3DVision DOOM Eternal VR v0.90 profile | https://3dsurroundgaming.com/Vk3DVision/SFS_Releases/DOOM-Eternal-VR-0.90.7z | 2026-09-25 | our notes (MIT); archive not mirrored |
| reference/prior-art/docs/roadtovr-doom-vfr-locomotion.md | DOOM VFR locomotion (teleport/dash) | https://www.roadtovr.com/doom-vfr-devs-detail-gameplay-setting-locomotion-new-video/ | 2026-09-25 | (c) Road to VR, reference |
| reference/prior-art/docs/roadtovr-doom-vfr-review.md | DOOM VFR review | https://roadtovr.com/doom-vfr-review/ | 2026-09-25 | (c) Road to VR, reference |
| reference/prior-art/docs/psu-doom3-vr-edition-interview.md | DOOM 3: VR Edition interview (diegetic UI, wrist watch) | https://www.psu.com/news/doom-3-vr-edition-interview-remote-working-history-with-prey-vr-psvr-optimisations-more/ | 2026-09-25 | (c) PSU, reference |
| reference/prior-art/docs/hl2vr-faq.md | Half-Life 2: VR Mod FAQ | https://halflife2vr.com/faq/ | 2026-09-25 | (c) Source VR Mod Team, reference |
| reference/prior-art/docs/reframework-readme.md | REFramework README | https://github.com/praydog/REFramework | 2026-09-25 | MIT |
| reference/prior-art/docs/openxr-toolkit-readme.md | OpenXR Toolkit README (stub pointing to site) | https://github.com/mbucchia/OpenXR-Toolkit | 2026-09-25 | MIT |
| tools/references/prior-art.sh | Idempotent fetch script for everything above | -- | 2026-09-25 | MIT (ours) |

## tooling

| Path | What | Source URL | Fetched | License |
|---|---|---|---|---|
| tooling/docs/gfxr-usage-desktop-vulkan.md | GFXReconstruct desktop Vulkan usage: capture layer env vars, frame-range trimming, hotkey trigger, replay options (screenshots, offscreen swapchain, dump-resources, memory translation) | https://raw.githubusercontent.com/LunarG/gfxreconstruct/dev/USAGE_desktop_Vulkan.md | 2026-09-25 | MIT (see gfxr-license.md) |
| tooling/docs/gfxr-usage-desktop-openxr.md | GFXReconstruct OpenXR capture notes | https://raw.githubusercontent.com/LunarG/gfxreconstruct/dev/USAGE_desktop_OpenXR.md | 2026-09-25 | MIT |
| tooling/docs/gfxr-license.md | GFXReconstruct license text | https://raw.githubusercontent.com/LunarG/gfxreconstruct/dev/LICENSE.md | 2026-09-25 | MIT |
| tooling/docs/vvl-khronos-validation-layer.md | Validation layer configuration: vk_layer_settings.txt, env var settings, message filters | https://raw.githubusercontent.com/KhronosGroup/Vulkan-ValidationLayers/main/docs/khronos_validation_layer.md | 2026-09-25 | Apache-2.0 |
| tooling/docs/vvl-gpu-validation.md | GPU-assisted validation (GPU-AV) | https://raw.githubusercontent.com/KhronosGroup/Vulkan-ValidationLayers/main/docs/gpu_validation.md | 2026-09-25 | Apache-2.0 |
| tooling/docs/vvl-syncval-usage.md | Synchronization validation usage and settings | https://raw.githubusercontent.com/KhronosGroup/Vulkan-ValidationLayers/main/docs/syncval_usage.md | 2026-09-25 | Apache-2.0 |
| tooling/docs/renderdoc_app.h | RenderDoc in-application API header (v1.x branch) | https://raw.githubusercontent.com/baldurk/renderdoc/v1.x/renderdoc/api/app/renderdoc_app.h | 2026-09-25 | MIT |
| tooling/docs/renderdoc-in-application-api.md | RenderDoc in-application API reference | https://renderdoc.org/docs/in_application_api.html | 2026-09-25 | RenderDoc docs, reference copy |
| tooling/docs/renderdoc-how-capture-frame.md | RenderDoc: capturing a frame | https://renderdoc.org/docs/how/how_capture_frame.html | 2026-09-25 | RenderDoc docs, reference copy |
| tooling/docs/renderdoc-capture-attach.md | RenderDoc: launch/attach and capture options (incl. env vars, hooking children) | https://renderdoc.org/docs/window/capture_attach.html | 2026-09-25 | RenderDoc docs, reference copy |
| tooling/docs/renderdoc-python-index.md | RenderDoc Python API index | https://renderdoc.org/docs/python_api/index.html | 2026-09-25 | RenderDoc docs, reference copy |
| tooling/docs/renderdoc-python-intro.md | RenderDoc Python: using the module outside the UI | https://renderdoc.org/docs/python_api/examples/renderdoc_intro.html | 2026-09-25 | RenderDoc docs, reference copy |
| tooling/docs/renderdoc-python-basics.md | RenderDoc Python basics | https://renderdoc.org/docs/python_api/examples/basics.html | 2026-09-25 | RenderDoc docs, reference copy |
| tooling/docs/renderdoc-python-save-texture.md | RenderDoc Python example: save a texture to disk | https://renderdoc.org/docs/python_api/examples/renderdoc/save_texture.html | 2026-09-25 | RenderDoc docs, reference copy |
| tooling/docs/nsight-graphics-capture-cli.md | Nsight Graphics command-line capture (ngfx) | https://docs.nvidia.com/nsight-graphics/UserGuide/graphics-capture-cli.html | 2026-09-25 | NVIDIA docs, reference copy |
| tooling/docs/openxr-simulator-readme.md | OpenXR-Simulator (desktop OpenXR runtime, Vulkan via D3D12 shared images, screenshots, settings.json) | https://raw.githubusercontent.com/elliotttate/OpenXR-Simulator/master/README.md | 2026-09-25 | MIT |
| tooling/docs/meta-xr-simulator-intro.md | Meta XR Simulator overview (standalone app, runtime toggle, VRS record/replay) | https://developers.meta.com/horizon/documentation/native/xrsim-intro/ | 2026-09-25 | Meta docs, reference copy |
| tooling/docs/meta-xr-simulator-getting-started.md | Meta XR Simulator getting started | https://developers.meta.com/horizon/documentation/unity/xrsim-getting-started/ | 2026-09-25 | Meta docs, reference copy |
| tooling/docs/steamvr-noheadset-readme.md | SteamVR null driver setup (requireHmd, forcedDriver, activateMultipleDrivers) | https://raw.githubusercontent.com/username223/SteamVRNoHeadset/master/README.md | 2026-09-25 | see upstream repo |
| tooling/docs/monado-readme.md | Monado OpenXR runtime README (platform status) | https://gitlab.freedesktop.org/monado/monado/-/raw/main/README.md | 2026-09-25 | BSL-1.0 |
| tooling/docs/openxr-layer-template-readme.md | mbucchia OpenXR API layer template | https://raw.githubusercontent.com/mbucchia/OpenXR-Layer-Template/main/README.md | 2026-09-25 | MIT |
| tooling/docs/wer-collecting-user-mode-dumps.md | WER LocalDumps registry configuration | https://learn.microsoft.com/en-us/windows/win32/wer/collecting-user-mode-dumps | 2026-09-25 | Microsoft docs (CC-BY-4.0 / MIT for code) |
| tooling/docs/sysinternals-procdump.md | ProcDump usage | https://learn.microsoft.com/en-us/sysinternals/downloads/procdump | 2026-09-25 | Microsoft docs, reference copy |
| tooling/docs/sysinternals-psexec.md | PsExec usage (-i session targeting) | https://learn.microsoft.com/en-us/sysinternals/downloads/psexec | 2026-09-25 | Microsoft docs, reference copy |
| tooling/docs/wsl-filesystems-interop.md | WSL file systems and Windows interop | https://learn.microsoft.com/en-us/windows/wsl/filesystems | 2026-09-25 | Microsoft docs (CC-BY-4.0) |
| tooling/docs/safetyhook-readme.md | safetyhook build/integration notes (FetchContent, Zydis, amalgamated builds) | https://raw.githubusercontent.com/cursey/safetyhook/main/README.md | 2026-09-25 | BSL-1.0 |
| tooling/docs/kananlib-readme.md | kananlib README (cmkr build, kananlib-cli) | https://raw.githubusercontent.com/cursey/kananlib/main/README.md | 2026-09-25 | BSL-1.0 |
| tooling/docs/msvc-asan.md | MSVC AddressSanitizer overview and limits | https://learn.microsoft.com/en-us/cpp/sanitizers/asan | 2026-09-25 | Microsoft docs (CC-BY-4.0) |
| tooling/docs/dotnet-single-file.md | .NET single-file deployment | https://learn.microsoft.com/en-us/dotnet/core/deploying/single-file/overview | 2026-09-25 | Microsoft docs (CC-BY-4.0) |
| tooling/docs/gha-windows-2025-runner-image.md | GitHub Actions windows-2025 image software list | https://raw.githubusercontent.com/actions/runner-images/main/images/windows/Windows2025-Readme.md | 2026-09-25 | MIT |
| tooling/docs/gha-billing.md | GitHub Actions billing (included minutes, per-minute rates, free for public repos and self-hosted) | https://docs.github.com/en/billing/concepts/product-billing/github-actions | 2026-09-25 | GitHub docs (CC-BY-4.0) |
| tooling/docs/gha-self-hosted-runners.md | Self-hosted runners overview | https://docs.github.com/en/actions/concepts/runners/self-hosted-runners | 2026-09-25 | GitHub docs (CC-BY-4.0) |
| tooling/docs/gha-secure-use.md | Actions security hardening (self-hosted runner risks, pinning) | https://raw.githubusercontent.com/github/docs/main/content/actions/reference/security/secure-use.md | 2026-09-25 | GitHub docs (CC-BY-4.0) |
| tooling/docs/signpath-foundation-terms.md | SignPath Foundation conditions for OSS projects | https://signpath.org/terms.html | 2026-09-25 | SignPath, reference copy |
| tooling/docs/signpath-foundation-home.md | SignPath Foundation overview | https://signpath.org/ | 2026-09-25 | SignPath, reference copy |
| tooling/docs/signpath-github-trusted-build.md | SignPath GitHub trusted build system (OSS: all jobs on GitHub-hosted runners) | https://docs.signpath.io/trusted-build-systems/github | 2026-09-25 | SignPath, reference copy |
| tooling/docs/artifact-signing-quickstart.md | Azure Artifact Signing (formerly Trusted Signing) quickstart incl. eligibility | https://raw.githubusercontent.com/MicrosoftDocs/azure-docs/main/articles/artifact-signing/quickstart.md | 2026-09-25 | CC-BY-4.0 |
| tooling/docs/artifact-signing-integrations.md | Artifact Signing integrations (SignTool dlib, GitHub Action) | https://raw.githubusercontent.com/MicrosoftDocs/azure-docs/main/articles/artifact-signing/how-to-signing-integrations.md | 2026-09-25 | CC-BY-4.0 |
| tooling/docs/artifact-signing-faq.yml.md | Artifact Signing FAQ (source YAML) | https://raw.githubusercontent.com/MicrosoftDocs/azure-docs/main/articles/artifact-signing/faq.yml | 2026-09-25 | CC-BY-4.0 |
| tooling/docs/smartscreen-reputation.md | SmartScreen reputation for app developers | https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation | 2026-09-25 | Microsoft docs (CC-BY-4.0) |
| tooling/docs/defender-submission-guide.md | Submitting files to Microsoft for analysis | https://learn.microsoft.com/en-us/defender-xdr/submission-guide | 2026-09-25 | Microsoft docs (CC-BY-4.0) |
| tooling/docs/defender-false-positives.md | Defender false positive handling | https://learn.microsoft.com/en-us/defender-endpoint/defender-endpoint-false-positives-negatives | 2026-09-25 | Microsoft docs (CC-BY-4.0) |

## upscaling

| Path | What | Source URL | Fetched | License |
|---|---|---|---|---|
| tools/references/upscaling.sh | Idempotent fetch script for everything below | -- | 2026-09-25 | MIT (ours) |
| reference/upscaling/doom-eternal-dlss-integration-notes.md | What is known about DOOM Eternal's DLSS (version, CVARs incl. `r_dlssForceReset`, unknowns) | compiled; CVARs from https://github.com/Official-KEX/doom-eternal-full-cvarlist | 2026-09-25 | notes (ours) |
| reference/upscaling/ngx-vulkan-api-pointers.md | Where the NGX Vulkan entry points, resource wrapper and param keys live; per-layer selection in NGX/XeSS/FSR | compiled from NVIDIA/DLSS headers, XeSS guide, FidelityFX VK backend | 2026-09-25 | notes (ours) |
| reference/upscaling/docs/DLSS-Programming-Guide-310.6-excerpts.md | Excerpts: revision history, execution times/VRAM, dynres caveats, MVs + jitter, presets, scene transitions, multi-view/VR (3.17), Vulkan wrapper | https://github.com/NVIDIA/DLSS/blob/main/doc/DLSS_Programming_Guide_Release.pdf | 2026-09-25 | NVIDIA proprietary (excerpt, private research copy) |
| reference/upscaling/docs/DLSS-README.md | DLSS SDK README | https://github.com/NVIDIA/DLSS | 2026-09-25 | NVIDIA RTX SDKs license |
| reference/upscaling/docs/DLSS-LICENSE.md | NVIDIA RTX SDKs license + RTX supplement (v. 2024-03-14): redistribution, attribution, notification, NVIDIA-GPU-only terms | https://github.com/NVIDIA/DLSS/blob/main/LICENSE.txt | 2026-09-25 | license text |
| reference/upscaling/docs/Streamline-README.md | Streamline 2.14.1 README | https://github.com/NVIDIA-RTX/Streamline | 2026-09-25 | MIT |
| reference/upscaling/docs/Streamline-ProgrammingGuideDLSS.md | Streamline DLSS guide (section 8.0: multiple viewports) | https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS.md | 2026-09-25 | MIT |
| reference/upscaling/docs/Streamline-changelog-2.14-excerpt.md | SL 2.14.0 changelog: `sl.dlss_nr` (DLSS 5), Vulkan Reflex | https://github.com/NVIDIA-RTX/Streamline/blob/main/changelog.txt | 2026-09-25 | MIT |
| reference/upscaling/docs/FSR2-README.md | FSR 2.2 integration guide (Vulkan + DX12) | https://github.com/GPUOpen-Effects/FidelityFX-FSR2 | 2026-09-25 | MIT |
| reference/upscaling/docs/FidelityFX-SDK-1.1.4-README.md | FidelityFX SDK 1.1.4 README (last Vulkan-capable SDK; FSR 3.1.4) | https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/tree/v1.1.4 | 2026-09-25 | MIT |
| reference/upscaling/docs/FSR3.1-upscaler-technique.md | FSR 3.1 upscaler technique/integration doc | https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/v1.1.4/docs/techniques/super-resolution-upscaler.md | 2026-09-25 | MIT |
| reference/upscaling/docs/FSR-SDK-2.3-README.md | AMD FSR SDK 2.3.0 "Redstone" README (FSR 4.1.1; "Vulkan is currently not supported") | https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK | 2026-09-25 | samples MIT; binaries AMD license |
| reference/upscaling/docs/FSR-SDK-2.3-whats-new.md | SDK 2.3.0 what's new (FSR 4.1 on RDNA 3) | same repo, `Kits/FidelityFX/docs/whats-new/index.md` | 2026-09-25 | MIT |
| reference/upscaling/docs/FSR-SDK-2.3-license.md | AMD FSR SDK license (binary redistribution terms) | same repo, `docs/license.md` | 2026-09-25 | license text |
| reference/upscaling/docs/XeSS-README.md | XeSS 3 SDK README | https://github.com/intel/xess | 2026-09-25 | Intel Simplified Software License |
| reference/upscaling/docs/XeSS-LICENSE.md | XeSS SDK license | https://github.com/intel/xess/blob/main/LICENSE.txt | 2026-09-25 | license text |
| reference/upscaling/docs/XeSS-SR-developer-guide.md | XeSS-SR developer guide (Vulkan per-layer image views, reset) | https://github.com/intel/xess/blob/main/doc/xess_sr_developer_guide_english.md | 2026-09-25 | Intel documentation |
| reference/upscaling/docs/DLSSTweaks-README.md | DLSSTweaks README + full `dlsstweaks.ini` (preset/DLAA/DLL overrides, loading methods) | https://github.com/emoose/DLSSTweaks | 2026-09-25 | MIT |
| reference/upscaling/docs/OptiScaler-README.md | OptiScaler README (DLSS<->FSR/XeSS translation, Vulkan backends, FSR4 via DX12 bridge) | https://github.com/optiscaler/OptiScaler | 2026-09-25 | GPL-3.0 (README only) |
| reference/upscaling/docs/DLSS-Swapper-README.md | DLSS Swapper README | https://github.com/beeradmoore/dlss-swapper | 2026-09-25 | GPL-3.0 (README only) |
| reference/upscaling/articles/nvidia-dlss5-neural-rendering.md | DLSS 5 notes: what it is, leak (2026-08-26) and release (2026-09-03), cost, VR relevance | NVIDIA newsroom/GeForce news, Club386, HotHardware, Held Games, XDA, OC3D, TechPowerUp, CompoundVR (URLs inside) | 2026-09-25 | notes (ours) |
| reference/upscaling/articles/nvidia-dlss45-presets.md | DLSS 4/4.5 preset notes (J/K/L/M), NVIDIA App overrides, RTX 4080 costs | NVIDIA GeForce news + DLSS guide (URLs inside) | 2026-09-25 | notes (ours) |
| reference/upscaling/articles/amd-fsr4-redstone-status.md | FSR 2/3.1/4.x status, RDNA 2/3/4 support, Vulkan gap, INT8 leak | GPUOpen, HotHardware, Tom's Hardware, Wccftech, VideoCardz, PC Gamer (URLs inside) | 2026-09-25 | notes (ours) |
| reference/upscaling/articles/vr-upscaling-precedents.md | Table of VR upscaling precedents and reported artifacts | various (URLs inside) | 2026-09-25 | notes (ours) |
| reference/_cache/DLSS/ | NVIDIA DLSS SDK (commit 3749594): headers, guides; sparse on fresh fetch (existing full clone also holds 310.9.1 DLLs -- do not redistribute) | https://github.com/NVIDIA/DLSS | 2026-09-25 | NVIDIA RTX SDKs license |
| reference/_cache/upscaling/DLSS_Programming_Guide_Release.pdf (+ .txt) | DLSS SR Programming Guide 310.6.0 (March 2026) and text extraction | https://github.com/NVIDIA/DLSS/blob/main/doc/DLSS_Programming_Guide_Release.pdf | 2026-09-25 | NVIDIA proprietary |
| reference/_cache/upscaling/DLSS-RR_Integration_Guide.pdf | DLSS Ray Reconstruction guide (n/a for us, kept for completeness) | https://github.com/NVIDIA/DLSS/tree/main/doc | 2026-09-25 | NVIDIA proprietary |
| reference/_cache/Streamline/ | Streamline SDK source + docs (2122257) | https://github.com/NVIDIA-RTX/Streamline | 2026-09-25 | MIT (NGX headers inside are NVIDIA proprietary) |
| reference/_cache/FidelityFX-FSR2/ | FSR 2.2 source (1680d1e), `src/ffx-fsr2-api/vk` backend | https://github.com/GPUOpen-Effects/FidelityFX-FSR2 | 2026-09-25 | MIT |
| reference/_cache/FidelityFX-SDK/ | AMD FSR SDK 2.3.0 (60f4ea8) | https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK | 2026-09-25 | samples MIT; runtime AMD license |
| reference/_cache/FidelityFX-SDK-v1.1.4/ | FidelityFX SDK 1.1.4 (c6efa6b): FSR 3.1.4 source with Vulkan backend (`sdk/src/backends/vk`) | https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/tree/v1.1.4 | 2026-09-25 | MIT |
| reference/_cache/xess/ | Intel XeSS 3 SDK (de0fb9c): headers, docs, binaries | https://github.com/intel/xess | 2026-09-25 | Intel Simplified Software License |
| reference/_cache/DLSSTweaks/ | DLSSTweaks source (1d2fddb; repo frozen at 0.200.8.0) | https://github.com/emoose/DLSSTweaks | 2026-09-25 | MIT |
| reference/_cache/dlss-swapper/ | DLSS Swapper source (ab9b1e2) | https://github.com/beeradmoore/dlss-swapper | 2026-09-25 | GPL-3.0 (study only) |
| reference/_cache/OptiScaler/ | OptiScaler source (1875b8a) | https://github.com/optiscaler/OptiScaler | 2026-09-25 | GPL-3.0 (study only; no code copying into MIT tree) |

## vulkan

| Path | What | Source URL | Fetched | License |
|---|---|---|---|---|
| vulkan/loader/LoaderLayerInterface.md | Loader-layer interface: discovery (registry keys), filtering env vars, negotiation, dispatch, manifest format, well-behaved layer rules | https://raw.githubusercontent.com/KhronosGroup/Vulkan-Loader/main/docs/LoaderLayerInterface.md | 2026-09-25 | Apache-2.0 |
| vulkan/loader/LoaderInterfaceArchitecture.md | Loader architecture, dispatch tables and call chains, env var table | https://raw.githubusercontent.com/KhronosGroup/Vulkan-Loader/main/docs/LoaderInterfaceArchitecture.md | 2026-09-25 | Apache-2.0 |
| vulkan/loader/LoaderApplicationInterface.md | Implicit vs explicit layers, overall layer ordering | https://raw.githubusercontent.com/KhronosGroup/Vulkan-Loader/main/docs/LoaderApplicationInterface.md | 2026-09-25 | Apache-2.0 |
| vulkan/loader/LoaderDebugging.md | VK_LOADER_DEBUG and layer diagnosis | https://raw.githubusercontent.com/KhronosGroup/Vulkan-Loader/main/docs/LoaderDebugging.md | 2026-09-25 | Apache-2.0 |
| vulkan/spec/appendix-VK_KHR_multiview.adoc | Extension appendix | https://raw.githubusercontent.com/KhronosGroup/Vulkan-Docs/main/appendices/VK_KHR_multiview.adoc | 2026-09-25 | CC-BY-4.0 (Vulkan-Docs) |
| vulkan/spec/appendix-VK_KHR_dynamic_rendering.adoc | Extension appendix | https://raw.githubusercontent.com/KhronosGroup/Vulkan-Docs/main/appendices/VK_KHR_dynamic_rendering.adoc | 2026-09-25 | CC-BY-4.0 |
| vulkan/spec/appendix-VK_KHR_dynamic_rendering_local_read.adoc | Extension appendix | https://raw.githubusercontent.com/KhronosGroup/Vulkan-Docs/main/appendices/VK_KHR_dynamic_rendering_local_read.adoc | 2026-09-25 | CC-BY-4.0 |
| vulkan/spec/appendix-VK_EXT_descriptor_indexing.adoc | Extension appendix | https://raw.githubusercontent.com/KhronosGroup/Vulkan-Docs/main/appendices/VK_EXT_descriptor_indexing.adoc | 2026-09-25 | CC-BY-4.0 |
| vulkan/spec/appendix-VK_KHR_fragment_shading_rate.adoc | Extension appendix | https://raw.githubusercontent.com/KhronosGroup/Vulkan-Docs/main/appendices/VK_KHR_fragment_shading_rate.adoc | 2026-09-25 | CC-BY-4.0 |
| vulkan/spec/appendix-VK_EXT_shader_viewport_index_layer.adoc | Extension appendix | https://raw.githubusercontent.com/KhronosGroup/Vulkan-Docs/main/appendices/VK_EXT_shader_viewport_index_layer.adoc | 2026-09-25 | CC-BY-4.0 |
| vulkan/spec/appendix-VK_KHR_create_renderpass2.adoc | Extension appendix | https://raw.githubusercontent.com/KhronosGroup/Vulkan-Docs/main/appendices/VK_KHR_create_renderpass2.adoc | 2026-09-25 | CC-BY-4.0 |
| vulkan/spec/appendix-VK_KHR_timeline_semaphore.adoc | Extension appendix | https://raw.githubusercontent.com/KhronosGroup/Vulkan-Docs/main/appendices/VK_KHR_timeline_semaphore.adoc | 2026-09-25 | CC-BY-4.0 |
| vulkan/spec/appendix-VK_KHR_synchronization2.adoc | Extension appendix | https://raw.githubusercontent.com/KhronosGroup/Vulkan-Docs/main/appendices/VK_KHR_synchronization2.adoc | 2026-09-25 | CC-BY-4.0 |
| vulkan/spec/appendix-VK_EXT_graphics_pipeline_library.adoc | Extension appendix | https://raw.githubusercontent.com/KhronosGroup/Vulkan-Docs/main/appendices/VK_EXT_graphics_pipeline_library.adoc | 2026-09-25 | CC-BY-4.0 |
| vulkan/spec/appendix-VK_EXT_shader_module_identifier.adoc | Extension appendix | https://raw.githubusercontent.com/KhronosGroup/Vulkan-Docs/main/appendices/VK_EXT_shader_module_identifier.adoc | 2026-09-25 | CC-BY-4.0 |
| vulkan/spec/appendix-VK_KHR_maintenance5.adoc | Extension appendix (inline SPIR-V in pipeline create info, relevant to shader interception) | https://raw.githubusercontent.com/KhronosGroup/Vulkan-Docs/main/appendices/VK_KHR_maintenance5.adoc | 2026-09-25 | CC-BY-4.0 |
| vulkan/spec/appendix-VK_EXT_shader_object.adoc | Extension appendix | https://raw.githubusercontent.com/KhronosGroup/Vulkan-Docs/main/appendices/VK_EXT_shader_object.adoc | 2026-09-25 | CC-BY-4.0 |
| vulkan/spec/proposal-VK_KHR_dynamic_rendering.adoc | Design proposal | https://raw.githubusercontent.com/KhronosGroup/Vulkan-Docs/main/proposals/VK_KHR_dynamic_rendering.adoc | 2026-09-25 | CC-BY-4.0 |
| vulkan/spec/proposal-VK_KHR_dynamic_rendering_local_read.adoc | Design proposal | https://raw.githubusercontent.com/KhronosGroup/Vulkan-Docs/main/proposals/VK_KHR_dynamic_rendering_local_read.adoc | 2026-09-25 | CC-BY-4.0 |
| vulkan/spec/proposal-VK_EXT_graphics_pipeline_library.adoc | Design proposal | https://raw.githubusercontent.com/KhronosGroup/Vulkan-Docs/main/proposals/VK_EXT_graphics_pipeline_library.adoc | 2026-09-25 | CC-BY-4.0 |
| vulkan/spec/proposal-VK_EXT_shader_module_identifier.adoc | Design proposal | https://raw.githubusercontent.com/KhronosGroup/Vulkan-Docs/main/proposals/VK_EXT_shader_module_identifier.adoc | 2026-09-25 | CC-BY-4.0 |
| vulkan/spec/proposal-VK_KHR_fragment_shading_rate.adoc | Design proposal | https://raw.githubusercontent.com/KhronosGroup/Vulkan-Docs/main/proposals/VK_KHR_fragment_shading_rate.adoc | 2026-09-25 | CC-BY-4.0 |
| vulkan/spec/proposal-VK_EXT_shader_object.adoc | Design proposal | https://raw.githubusercontent.com/KhronosGroup/Vulkan-Docs/main/proposals/VK_EXT_shader_object.adoc | 2026-09-25 | CC-BY-4.0 |
| vulkan/spec/multiview-and-layers-excerpts.md | Curated spec quotes: multiview limits, state reset, queries, layer clamp, FSR per view, dynamic rendering VUIDs | https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html | 2026-09-25 | CC-BY-4.0 (quotes) + project notes |
| spirv/spirv-stereo-excerpts.md | Curated SPIR-V facts for patching (enumerants, interface rules, arrayed coordinates, useful spirv-opt passes) | https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html | 2026-09-25 | CC-BY-4.0 (quotes) + project notes |
| spirv/SPV_KHR_multiview.asciidoc | SPIR-V extension spec | https://raw.githubusercontent.com/KhronosGroup/SPIRV-Registry/main/extensions/KHR/SPV_KHR_multiview.asciidoc | 2026-09-25 | Khronos registry (CC-BY-4.0) |
| spirv/SPV_EXT_shader_viewport_index_layer.asciidoc | SPIR-V extension spec | https://raw.githubusercontent.com/KhronosGroup/SPIRV-Registry/main/extensions/EXT/SPV_EXT_shader_viewport_index_layer.asciidoc | 2026-09-25 | Khronos registry (CC-BY-4.0) |
| spirv/SPV_KHR_fragment_shading_rate.asciidoc | SPIR-V extension spec | https://raw.githubusercontent.com/KhronosGroup/SPIRV-Registry/main/extensions/KHR/SPV_KHR_fragment_shading_rate.asciidoc | 2026-09-25 | Khronos registry (CC-BY-4.0) |
| spirv/GL_EXT_multiview.txt | GLSL extension spec (gl_ViewIndex) | https://raw.githubusercontent.com/KhronosGroup/GLSL/main/extensions/ext/GL_EXT_multiview.txt | 2026-09-25 | Khronos (see repo) |
| spirv/README-SPIRV-Cross.md | README | https://raw.githubusercontent.com/KhronosGroup/SPIRV-Cross/main/README.md | 2026-09-25 | Apache-2.0 |
| spirv/README-glslang.md | README | https://raw.githubusercontent.com/KhronosGroup/glslang/main/README.md | 2026-09-25 | BSD-3-Clause and others (glslang LICENSE.txt) |
| spirv/README-SPIRV-Tools.md | README (includes optimizer pass overview) | https://raw.githubusercontent.com/KhronosGroup/SPIRV-Tools/main/README.md | 2026-09-25 | Apache-2.0 |
| spirv/README-SPIRV-Reflect.md | README | https://raw.githubusercontent.com/KhronosGroup/SPIRV-Reflect/main/README.md | 2026-09-25 | Apache-2.0 |
| spirv/README-SPIRV-Headers.md | README | https://raw.githubusercontent.com/KhronosGroup/SPIRV-Headers/main/README.md | 2026-09-25 | MIT-style (Khronos) |
| spirv/README-MinHook.md | README | https://raw.githubusercontent.com/TsudaKageyu/minhook/master/README.md | 2026-09-25 | BSD-2-Clause |
| vulkan/articles/VVL-gpu_av_shader_instrumentation.md | How validation layers instrument SPIR-V at runtime and link precompiled GLSL helpers | https://raw.githubusercontent.com/KhronosGroup/Vulkan-ValidationLayers/main/docs/gpu_av_shader_instrumentation.md | 2026-09-25 | Apache-2.0 |
| vulkan/articles/VVL-gpu_validation.md | GPU-AV requirements (reserved descriptor-set slot, timeline semaphores) | https://raw.githubusercontent.com/KhronosGroup/Vulkan-ValidationLayers/main/docs/gpu_validation.md | 2026-09-25 | Apache-2.0 |
| vulkan/articles/VVL-gpuav-spirv-passes-README.md | GPU-AV pass structure | https://raw.githubusercontent.com/KhronosGroup/Vulkan-ValidationLayers/main/layers/gpuav/spirv/README.md | 2026-09-25 | Apache-2.0 |
| vulkan/articles/README-Fossilize.md | Fossilize pipeline capture/replay layer (Steam shader pre-caching) | https://raw.githubusercontent.com/ValveSoftware/Fossilize/master/README.md | 2026-09-25 | MIT |
| vulkan/articles/README-vkBasalt.md | vkBasalt post-processing layer | https://raw.githubusercontent.com/DadSchoorse/vkBasalt/master/README.md | 2026-09-25 | zlib |
| vulkan/articles/README-Vk3DVision-Public.md | Vk3DVision release repo README | https://raw.githubusercontent.com/helifax/Vk3DVision-Public/main/README.md | 2026-09-25 | BSD-3-Clause |
| vulkan/articles/vk3dvision-3dsurroundgaming.md | Vk3DVision project page | https://3dsurroundgaming.com/Vk3DVision.html | 2026-09-25 | Copyright Helifax; private research copy |
| vulkan/articles/bad-vulkan-layers-mattstevens.md | Table of commonly crashing implicit layers and their disable env vars; notes DOOM Eternal's layer dialog | https://www.mattstevens.co.uk/posts/bad-vulkan-layers/ | 2026-09-25 | Copyright author; private research copy |
| vulkan/articles/nvidia-turing-multi-view-rendering.md | NVIDIA Turing MVR / single pass stereo | https://developer.nvidia.com/blog/turing-multi-view-rendering-vrworks/ | 2026-09-25 | Copyright NVIDIA; private research copy |
| vulkan/articles/unity-single-pass-instanced.md | Unity single-pass instanced stereo | https://docs.unity3d.com/Manual/SinglePassInstancing.html | 2026-09-25 | Copyright Unity; private research copy |
| vulkan/articles/meta-mobile-multiview.md | Meta/Oculus multiview guidance | https://developer.oculus.com/documentation/native/android/mobile-multiview/ | 2026-09-25 | Copyright Meta; private research copy |
| vulkan/articles/uevr-overview.md | UEVR rendering methods (native stereo, synced sequential, AFR) | http://docs.uevr.io/usage/overview.html | 2026-09-25 | Copyright UEVR authors; private research copy |
| vulkan/articles/doom-eternal-siggraph2020-notes.md | Our notes on the id Tech 7 renderer talk and what each technique means for stereo | https://advances.realtimerendering.com/s2020/RenderingDoomEternal.pdf | 2026-09-25 | Project notes (MIT); talk copyright id Software |
| tools/references/vulkan.sh | Idempotent fetch script for all of the above and the cache | (this repo) | 2026-09-25 | MIT |
| Path | What | Source URL | Fetched | License |
|---|---|---|---|---|
| _cache/vkspec.html | Full Vulkan specification (single-page HTML, ~40 MB) | https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html | 2026-09-25 | CC-BY-4.0 |
| _cache/SPIRV.html | Full SPIR-V unified specification | https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html | 2026-09-25 | CC-BY-4.0 |
| _cache/pdf/RenderingDoomEternal-SIGGRAPH2020.pdf | id Tech 7 renderer talk | https://advances.realtimerendering.com/s2020/RenderingDoomEternal.pdf | 2026-09-25 | Copyright id Software; do not redistribute |
| _cache/pdf/Vlachos-Advanced-VR-Rendering-GDC2015.pdf | Valve: instanced stereo, hidden area mesh, stereo culling | https://media.steampowered.com/apps/valve/2015/Alex_Vlachos_Advanced_VR_Rendering_GDC2015.pdf | 2026-09-25 | Copyright Valve |
| _cache/pdf/Vlachos-Advanced-VR-Rendering-Performance-GDC2016.pdf | Valve: adaptive quality, fixed foveation | https://media.steampowered.com/apps/valve/2016/Alex_Vlachos_Advanced_VR_Rendering_Performance_GDC2016.pdf | 2026-09-25 | Copyright Valve |
| _cache/pdf/LunarG-Loader-and-Layers-Diagnosing-Layer-Issues.pdf | LunarG loader/layer troubleshooting | https://www.lunarg.com/wp-content/uploads/2022/12/The-Vulkan-Loader-and-Vulkan-Layers_-Diagnosing-Layer-Issues.pdf | 2026-09-25 | Copyright LunarG |
| _cache/pdf/ARM-Multiview-SIGGRAPH2016.pdf | ARM multiview talk | https://community.arm.com/cfs-file/__key/communityserver-blogs-components-weblogfiles/00-00-00-20-66/5_2D00_mmg_2D00_siggraph2016_2D00_multiview_2D00_cass.pdf | 2026-09-25 | Copyright ARM |
| _cache/SPIRV-Cross/ | Source (shallow clone) | https://github.com/KhronosGroup/SPIRV-Cross | 2026-09-25 | Apache-2.0 |
| _cache/glslang/ | Source | https://github.com/KhronosGroup/glslang | 2026-09-25 | BSD-3-Clause and others |
| _cache/SPIRV-Tools/ | Source | https://github.com/KhronosGroup/SPIRV-Tools | 2026-09-25 | Apache-2.0 |
| _cache/SPIRV-Headers/ | Source | https://github.com/KhronosGroup/SPIRV-Headers | 2026-09-25 | MIT-style (Khronos) |
| _cache/SPIRV-Reflect/ | Source | https://github.com/KhronosGroup/SPIRV-Reflect | 2026-09-25 | Apache-2.0 |
| _cache/minhook/ | Source | https://github.com/TsudaKageyu/minhook | 2026-09-25 | BSD-2-Clause |
| _cache/Vulkan-Headers/ | Source (vk_layer.h, registry) | https://github.com/KhronosGroup/Vulkan-Headers | 2026-09-25 | Apache-2.0 / MIT |
| _cache/Vulkan-Utility-Libraries/ | `vk_dispatch_table.h`, safe structs for pNext deep copies | https://github.com/KhronosGroup/Vulkan-Utility-Libraries | 2026-09-25 | Apache-2.0 |
| _cache/Vulkan-Guide/ | Khronos Vulkan Guide (multiview, descriptor indexing, dynamic rendering chapters) | https://github.com/KhronosGroup/Vulkan-Guide | 2026-09-25 | CC-BY-4.0 |
| _cache/vkBasalt/ | Layer design reference (dispatch maps, present interception) | https://github.com/DadSchoorse/vkBasalt | 2026-09-25 | zlib |
| _cache/Fossilize/ | Layer design reference (pipeline state capture) | https://github.com/ValveSoftware/Fossilize | 2026-09-25 | MIT |
| _cache/Vk3DVision-Public/ | Release-only repo (no source) | https://github.com/helifax/Vk3DVision-Public | 2026-09-25 | BSD-3-Clause |

## idtech7/vr-subsystem

| Path | What | Source URL | Fetched | License |
|---|---|---|---|---|
| reference/idtech7/vr-subsystem/bfg-stereo-source-excerpts.md | Verbatim BFG stereo excerpts (cvar table, HMD vs 3D-TV separation, per-eye view, GUI stereo offset, backend dispatch) with line numbers | https://github.com/id-Software/DOOM-3-BFG (1caba19) | 2026-09-25 | GPL-3.0 + id additional terms (notice kept in file) |
| reference/idtech7/vr-subsystem/eternal-vr-names-evidence.md | Every VR-related cvar, RTTI class, key enum value and reflected name visible in DOOM Eternal dumps, plus Great Circle and Dishonored 2 comparisons | Official-KEX/doom-eternal-full-cvarlist; Lyall/GreatCircleFix (1ce0a86) cvardump.txt; FishnCrisps/m3337ho0o0ok (10dee47) declare_vtbl_feature_vars.hpp; brongo/m3337ho0o0ok mh_inputsys.hpp; Decimation/EternalAdvanced (eb52836) id.h; LordRadai/Dishonored2-Debug (ed1a72b) cvars.md | 2026-09-25 | ours (MIT); cited names are game-derived metadata |
| reference/idtech7/vr-subsystem/doom-vfr-facts.md | DOOM VFR platform/SDK/input/comfort facts and open questions | https://www.pcgamingwiki.com/wiki/Doom_VFR ; Steam news API app 650000 ; https://en.wikipedia.org/wiki/Doom_(2016_video_game) ; https://github.com/omarehaly/DOOMVFL | 2026-09-25 | ours (MIT); paraphrase of cited pages |
| reference/idtech7/vr-subsystem/doom2016-stereo-lineage.md | Summary of DOOM 2016 stereo RE: second-view design (worldViews/screenViews/viewIndex/guiOriginOffset), cvar registration, imports | https://github.com/TefMeister/doom-2016-vr (08cc186, no license: summary only) | 2026-09-25 | ours (MIT); summary with links |

