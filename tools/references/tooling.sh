#!/usr/bin/env bash
# Recreate the reference items for topic 09 (development loop, tooling, build, CI, release).
#
# Idempotent: files that already exist are left alone unless FORCE=1.
#
#   reference/tooling/fetch.sh            # fetch anything missing
#   FORCE=1 reference/tooling/fetch.sh    # re-download everything (overwrites committed copies)
#
# Committed copies land in reference/tooling/docs/ with a Source/Fetched header.
# Nothing is cloned for this topic: safetyhook, kananlib and Fossilize clones are already
# produced by reference/prior-art/fetch.sh and reference/vulkan/fetch.sh.
#
# Requires: bash, curl; pandoc for HTML -> markdown conversion.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOCS="$HERE/docs"
FETCHED="${FETCH_DATE:-$(date +%Y-%m-%d)}"
UA='Mozilla/5.0 (X11; Linux x86_64) reference-fetch'
mkdir -p "$DOCS"

have() { [[ -s "$1" && "${FORCE:-0}" != 1 ]]; }

# raw NAME URL  -- source is already markdown/text; prepend a provenance header
raw() {
  local out="$DOCS/$1" url="$2"
  if have "$out"; then echo "[skip] $1"; return 0; fi
  echo "[raw]  $1"
  local tmp; tmp="$(mktemp)"
  if curl -fsSL -A "$UA" --retry 3 -o "$tmp" "$url"; then
    case "$out" in
      *.h) { printf '// Source: %s\n// Fetched: %s. Verbatim copy; see MANIFEST.part.md for license.\n\n' "$url" "$FETCHED"; cat "$tmp"; } > "$out" ;;
      *)   { printf '<!-- Source: %s -->\n<!-- Fetched: %s. Verbatim copy; see MANIFEST.part.md for license. -->\n\n' "$url" "$FETCHED"; cat "$tmp"; } > "$out" ;;
    esac
  else
    echo "  WARN: failed $url" >&2
  fi
  rm -f "$tmp"
}

# page NAME URL  -- HTML page converted to markdown with pandoc
page() {
  local out="$DOCS/$1" url="$2"
  if have "$out"; then echo "[skip] $1"; return 0; fi
  if ! command -v pandoc >/dev/null; then echo "  WARN: pandoc missing, skipping $1" >&2; return 0; fi
  echo "[page] $1"
  local tmp; tmp="$(mktemp).html"
  if curl -fsSL -A "$UA" --retry 3 -o "$tmp" "$url"; then
    { printf '<!-- Source: %s -->\n<!-- Fetched: %s. Converted from HTML with pandoc; navigation chrome may remain. -->\n\n' "$url" "$FETCHED"
      pandoc -f html -t gfm-raw_html --wrap=none "$tmp" 2>/dev/null; } > "$out"
  else
    echo "  WARN: failed $url" >&2
  fi
  rm -f "$tmp"
}

GH=https://raw.githubusercontent.com

# --- Frame capture / replay / validation
raw  gfxr-usage-desktop-vulkan.md      $GH/LunarG/gfxreconstruct/dev/USAGE_desktop_Vulkan.md
raw  gfxr-usage-desktop-openxr.md      $GH/LunarG/gfxreconstruct/dev/USAGE_desktop_OpenXR.md
raw  gfxr-license.md                   $GH/LunarG/gfxreconstruct/dev/LICENSE.md
raw  vvl-khronos-validation-layer.md   $GH/KhronosGroup/Vulkan-ValidationLayers/main/docs/khronos_validation_layer.md
raw  vvl-gpu-validation.md             $GH/KhronosGroup/Vulkan-ValidationLayers/main/docs/gpu_validation.md
raw  vvl-syncval-usage.md              $GH/KhronosGroup/Vulkan-ValidationLayers/main/docs/syncval_usage.md
raw  renderdoc_app.h                   $GH/baldurk/renderdoc/v1.x/renderdoc/api/app/renderdoc_app.h
page renderdoc-in-application-api.md   https://renderdoc.org/docs/in_application_api.html
page renderdoc-how-capture-frame.md    https://renderdoc.org/docs/how/how_capture_frame.html
page renderdoc-capture-attach.md       https://renderdoc.org/docs/window/capture_attach.html
page renderdoc-python-index.md         https://renderdoc.org/docs/python_api/index.html
page renderdoc-python-intro.md         https://renderdoc.org/docs/python_api/examples/renderdoc_intro.html
page renderdoc-python-basics.md        https://renderdoc.org/docs/python_api/examples/basics.html
page renderdoc-python-save-texture.md  https://renderdoc.org/docs/python_api/examples/renderdoc/save_texture.html
page nsight-graphics-capture-cli.md    https://docs.nvidia.com/nsight-graphics/UserGuide/graphics-capture-cli.html

# --- Headset-free OpenXR
raw  openxr-simulator-readme.md        $GH/elliotttate/OpenXR-Simulator/master/README.md
page meta-xr-simulator-intro.md        https://developers.meta.com/horizon/documentation/native/xrsim-intro/
page meta-xr-simulator-getting-started.md https://developers.meta.com/horizon/documentation/unity/xrsim-getting-started/
raw  steamvr-noheadset-readme.md       $GH/username223/SteamVRNoHeadset/master/README.md
raw  monado-readme.md                  https://gitlab.freedesktop.org/monado/monado/-/raw/main/README.md
raw  openxr-layer-template-readme.md   $GH/mbucchia/OpenXR-Layer-Template/main/README.md

# --- Driving the game on the rig
page wer-collecting-user-mode-dumps.md https://learn.microsoft.com/en-us/windows/win32/wer/collecting-user-mode-dumps
page sysinternals-procdump.md          https://learn.microsoft.com/en-us/sysinternals/downloads/procdump
page sysinternals-psexec.md            https://learn.microsoft.com/en-us/sysinternals/downloads/psexec
page wsl-filesystems-interop.md        https://learn.microsoft.com/en-us/windows/wsl/filesystems

# --- Build and code quality
raw  safetyhook-readme.md              $GH/cursey/safetyhook/main/README.md
raw  kananlib-readme.md                $GH/cursey/kananlib/main/README.md
page msvc-asan.md                      https://learn.microsoft.com/en-us/cpp/sanitizers/asan
page dotnet-single-file.md             https://learn.microsoft.com/en-us/dotnet/core/deploying/single-file/overview

# --- CI
raw  gha-windows-2025-runner-image.md  $GH/actions/runner-images/main/images/windows/Windows2025-Readme.md
page gha-billing.md                    https://docs.github.com/en/billing/concepts/product-billing/github-actions
page gha-self-hosted-runners.md        https://docs.github.com/en/actions/concepts/runners/self-hosted-runners
raw  gha-secure-use.md                 $GH/github/docs/main/content/actions/reference/security/secure-use.md

# --- Signing and antivirus
page signpath-foundation-terms.md      https://signpath.org/terms.html
page signpath-foundation-home.md       https://signpath.org/
page signpath-github-trusted-build.md  https://docs.signpath.io/trusted-build-systems/github
raw  artifact-signing-quickstart.md    $GH/MicrosoftDocs/azure-docs/main/articles/artifact-signing/quickstart.md
raw  artifact-signing-integrations.md  $GH/MicrosoftDocs/azure-docs/main/articles/artifact-signing/how-to-signing-integrations.md
raw  artifact-signing-faq.yml.md       $GH/MicrosoftDocs/azure-docs/main/articles/artifact-signing/faq.yml
page smartscreen-reputation.md         https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation
page defender-submission-guide.md      https://learn.microsoft.com/en-us/defender-xdr/submission-guide
page defender-false-positives.md       https://learn.microsoft.com/en-us/defender-endpoint/defender-endpoint-false-positives-negatives

echo "done: $DOCS"
