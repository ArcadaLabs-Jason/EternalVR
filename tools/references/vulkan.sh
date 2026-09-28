#!/usr/bin/env bash
# Recreate the reference items for topic 02 (Vulkan layers, stereo rendering, SPIR-V tooling).
#
# Default: (re)create everything under reference/_cache/ that is missing (large specs,
# PDFs and shallow source clones). Idempotent: existing files/clones are left alone.
#
#   reference/vulkan/fetch.sh              # fill reference/_cache/ only
#   reference/vulkan/fetch.sh --committed  # also re-download the committed copies in
#                                          # reference/vulkan/ and reference/spirv/ (overwrites)
#   reference/vulkan/fetch.sh --update     # also 'git pull --depth 1' existing clones
#
# Requires: bash, curl, git, python3; pandoc for HTML->markdown conversion of articles.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REF="$(cd "$HERE/.." && pwd)"
CACHE="$REF/_cache"
VK="$REF/vulkan"
SPV="$REF/spirv"
FETCHED="${FETCH_DATE:-$(date +%Y-%m-%d)}"
UA='Mozilla/5.0 (X11; Linux x86_64) reference-fetch'

COMMITTED=0; UPDATE=0
for a in "$@"; do
  case "$a" in
    --committed) COMMITTED=1 ;;
    --update) UPDATE=1 ;;
    *) echo "unknown arg $a" >&2; exit 2 ;;
  esac
done

mkdir -p "$CACHE" "$VK/loader" "$VK/spec" "$VK/articles" "$SPV"

# get URL DEST [force]  -- download unless DEST exists (or force=1)
get() {
  local url="$1" dest="$2" force="${3:-0}"
  if [[ -s "$dest" && "$force" != 1 ]]; then return 0; fi
  mkdir -p "$(dirname "$dest")"
  echo "fetch $url"
  if ! curl -fsSL -A "$UA" --retry 3 -o "$dest.part" "$url"; then
    echo "  WARN: failed $url" >&2; rm -f "$dest.part"; return 0
  fi
  mv "$dest.part" "$dest"
}

# clone NAME URL  -- shallow clone into _cache/NAME
clone() {
  local name="$1" url="$2" dir="$CACHE/$1"
  if [[ -d "$dir/.git" ]]; then
    if [[ $UPDATE == 1 ]]; then git -C "$dir" pull --depth 1 --ff-only || true; fi
    return 0
  fi
  echo "clone $url"
  git clone --depth 1 --quiet "$url" "$dir" || echo "  WARN: clone failed $url" >&2
}

# text_with_header URL DEST COMMENT_PREFIX  -- committed copy with a provenance header
text_with_header() {
  local url="$1" dest="$2" c="$3"
  if [[ -s "$dest" && $COMMITTED != 1 ]]; then return 0; fi
  local tmp; tmp="$(mktemp)"
  get "$url" "$tmp" 1
  if [[ -s "$tmp" ]]; then
    case "$c" in
      html) { printf '<!-- Source: %s -->\n<!-- Fetched: %s. Verbatim copy; see MANIFEST.part.md for license. -->\n\n' "$url" "$FETCHED"; cat "$tmp"; } > "$dest" ;;
      *)    { printf '%s Source: %s\n%s Fetched: %s. Verbatim copy; see MANIFEST.part.md for license.\n\n' "$c" "$url" "$c" "$FETCHED"; cat "$tmp"; } > "$dest" ;;
    esac
  fi
  rm -f "$tmp"
}

# article URL DEST TITLE  -- HTML page -> markdown with header (committed)
article() {
  local url="$1" dest="$2" title="$3"
  if [[ -s "$dest" && $COMMITTED != 1 ]]; then return 0; fi
  local tmp; tmp="$(mktemp).html"
  get "$url" "$tmp" 1
  if [[ -s "$tmp" ]] && command -v pandoc >/dev/null; then
    {
      printf '# %s\n\n> Source: <%s>  \n> Fetched: %s. Converted from HTML with pandoc; navigation chrome may remain. Copyright remains with the original author/publisher; kept here as a private research copy.\n\n' "$title" "$url" "$FETCHED"
      pandoc -f html -t gfm-raw_html --wrap=none "$tmp" 2>/dev/null
    } > "$dest"
  fi
  rm -f "$tmp"
}

RAW=https://raw.githubusercontent.com

########## 1. Large items -> _cache ##########
get https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html "$CACHE/vkspec.html"
get https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html      "$CACHE/SPIRV.html"
get https://advances.realtimerendering.com/s2020/RenderingDoomEternal.pdf "$CACHE/pdf/RenderingDoomEternal-SIGGRAPH2020.pdf"
get https://media.steampowered.com/apps/valve/2015/Alex_Vlachos_Advanced_VR_Rendering_GDC2015.pdf "$CACHE/pdf/Vlachos-Advanced-VR-Rendering-GDC2015.pdf"
get https://media.steampowered.com/apps/valve/2016/Alex_Vlachos_Advanced_VR_Rendering_Performance_GDC2016.pdf "$CACHE/pdf/Vlachos-Advanced-VR-Rendering-Performance-GDC2016.pdf"
get https://www.lunarg.com/wp-content/uploads/2022/12/The-Vulkan-Loader-and-Vulkan-Layers_-Diagnosing-Layer-Issues.pdf "$CACHE/pdf/LunarG-Loader-and-Layers-Diagnosing-Layer-Issues.pdf"
get https://community.arm.com/cfs-file/__key/communityserver-blogs-components-weblogfiles/00-00-00-20-66/5_2D00_mmg_2D00_siggraph2016_2D00_multiview_2D00_cass.pdf "$CACHE/pdf/ARM-Multiview-SIGGRAPH2016.pdf"

# Source trees (shallow). Library sources we would link or study.
clone SPIRV-Cross     https://github.com/KhronosGroup/SPIRV-Cross.git
clone glslang         https://github.com/KhronosGroup/glslang.git
clone SPIRV-Tools     https://github.com/KhronosGroup/SPIRV-Tools.git
clone SPIRV-Headers   https://github.com/KhronosGroup/SPIRV-Headers.git
clone SPIRV-Reflect   https://github.com/KhronosGroup/SPIRV-Reflect.git
clone minhook         https://github.com/TsudaKageyu/minhook.git
clone Vulkan-Headers  https://github.com/KhronosGroup/Vulkan-Headers.git
clone Vulkan-Utility-Libraries https://github.com/KhronosGroup/Vulkan-Utility-Libraries.git
clone Vulkan-Guide    https://github.com/KhronosGroup/Vulkan-Guide.git
# Layer design references (read-only study material)
clone vkBasalt        https://github.com/DadSchoorse/vkBasalt.git
clone Fossilize       https://github.com/ValveSoftware/Fossilize.git
clone Vk3DVision-Public https://github.com/helifax/Vk3DVision-Public.git

########## 2. Committed copies: Vulkan loader docs ##########
LOADER=$RAW/KhronosGroup/Vulkan-Loader/main/docs
for f in LoaderLayerInterface.md LoaderInterfaceArchitecture.md LoaderApplicationInterface.md LoaderDebugging.md; do
  text_with_header "$LOADER/$f" "$VK/loader/$f" html
done

########## 3. Committed copies: Vulkan extension appendices / proposals ##########
DOCS=$RAW/KhronosGroup/Vulkan-Docs/main
for e in VK_KHR_multiview VK_KHR_dynamic_rendering VK_EXT_descriptor_indexing VK_KHR_fragment_shading_rate \
         VK_EXT_shader_viewport_index_layer VK_KHR_create_renderpass2 VK_KHR_timeline_semaphore \
         VK_KHR_synchronization2 VK_EXT_graphics_pipeline_library VK_EXT_shader_module_identifier \
         VK_KHR_dynamic_rendering_local_read VK_KHR_maintenance5 VK_EXT_shader_object; do
  text_with_header "$DOCS/appendices/$e.adoc" "$VK/spec/appendix-$e.adoc" '//'
done
for p in VK_KHR_dynamic_rendering VK_EXT_graphics_pipeline_library VK_EXT_shader_module_identifier \
         VK_KHR_fragment_shading_rate VK_EXT_shader_object VK_KHR_dynamic_rendering_local_read; do
  text_with_header "$DOCS/proposals/$p.adoc" "$VK/spec/proposal-$p.adoc" '//'
done

########## 4. Committed copies: SPIR-V / GLSL extension specs and tool READMEs ##########
text_with_header $RAW/KhronosGroup/SPIRV-Registry/main/extensions/KHR/SPV_KHR_multiview.asciidoc "$SPV/SPV_KHR_multiview.asciidoc" '//'
text_with_header $RAW/KhronosGroup/SPIRV-Registry/main/extensions/EXT/SPV_EXT_shader_viewport_index_layer.asciidoc "$SPV/SPV_EXT_shader_viewport_index_layer.asciidoc" '//'
text_with_header $RAW/KhronosGroup/SPIRV-Registry/main/extensions/KHR/SPV_KHR_fragment_shading_rate.asciidoc "$SPV/SPV_KHR_fragment_shading_rate.asciidoc" '//'
text_with_header $RAW/KhronosGroup/GLSL/main/extensions/ext/GL_EXT_multiview.txt "$SPV/GL_EXT_multiview.txt" '#'
text_with_header $RAW/KhronosGroup/SPIRV-Cross/main/README.md   "$SPV/README-SPIRV-Cross.md" html
text_with_header $RAW/KhronosGroup/glslang/main/README.md       "$SPV/README-glslang.md" html
text_with_header $RAW/KhronosGroup/SPIRV-Tools/main/README.md   "$SPV/README-SPIRV-Tools.md" html
text_with_header $RAW/KhronosGroup/SPIRV-Reflect/main/README.md "$SPV/README-SPIRV-Reflect.md" html
text_with_header $RAW/KhronosGroup/SPIRV-Headers/main/README.md "$SPV/README-SPIRV-Headers.md" html
text_with_header $RAW/TsudaKageyu/minhook/master/README.md      "$SPV/README-MinHook.md" html

########## 5. Committed copies: articles / project docs ##########
text_with_header $RAW/ValveSoftware/Fossilize/master/README.md "$VK/articles/README-Fossilize.md" html
text_with_header $RAW/DadSchoorse/vkBasalt/master/README.md    "$VK/articles/README-vkBasalt.md" html
text_with_header $RAW/helifax/Vk3DVision-Public/main/README.md "$VK/articles/README-Vk3DVision-Public.md" html
VVL=$RAW/KhronosGroup/Vulkan-ValidationLayers/main
text_with_header $VVL/docs/gpu_av_shader_instrumentation.md "$VK/articles/VVL-gpu_av_shader_instrumentation.md" html
text_with_header $VVL/docs/gpu_validation.md                "$VK/articles/VVL-gpu_validation.md" html
text_with_header $VVL/layers/gpuav/spirv/README.md          "$VK/articles/VVL-gpuav-spirv-passes-README.md" html
article https://www.mattstevens.co.uk/posts/bad-vulkan-layers/ "$VK/articles/bad-vulkan-layers-mattstevens.md" "Bad Vulkan Layers (Matt Stevens)"
article https://developer.nvidia.com/blog/turing-multi-view-rendering-vrworks/ "$VK/articles/nvidia-turing-multi-view-rendering.md" "Turing Multi-View Rendering in VRWorks (NVIDIA)"
article https://docs.unity3d.com/Manual/SinglePassInstancing.html "$VK/articles/unity-single-pass-instanced.md" "Single Pass Instanced rendering (Unity Manual)"
article https://developer.oculus.com/documentation/native/android/mobile-multiview/ "$VK/articles/meta-mobile-multiview.md" "Multi-View (Meta Horizon OS developer docs)"
article http://docs.uevr.io/usage/overview.html "$VK/articles/uevr-overview.md" "UEVR detailed overview (rendering methods)"
article https://3dsurroundgaming.com/Vk3DVision.html "$VK/articles/vk3dvision-3dsurroundgaming.md" "Vk3DVision project page (3D Surround Gaming)"

echo "done. cache: $CACHE"
