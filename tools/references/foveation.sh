#!/usr/bin/env bash
# Idempotent fetch of foveated-rendering reference material (research topic 08).
# Git sources are shallow, blob-filtered clones into reference/_cache/<name>/ (gitignored).
# Spec appendices/proposals go to reference/foveation/spec/ (committed, CC-BY-4.0 / Apache-2.0).
# Committed README copies under reference/foveation/readmes/ come from the clones; re-run with
# REFRESH_COPIES=1 to refresh them. --update pulls existing clones. --articles re-saves any missing
# web articles under reference/foveation/articles/ (pandoc required).
#
# Already present elsewhere, not duplicated here:
#   reference/vulkan/spec/appendix-VK_KHR_fragment_shading_rate.adoc
#   reference/vulkan/spec/proposal-VK_KHR_fragment_shading_rate.adoc
#   reference/openxr/extensions/{ext_eye_gaze_interaction,fb_foveation*,meta_foveation_eye_tracked}.adoc
#   reference/_cache/Quad-Views-Foveated, reference/_cache/OpenXR-Toolkit (clones from topic 01/04)
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
CACHE="$HERE/../_cache"
UA='Mozilla/5.0'
UPDATE=0; for a in "$@"; do [ "$a" = "--update" ] && UPDATE=1; done
mkdir -p "$CACHE" "$HERE/spec" "$HERE/readmes" "$HERE/articles"

clone() { # name url
  local name="$1" url="$2" dst="$CACHE/$1"
  if [ -d "$dst/.git" ]; then
    if [ $UPDATE = 1 ]; then echo "[update] $name"; git -C "$dst" pull --ff-only --depth 1 -q || echo "  (pull failed, keeping existing)"; fi
  else
    echo "[clone] $name"
    GIT_LFS_SKIP_SMUDGE=1 git clone -q --depth 1 --filter=blob:limit=8m "$url" "$dst" \
      || echo "  (clone failed: $url)"
  fi
}

get() { # url dest
  local url="$1" dst="$2"
  if [ -s "$dst" ]; then return 0; fi
  echo "[get] $url"
  curl -fsSL -A "$UA" --retry 3 -o "$dst.part" "$url" && mv "$dst.part" "$dst" \
    || { echo "  (download failed: $url)"; rm -f "$dst.part"; }
}

# mbucchia foveation tooling
clone Quad-Views-Foveated   https://github.com/mbucchia/Quad-Views-Foveated.git
clone PimaxMagic4All        https://github.com/mbucchia/PimaxMagic4All.git
clone OpenXR-Eye-Trackers   https://github.com/mbucchia/OpenXR-Eye-Trackers.git
clone OpenXR-Toolkit        https://github.com/mbucchia/OpenXR-Toolkit.git
# vrperfkit family (FFR via NVIDIA VRS, foveated upscaling)
clone vrperfkit             https://github.com/fholger/vrperfkit.git
clone VRPerfKit_RSF         https://github.com/RavenSystem/VRPerfKit_RSF.git
clone vrperfkit-granther    https://github.com/Granther/foveated-rendering.git
# 2026 foveated DLSS / DLSS 5 (neural rendering) mods
clone CheekyFoveatedDLSS    https://github.com/ClarkCheekyKent/CheekyFoveatedDLSS.git
clone dlss5-vr              https://github.com/eregnier/dlss5-vr.git
# PSVR2 on PC (eye tracking source)
clone PSVR2Toolkit          https://github.com/BnuuySolutions/PSVR2Toolkit.git
# Vulkan samples showing attachment VRS / fragment density map
clone Vulkan-Samples-fsr    https://github.com/KhronosGroup/Vulkan-Samples.git
# Wikis with per-headset eye tracking setup (Steam Frame, Quest Pro, PSVR2, Beyond 2e, ...)
clone Quad-Views-Foveated.wiki  https://github.com/mbucchia/Quad-Views-Foveated.wiki.git
clone PimaxMagic4All.wiki       https://github.com/mbucchia/PimaxMagic4All.wiki.git
clone OpenXR-Eye-Trackers.wiki  https://github.com/mbucchia/OpenXR-Eye-Trackers.wiki.git

# Large documents
mkdir -p "$CACHE/foveation"
get https://research.nvidia.com/sites/default/files/pubs/2017-09_Latency-Requirements-for/a25-albert.pdf \
    "$CACHE/foveation/Albert2017-latency-foveated.pdf"

VD=https://raw.githubusercontent.com/KhronosGroup/Vulkan-Docs/main
get "$VD/appendices/VK_EXT_fragment_density_map.adoc"   "$HERE/spec/appendix-VK_EXT_fragment_density_map.adoc"
get "$VD/appendices/VK_EXT_fragment_density_map2.adoc"  "$HERE/spec/appendix-VK_EXT_fragment_density_map2.adoc"
get "$VD/appendices/VK_QCOM_fragment_density_map_offset.adoc" "$HERE/spec/appendix-VK_QCOM_fragment_density_map_offset.adoc"
get "$VD/appendices/VK_EXT_fragment_density_map_offset.adoc"  "$HERE/spec/appendix-VK_EXT_fragment_density_map_offset.adoc"
get "$VD/appendices/VK_NV_shading_rate_image.adoc"      "$HERE/spec/appendix-VK_NV_shading_rate_image.adoc"
get "$VD/chapters/primsrast.adoc"                       "$CACHE/foveation-primsrast.adoc"
get "$VD/proposals/VK_EXT_fragment_density_map_offset.adoc" "$HERE/spec/proposal-VK_EXT_fragment_density_map_offset.adoc"

OX=https://raw.githubusercontent.com/KhronosGroup/OpenXR-Docs/main/specification/sources/chapters/extensions
get "$OX/varjo/varjo_quad_views.adoc"          "$HERE/spec/openxr-varjo_quad_views.adoc"
get "$OX/varjo/varjo_foveated_rendering.adoc"  "$HERE/spec/openxr-varjo_foveated_rendering.adoc"

# OpenXR Toolkit user docs (gh-pages branch, MIT): foveated rendering and eye tracking pages.
for f in fr et; do
  dst="$HERE/articles/openxr-toolkit-$f.md"
  if [ ! -s "$dst" ]; then
    { printf -- "<!-- Source: https://github.com/mbucchia/OpenXR-Toolkit/blob/gh-pages/%s.md (rendered at https://mbucchia.github.io/OpenXR-Toolkit/%s.html). Fetched %s. License: MIT (OpenXR-Toolkit). -->\n\n" "$f" "$f" "$(date +%F)"
      curl -fsSL -A "$UA" "https://raw.githubusercontent.com/mbucchia/OpenXR-Toolkit/gh-pages/$f.md"; } > "$dst" || rm -f "$dst"
  fi
done

# Web articles, converted to markdown (needs pandoc). Only with --articles, and only if missing.
# The committed copies were produced this way and then stripped of inline images.
save_article() { # url out
  local url="$1" out="$HERE/articles/$2"
  [ -s "$out" ] && return 0
  command -v pandoc >/dev/null || { echo "  (pandoc missing, skip $url)"; return 0; }
  echo "[article] $url"
  curl -fsSL -A 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/126 Safari/537.36' "$url" \
    | pandoc -f html -t gfm-raw_html --wrap=none 2>/dev/null \
    | grep -v 'data:image' | sed -E 's/!\[[^]]*\]\([^)]*\)//g' \
    | { printf -- "<!-- Source: %s\n     Fetched: %s. Copyright its publisher; kept for reference only. -->\n\n" "$url" "$(date +%F)"; cat; } > "$out" \
    || echo "  (failed: $url)"
}
if [ "${1:-}" = "--articles" ] || [ "${2:-}" = "--articles" ]; then
  save_article https://developer.nvidia.com/blog/turing-variable-rate-shading-vrworks/ nvidia-turing-vrs-vrworks.md
  save_article https://www.nvidia.com/en-us/geforce/news/nvidia-adaptive-shading-a-deep-dive/ nvidia-adaptive-shading-deep-dive.md
  save_article https://gpuopen.com/fidelityfx-variable-shading/ amd-fidelityfx-variable-shading.md
  save_article https://developer.microsoft.com/en-us/games/articles/2026/04/variable-rate-compute-shaders-doom-the-dark-ages/ microsoft-vrcs-doom-the-dark-ages.md
  save_article https://store.bigscreenvr.com/blogs/beyond/dynamic-foveated-rendering-with-bigscreen-beyond-2e bigscreen-beyond-2e-dfr.md
  save_article https://www.uploadvr.com/pimaxmagic4all-adds-eye-tracking-to-many-steamvr-games/ uploadvr-pimaxmagic4all.md
  save_article https://www.uploadvr.com/microsoft-flight-simulator-2024-now-has-foveated-rendering/ uploadvr-msfs2024-foveated.md
  save_article https://www.heise.de/en/news/DLSS-5-Nvidia-is-finally-bringing-neural-rendering-to-RTX-40-cards-11441795.html heise-dlss5-rtx40.md
  save_article https://nvidianews.nvidia.com/news/nvidia-dlss-5-delivers-ai-powered-breakthrough-in-visual-fidelity-for-games nvidia-dlss5-announcement.md
  save_article https://store.pimax.com/blogs/blogs/the-crystal-supers-secret-weapon-dynamic-foveated-rendering pimax-dfr-crystal-super.md
fi

if [ "${REFRESH_COPIES:-0}" = "1" ]; then
  for n in Quad-Views-Foveated PimaxMagic4All OpenXR-Eye-Trackers vrperfkit VRPerfKit_RSF \
           vrperfkit-granther CheekyFoveatedDLSS dlss5-vr PSVR2Toolkit; do
    [ -f "$CACHE/$n/README.md" ] && cp "$CACHE/$n/README.md" "$HERE/readmes/$n-README.md"
  done
fi
echo done
