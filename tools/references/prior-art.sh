#!/usr/bin/env bash
# Idempotent fetch of prior-art source trees for topic 04 (flat-to-VR injectors, UI, robustness).
# Clones (shallow) into reference/_cache/<name>/ (gitignored). Re-running updates existing clones.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
CACHE="$HERE/../_cache"
mkdir -p "$CACHE"

clone() { # name url [sparse paths...]
  local name="$1" url="$2"; shift 2
  local dst="$CACHE/$name"
  if [ -d "$dst/.git" ]; then
    echo "[update] $name"; git -C "$dst" pull --ff-only --depth 1 -q || echo "  (pull failed, keeping existing)"
    return
  fi
  if [ "$#" -gt 0 ]; then
    echo "[sparse] $name"
    git clone -q --depth 1 --filter=blob:none --sparse "$url" "$dst"
    git -C "$dst" sparse-checkout set --no-cone "$@"
  else
    echo "[clone] $name"
    git clone -q --depth 1 "$url" "$dst"
  fi
}

clone uevr            https://github.com/praydog/UEVR.git
clone reframework     https://github.com/praydog/REFramework.git "/src/mods/vr/" "/src/mods/VR.cpp" "/src/mods/VR.hpp" "/README.md" "/LICENSE" "/scripts/"
clone safetyhook      https://github.com/cursey/safetyhook.git
clone libhat          https://github.com/BasedInc/libhat.git
clone hooking-patterns https://github.com/ThirteenAG/Hooking.Patterns.git
clone doom3bfg-vr     https://github.com/CarlKenner/DOOM-3-BFG-VR.git
clone kananlib         https://github.com/cursey/kananlib.git
clone minhook         https://github.com/TsudaKageyu/minhook.git
clone hl2vru          https://github.com/vittorioromeo/HL2VRU.git
clone hl2vr-d3d9      https://github.com/DrBeef/HL2VR_d3d9.git
clone vk3dvision      https://github.com/helifax/Vk3DVision-Public.git
clone 3dmigoto        https://github.com/bo3b/3Dmigoto.git "/README.md" "/DirectX11/" "/Dependencies/d3dx.ini" "/LICENSE.GPL.txt"
clone doom3quest      https://github.com/DrBeef/Doom3Quest.git "/README.md" "/app/src/main/jni/Doom3Quest/" "/app/src/main/jni/d3es-multithread-master/neo/game/Vr.cpp" "/app/src/main/jni/d3es-multithread-master/neo/game/Vr.h" "/app/src/main/jni/d3es-multithread-master/neo/renderer/GuiModel.cpp" "/app/src/main/jni/d3es-multithread-master/neo/renderer/tr_guisurf.cpp" "/LICENSE"
clone vrperfkit       https://github.com/fholger/vrperfkit.git
clone crysis-vrmod    https://github.com/fholger/crysis_vrmod.git "/README.md" "/LICENSE.txt" "/BUILDING.md" "/Code/"
clone thedarkmodvr    https://github.com/fholger/thedarkmodvr.git "/README*" "/LICENSE*" "/renderer/" "/game/gamesys/" "/framework/"
clone uevr-docs       https://github.com/praydog/uevr-docs.git

# --- Web articles -> markdown (needs pandoc). Skips files that already exist unless FORCE=1.
DOCS="$HERE/docs"; mkdir -p "$DOCS"
fetch_doc() { # outname url
  local out="$DOCS/$1.md" url="$2"
  if [ -s "$out" ] && [ "${FORCE:-0}" != 1 ]; then echo "[skip] $1"; return; fi
  echo "[doc] $1"
  local tmp; tmp="$(mktemp)"
  if curl -fsSL -A 'Mozilla/5.0' "$url" -o "$tmp"; then
    { printf -- '<!-- Source: %s -->\n<!-- Fetched: %s -->\n\n' "$url" "$(date +%Y-%m-%d)";
      pandoc -f html -t gfm-raw_html --wrap=none "$tmp" 2>/dev/null; } > "$out"
  else echo "  fetch failed: $url"; fi
  rm -f "$tmp"
}
fetch_raw() { # outname url   (for sources that are already markdown)
  local out="$DOCS/$1.md" url="$2"
  if [ -s "$out" ] && [ "${FORCE:-0}" != 1 ]; then echo "[skip] $1"; return; fi
  echo "[raw] $1"
  { printf -- '<!-- Source: %s -->\n<!-- Fetched: %s -->\n\n' "$url" "$(date +%Y-%m-%d)";
    curl -fsSL -A 'Mozilla/5.0' "$url"; } > "$out" || echo "  fetch failed: $url"
}
fetch_doc praydog-uevr-exploration       https://praydog.com/reverse-engineering/2023/07/03/uevr.html
fetch_doc aixxe-safetyhook-midhooks      https://aixxe.net/2022/12/safetyhook-midfn-hooking
fetch_raw lukeross-gta5-real-readme      https://raw.githubusercontent.com/LukeRoss00/gta5-real-mod/master/README.md
fetch_doc mixed-news-real-vr-aer         https://mixed-news.com/en/real-vr-mod-dlss-ray-reconstruction/
fetch_doc vorpx-support-faq              https://www.vorpx.com/support-faq/
fetch_doc vorpx-znormal-vs-zadaptive     https://www.vorpx.com/forums/topic/difference-between-z-normal-and-z-adaptive/
fetch_doc helixmod-geo11-announcement    https://helixmod.blogspot.com/2022/06/announcing-new-geo-11-3d-driver.html
fetch_doc vk3dvision-game-fixes          https://3dsurroundgaming.com/Vk3DVisionGames.html
fetch_doc roadtovr-doom-vfr-locomotion   https://www.roadtovr.com/doom-vfr-devs-detail-gameplay-setting-locomotion-new-video/
fetch_doc roadtovr-doom-vfr-review       https://roadtovr.com/doom-vfr-review/
fetch_doc psu-doom3-vr-edition-interview https://www.psu.com/news/doom-3-vr-edition-interview-remote-working-history-with-prey-vr-psvr-optimisations-more/
fetch_doc hl2vr-faq                      https://halflife2vr.com/faq/
fetch_raw reframework-readme             https://raw.githubusercontent.com/praydog/REFramework/master/README.md
fetch_raw openxr-toolkit-readme          https://raw.githubusercontent.com/mbucchia/OpenXR-Toolkit/main/README.md
echo "done. cache at $CACHE"

# Note: docs/vk3dvision-doom-eternal-vr-notes.md is hand-written from the Vk3DVision release archive
# (https://3dsurroundgaming.com/Vk3DVision/SFS_Releases/DOOM-Eternal-VR-0.90.7z). The archive itself is
# closed-source and is not mirrored here.
