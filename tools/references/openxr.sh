#!/usr/bin/env bash
# Idempotent fetch of OpenXR / VR-runtime reference material (research topic 01).
# Git sources are shallow-cloned into reference/_cache/<name>/ (gitignored).
# Large single-file documents go to reference/_cache/openxr/.
# The small committed copies under reference/openxr/ were taken from these clones;
# re-run with REFRESH_COPIES=1 to refresh them from the caches.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
CACHE="$HERE/../_cache"
mkdir -p "$CACHE/openxr"

clone() { # name url
  local name="$1" url="$2" dst="$CACHE/$1"
  if [ -d "$dst/.git" ]; then
    echo "[update] $name"; git -C "$dst" pull --ff-only --depth 1 -q || echo "  (pull failed, keeping existing)"
  else
    echo "[clone] $name"; git clone -q --depth 1 "$url" "$dst"
  fi
}

download() { # url dest
  local url="$1" dst="$2"
  if [ -s "$dst" ]; then echo "[have] $(basename "$dst")"; return; fi
  echo "[get] $url"; curl -fsSL -A 'Mozilla/5.0' -o "$dst" "$url"
}

# Khronos sources: spec (asciidoc + xr.xml registry), loader docs, SDK, runtime inventory.
clone OpenXR-Docs               https://github.com/KhronosGroup/OpenXR-Docs.git
clone OpenXR-SDK-Source         https://github.com/KhronosGroup/OpenXR-SDK-Source.git
clone OpenXR-Inventory          https://github.com/KhronosGroup/OpenXR-Inventory.git
# Runtime and layer sources (all MIT).
clone VirtualDesktop-OpenXR     https://github.com/mbucchia/VirtualDesktop-OpenXR.git
clone VirtualDesktop-OpenXR.wiki https://github.com/mbucchia/VirtualDesktop-OpenXR.wiki.git
clone OpenXR-Toolkit            https://github.com/mbucchia/OpenXR-Toolkit.git
clone Quad-Views-Foveated       https://github.com/mbucchia/Quad-Views-Foveated.git
# Prior art: Vulkan-layer VR mod that runs its OpenXR session on D3D12 and shares Vulkan images.
clone BotW-BetterVR             https://github.com/Crementif/BotW-BetterVR.git

# Large single documents.
download https://registry.khronos.org/OpenXR/specs/1.1/html/xrspec.html   "$CACHE/openxr/xrspec-1.1.html"
download https://registry.khronos.org/OpenXR/specs/1.1/loader.html        "$CACHE/openxr/openxr_loader_spec.html"
download https://www.khronos.org/files/openxr-10-reference-guide.pdf      "$CACHE/openxr/openxr-10-reference-guide.pdf"
download https://registry.khronos.org/OpenXR/specs/1.1/pdf/xrspec.pdf     "$CACHE/openxr/xrspec-1.1.pdf"
download https://github.khronos.org/OpenXR-Inventory/extension_support.html "$CACHE/openxr/openxr-inventory-extension-support.html"

if [ "${REFRESH_COPIES:-0}" = "1" ]; then
  O="$HERE"; D="$CACHE/OpenXR-Docs/specification"; E="$D/sources/chapters/extensions"
  mkdir -p "$O/loader" "$O/spec-chapters" "$O/extensions" "$O/registry" "$O/vdxr" "$O/inventory"
  cp "$CACHE"/OpenXR-SDK-Source/specification/loader/{api_layer,runtime,application,design,overview,loader,debug}.adoc "$O/loader/"
  cp "$D"/sources/chapters/{session,rendering,input,spaces,view_configurations,versions,system,instance}.adoc "$O/spec-chapters/"
  cp "$E"/khr/khr_{vulkan_enable,vulkan_enable2,vulkan_swapchain_format_list,composition_layer_depth,composition_layer_cylinder,composition_layer_equirect2,composition_layer_color_scale_bias,visibility_mask,maintenance1,locate_spaces,binding_modification,generic_controller,win32_convert_performance_counter_time}.adoc "$O/extensions/"
  cp "$E"/ext/ext_{hand_tracking,hand_interaction,eye_gaze_interaction,local_floor,palm_pose,dpad_binding,active_action_set_priority,hp_mixed_reality_controller,performance_settings,user_presence,debug_utils,frame_synthesis,haptic_parametric,view_configuration_views_change}.adoc "$O/extensions/"
  cp "$E"/fb/fb_{foveation,foveation_configuration,foveation_vulkan,swapchain_update_state,swapchain_update_state_vulkan,display_refresh_rate,composition_layer_settings,haptic_amplitude_envelope,haptic_pcm,touch_controller_pro}.adoc "$O/extensions/"
  cp "$E"/meta/meta_{foveation_eye_tracked,touch_controller_plus,recommended_layer_resolution,performance_metrics,vulkan_swapchain_create_info}.adoc "$O/extensions/"
  cp "$D/registry/xr.xml" "$O/registry/"
  for f in valve_steamvr meta_pc meta_pc_dev_mode microsoft_pc varjo monado_windows htc_vive_cosmos; do
    cp "$CACHE/OpenXR-Inventory/runtimes/$f.json" "$CACHE/OpenXR-Inventory/runtimes/$f.json.license" "$O/inventory/"
  done
  W="$CACHE/VirtualDesktop-OpenXR.wiki"
  for f in Home Developers Application-Compatibility 'Oculus-"Runtimes"' 'OculusXR-(OVRPlugin)-Compatibility-Mode'; do
    out=$(echo "$f" | tr -d '"()')
    { echo "<!-- Source: https://github.com/mbucchia/VirtualDesktop-OpenXR/wiki/$f | fetched $(date +%F) via git clone of VirtualDesktop-OpenXR.wiki.git -->"; echo; cat "$W/$f.md"; } > "$O/vdxr/$out.md"
  done
  { echo "<!-- Source: https://github.com/mbucchia/VirtualDesktop-OpenXR/blob/main/README.md | fetched $(date +%F) -->"; echo; cat "$CACHE/VirtualDesktop-OpenXR/README.md"; } > "$O/vdxr/README.md"
  # Web pages under web/ were fetched with curl and converted by pandoc:
  #   curl -sSL -A 'Mozilla/5.0' URL | pandoc -f html -t gfm-raw_html --wrap=none
  # then lines containing inline data: images were stripped. See MANIFEST.part.md for URLs.
fi
echo "done"
