#!/usr/bin/env bash
# Idempotent fetch of upscaling reference material (research topic 07: DLSS / FSR / XeSS in VR).
#
# Git sources are shallow clones into reference/_cache/<name>/ (gitignored). Existing clones are
# updated with a fast-forward pull; nothing is deleted. Large single documents go to
# reference/_cache/upscaling/.
#
# Size note: a full NVIDIA/DLSS checkout is ~1.5 GB because the repo carries the runtime DLLs
# (nvngx_dlss*.dll) and static libs. We clone it sparse (doc/, include/, utils/ and root files) since
# we only need headers, the programming guide and the license. The DLLs are NVIDIA-licensed and must
# not be committed or redistributed from here.
#
# The small committed markdown copies under reference/upscaling/docs/ were generated from these caches;
# re-run with REFRESH_COPIES=1 to regenerate them.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
CACHE="$HERE/../_cache"
mkdir -p "$CACHE/upscaling"

clone() { # name url [branch]
  local name="$1" url="$2" branch="${3:-}" dst="$CACHE/$1"
  if [ -d "$dst/.git" ]; then
    if [ -n "$branch" ]; then echo "[have] $name ($branch)"; return; fi
    echo "[update] $name"; git -C "$dst" pull --ff-only --depth 1 -q || echo "  (pull failed, keeping existing)"
  else
    echo "[clone] $name ${branch:+($branch)}"
    GIT_LFS_SKIP_SMUDGE=1 git clone -q --depth 1 ${branch:+--branch "$branch"} "$url" "$dst" \
      || echo "  (clone failed: $url)"
  fi
}

clone_sparse() { # name url dir...
  local name="$1" url="$2"; shift 2; local dst="$CACHE/$name"
  if [ -d "$dst/.git" ]; then
    echo "[update] $name"; git -C "$dst" pull --ff-only --depth 1 -q || echo "  (pull failed, keeping existing)"
  else
    echo "[clone-sparse] $name ($*)"
    GIT_LFS_SKIP_SMUDGE=1 git clone -q --depth 1 --filter=blob:none --sparse "$url" "$dst" \
      && git -C "$dst" sparse-checkout set "$@" \
      || echo "  (clone failed: $url)"
  fi
}

download() { # url dest
  local url="$1" dst="$2"
  if [ -s "$dst" ]; then echo "[have] $(basename "$dst")"; return; fi
  echo "[get] $url"; curl -fsSL -A 'Mozilla/5.0' -o "$dst" "$url" || echo "  (download failed: $url)"
}

# NVIDIA: DLSS SDK (headers, programming guides, license) and Streamline (MIT framework + docs).
clone_sparse DLSS          https://github.com/NVIDIA/DLSS.git doc include utils
clone Streamline           https://github.com/NVIDIA-RTX/Streamline.git
# AMD: FSR2 standalone (MIT, Vulkan backend), FSR SDK 2.x "Redstone" (FSR 4.x, DX12 only),
# and FidelityFX SDK 1.1.4 (last release with a Vulkan backend; FSR 3.1.4, MIT).
clone FidelityFX-FSR2      https://github.com/GPUOpen-Effects/FidelityFX-FSR2.git
clone FidelityFX-SDK       https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK.git
clone FidelityFX-SDK-v1.1.4 https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK.git v1.1.4
# Intel XeSS 3 SDK (binary SDK, headers + docs).
clone xess                 https://github.com/intel/xess.git
# Community tools: preset overrides (MIT), DLL manager (GPL-3.0), upscaler translation layer (GPL-3.0).
clone DLSSTweaks           https://github.com/emoose/DLSSTweaks.git
clone dlss-swapper         https://github.com/beeradmoore/dlss-swapper.git
clone OptiScaler           https://github.com/optiscaler/OptiScaler.git

# Programming guides (also inside the DLSS clone; kept here so the PDF path is stable).
download https://raw.githubusercontent.com/NVIDIA/DLSS/main/doc/DLSS_Programming_Guide_Release.pdf \
         "$CACHE/upscaling/DLSS_Programming_Guide_Release.pdf"
download "https://raw.githubusercontent.com/NVIDIA/DLSS/main/doc/DLSS-RR%20Integration%20Guide.pdf" \
         "$CACHE/upscaling/DLSS-RR_Integration_Guide.pdf"
if command -v pdftotext >/dev/null 2>&1 && [ ! -s "$CACHE/upscaling/DLSS_Programming_Guide_Release.txt" ]; then
  pdftotext -layout "$CACHE/upscaling/DLSS_Programming_Guide_Release.pdf" \
            "$CACHE/upscaling/DLSS_Programming_Guide_Release.txt" || true
fi

if [ "${REFRESH_COPIES:-0}" = "1" ]; then
  O="$HERE/docs"; mkdir -p "$O"
  copy() { # dest title url license src
    local dst="$O/$1" title="$2" url="$3" lic="$4" src="$5"
    [ -f "$src" ] || { echo "  (missing $src)"; return; }
    { printf '# %s\n\n> Source: <%s>  \n> Fetched: %s. Verbatim copy of the upstream file unless noted. License: %s.\n\n---\n\n' \
        "$title" "$url" "$(date +%F)" "$lic"; cat "$src"; } > "$dst"; echo "[copy] $1"
  }
  copy DLSS-README.md "NVIDIA DLSS SDK README" https://github.com/NVIDIA/DLSS/blob/main/README.md "NVIDIA RTX SDKs license" "$CACHE/DLSS/README.md"
  copy DLSS-LICENSE.md "NVIDIA RTX SDKs license (DLSS / NGX)" https://github.com/NVIDIA/DLSS/blob/main/LICENSE.txt "license text" "$CACHE/DLSS/LICENSE.txt"
  copy Streamline-README.md "Streamline README" https://github.com/NVIDIA-RTX/Streamline/blob/main/README.md "MIT" "$CACHE/Streamline/README.md"
  copy Streamline-ProgrammingGuideDLSS.md "Streamline DLSS programming guide" https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS.md "MIT" "$CACHE/Streamline/docs/ProgrammingGuideDLSS.md"
  copy FSR2-README.md "FidelityFX FSR 2.2 README" https://github.com/GPUOpen-Effects/FidelityFX-FSR2/blob/master/README.md "MIT" "$CACHE/FidelityFX-FSR2/README.md"
  copy FSR-SDK-2.3-README.md "AMD FSR SDK README" https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/main/readme.md "samples MIT; runtime binaries AMD license" "$CACHE/FidelityFX-SDK/readme.md"
  copy FSR-SDK-2.3-license.md "AMD FSR SDK license" https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/main/docs/license.md "license text" "$CACHE/FidelityFX-SDK/docs/license.md"
  copy FSR-SDK-2.3-whats-new.md "AMD FSR SDK what's new" https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/main/Kits/FidelityFX/docs/whats-new/index.md "MIT" "$CACHE/FidelityFX-SDK/Kits/FidelityFX/docs/whats-new/index.md"
  copy FidelityFX-SDK-1.1.4-README.md "AMD FidelityFX SDK 1.1.4 README" https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/v1.1.4/Readme.md "MIT" "$CACHE/FidelityFX-SDK-v1.1.4/Readme.md"
  copy FSR3.1-upscaler-technique.md "FSR 3.1 upscaler technique doc (SDK 1.1.4)" https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/v1.1.4/docs/techniques/super-resolution-upscaler.md "MIT" "$CACHE/FidelityFX-SDK-v1.1.4/docs/techniques/super-resolution-upscaler.md"
  copy XeSS-README.md "Intel XeSS SDK README" https://github.com/intel/xess/blob/main/README.md "Intel Simplified Software License" "$CACHE/xess/README.md"
  copy XeSS-LICENSE.md "Intel XeSS SDK license" https://github.com/intel/xess/blob/main/LICENSE.txt "license text" "$CACHE/xess/LICENSE.txt"
  copy XeSS-SR-developer-guide.md "Intel XeSS-SR developer guide" https://github.com/intel/xess/blob/main/doc/xess_sr_developer_guide_english.md "Intel documentation" "$CACHE/xess/doc/xess_sr_developer_guide_english.md"
  copy DLSSTweaks-README.md "DLSSTweaks README" https://github.com/emoose/DLSSTweaks/blob/master/README.md "MIT" "$CACHE/DLSSTweaks/README.md"
  copy OptiScaler-README.md "OptiScaler README" https://github.com/optiscaler/OptiScaler/blob/master/README.md "GPL-3.0 (README only)" "$CACHE/OptiScaler/README.md"
  copy DLSS-Swapper-README.md "DLSS Swapper README" https://github.com/beeradmoore/dlss-swapper/blob/main/README.md "GPL-3.0 (README only)" "$CACHE/dlss-swapper/README.md"
  echo "Note: DLSS-Programming-Guide-310.6-excerpts.md, the Streamline changelog excerpt and the"
  echo "      DLSSTweaks ini appendix were cut by hand from the caches; regenerate manually if needed."
fi
echo done
