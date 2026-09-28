#!/usr/bin/env bash
# Idempotent fetch of DOOM Eternal / id Tech 7 reference material (research topic 03).
#
# - Git sources are shallow-cloned into reference/_cache/<name>/ (gitignored); existing clones
#   are fast-forwarded.
# - Large binaries (SIGGRAPH PDF) go to reference/_cache/.
# - The committed copies under reference/idtech7/ (docs/*.md, typeinfo/*) are only (re)generated
#   when missing, or for all of them with REFRESH_COPIES=1.
#
# Needs: git, curl, python3; pandoc and pdftotext only when regenerating docs.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
CACHE="$HERE/../_cache"
DOCS="$HERE/docs"
TI="$HERE/typeinfo"
UA='Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126 Safari/537.36'
TODAY="2026-09-25"
REFRESH="${REFRESH_COPIES:-0}"
mkdir -p "$CACHE" "$DOCS" "$TI" "$CACHE/idtech7-web"

clone() { # name url
  local name="$1" url="$2" dst="$CACHE/$1"
  if [ -d "$dst/.git" ]; then
    echo "[update] $name"; git -C "$dst" pull --ff-only -q || echo "  (pull failed, keeping existing)"
  else
    echo "[clone] $name"; git clone -q --depth 1 "$url" "$dst"
  fi
}

download() { # url dest
  local url="$1" dst="$2"
  if [ -s "$dst" ] && [ "$REFRESH" != 1 ]; then echo "[have] $(basename "$dst")"; return 0; fi
  echo "[get] $url"; curl -fsSL -A "$UA" -o "$dst" "$url"
}

need_copy() { [ "$REFRESH" = 1 ] || [ ! -s "$1" ]; }

# ---------------------------------------------------------------- source repositories
clone meathook           https://github.com/brongo/m3337ho0o0ok.git                 # no license file
clone meathook-ap        https://github.com/snowzzrra/Meathook-AP.git                # no license file
clone de_advancedoptions https://github.com/SteamKaibz/DE_AdvancedOptionsModPublic.git # BSD-2-Clause
clone ap-mod             https://github.com/snowzzrra/DoomEternal-AP-Mod.git          # no license file
clone cvarlist           https://github.com/Official-KEX/doom-eternal-full-cvarlist.git # MIT
clone eternalpatcher     https://github.com/dcealopez/EternalPatcher.git              # no license file
clone restricted-cmds    https://github.com/dpteam/DOOM_Eternal_Restricted_CMDs_Patcher_Unlocker.git # GPL-3.0
clone eternalbasher      https://github.com/leveste/EternalBasher.git                 # GPL-3.0
clone livesplit          https://github.com/loitho/doom-eternal.git                   # no license file
clone downpatcher        https://github.com/mcdalcin/DoomEternalDownpatcher.git       # no license file
clone de_internal        https://github.com/marcopetro/DoomEternal_Internal.git       # no license file
clone desru              https://github.com/bowsr/DESRU.git                           # GPL-3.0

# ---------------------------------------------------------------- large documents
mkdir -p "$CACHE/siggraph2020"
download https://advances.realtimerendering.com/s2020/RenderingDoomEternal.pdf "$CACHE/siggraph2020/RenderingDoomEternal.pdf"

# ---------------------------------------------------------------- committed doc copies
strip_data_uris() { sed -E 's/!\[[^]]*\]\(data:[^)]*\)//g'; }

if need_copy "$DOCS/coenen-doom-eternal-graphics-study.md"; then
  download https://simoncoenen.com/blog/programming/graphics/DoomEternalStudy "$CACHE/idtech7-web/coenen.html"
  { printf '<!-- Source: https://simoncoenen.com/blog/programming/graphics/DoomEternalStudy -->\n<!-- Fetched: %s -->\n\n' "$TODAY"
    pandoc -f html -t gfm-raw_html "$CACHE/idtech7-web/coenen.html" \
      | awk '/^# DOOM Eternal - Graphics Study/{f=1} /^\[« Previous/{exit} f' | strip_data_uris
  } > "$DOCS/coenen-doom-eternal-graphics-study.md"
fi

wiki_book() { # book-slug output-name
  local out="$DOCS/$2.md"
  need_copy "$out" || return 0
  download "https://wiki.eternalmods.com/books/$1/export/markdown" "$CACHE/idtech7-web/wiki_$1.md"
  { printf '<!-- Source: https://wiki.eternalmods.com/books/%s -->\n<!-- Fetched: %s (BookStack markdown export of the whole book) -->\n\n' "$1" "$TODAY"
    pandoc -f markdown+raw_html -t gfm-raw_html "$CACHE/idtech7-web/wiki_$1.md" | strip_data_uris
  } > "$out"
}
wiki_book eternal-reverse-engineering-file-formats eternalmods-wiki-re-file-formats
wiki_book eternal-command-console                  eternalmods-wiki-command-console
wiki_book eternal-idstudio                         eternalmods-wiki-idstudio
wiki_book eternal-miscellaneous                    eternalmods-wiki-miscellaneous-swf-atlan
wiki_book eternal-how-to-create-mods               eternalmods-wiki-how-to-create-mods

if need_copy "$DOCS/idstudio-official-docs-selected.md"; then
  pages="faq/doom-eternal-mods getting-started/mod-packer/index getting-started/mod-packer/mod-structure
getting-started/about-decls/index getting-started/console getting-started/engine-tab/useful-cvars-and-console-commands
tutorials/change-a-weapons-projectile ui/add-floating-text-to-a-map known-issues
release-notes/2024/08/27/patch-1 release-notes/2024/11/21/update-1 release-notes/2025/06/17/update-2
release-notes/2025/07/31/update-3 release-notes/2026/02/10/update-4 release-notes/2026/05/19/update-5"
  { printf '<!-- Source: https://idstudio.idsoftware.com/ (selected pages, listed per section) -->\n<!-- Fetched: %s -->\n\n# idStudio official documentation -- selected pages\n\n' "$TODAY"
    for p in $pages; do
      f="$CACHE/idtech7-web/ids_$(echo "$p" | tr '/' '_').html"
      # The docs site serves static pages at <path>.html; the bare path is a client-side route.
      download "https://idstudio.idsoftware.com/$p.html" "$f"
      printf '\n\n---\n\n<!-- Page: https://idstudio.idsoftware.com/%s -->\n\n' "$p"
      pandoc -f html -t gfm-raw_html "$f" | awk '/^# /{f=1} f' | sed '/^Previous$/,$d' | strip_data_uris
    done
  } > "$DOCS/idstudio-official-docs-selected.md"
  echo "  note: the 'useful cvars' table converts to [TABLE]; see the committed copy for the plain-text version"
fi

if need_copy "$DOCS/siggraph2020-rendering-doom-eternal-slides.md"; then
  { printf '<!-- Source: https://advances.realtimerendering.com/s2020/RenderingDoomEternal.pdf -->\n<!-- Fetched: %s. Text extracted with pdftotext -layout -->\n\n# Rendering the Hellscape of DOOM Eternal (SIGGRAPH 2020) -- extracted slide text\n\n```text\n' "$TODAY"
    pdftotext -layout "$CACHE/siggraph2020/RenderingDoomEternal.pdf" -
    printf '\n```\n'
  } > "$DOCS/siggraph2020-rendering-doom-eternal-slides.md"
fi

if need_copy "$DOCS/pcgh-billy-khan-idtech7-interview-2020.md"; then
  # Live site rejects scripted clients; use the Wayback Machine raw snapshot.
  download "https://web.archive.org/web/2021id_/https://www.pcgameshardware.de/Doom-Eternal-Spiel-72610/Specials/Interview-mit-Lead-Engine-Programmer-Billy-Khan-1360708/" "$CACHE/idtech7-web/pcgh.html"
  echo "  note: pcgh-billy-khan-idtech7-interview-2020.md has a hand-written summary header; regenerate the body from $CACHE/idtech7-web/pcgh.html and keep the header"
fi

# ---------------------------------------------------------------- typeinfo / cvar dumps
MH="$CACHE/meathook/m34thook"
enum_names() { # file enum-name prefix
  python3 - "$1" "$2" "$3" <<'PY'
import re,sys
s=open(sys.argv[1],encoding='utf-8',errors='replace').read()
m=re.search(r'enum\s+'+re.escape(sys.argv[2])+r'[^{]*\{([^}]*)\}',s)
for n in m.group(1).split(','):
    n=n.strip()
    if n.startswith(sys.argv[3]): print(n[len(sys.argv[3]):])
PY
}
if need_copy "$TI/meathook-type-names-6.66.txt"; then
  { printf '# Source: https://github.com/brongo/m3337ho0o0ok/blob/master/m34thook/alltypes.h\n# Fetched: %s\n# Type names registered in DOOM Eternal typeinfo (game ~6.66), one per line.\n' "$TODAY"
    tr ',' '\n' < "$MH/alltypes.h" | tr -d '"{} \t\r' | grep -v '^$'; } > "$TI/meathook-type-names-6.66.txt"
fi
if need_copy "$TI/meathook-cvar-names-6.66.txt"; then
  { printf '# Source: https://github.com/brongo/m3337ho0o0ok/blob/master/m34thook/pregenerated/doom_eternal_cvars_generated.hpp\n# Fetched: %s\n# Cvar names (game ~6.66).\n' "$TODAY"
    enum_names "$MH/pregenerated/doom_eternal_cvars_generated.hpp" de_cvar_e cvr_; } > "$TI/meathook-cvar-names-6.66.txt"
fi
if need_copy "$TI/meathook-property-names-6.66.txt"; then
  { printf '# Source: https://github.com/brongo/m3337ho0o0ok/blob/master/m34thook/pregenerated/doom_eternal_properties_generated.hpp\n# Fetched: %s\n# Field (property) names used by any reflected class (game ~6.66).\n' "$TODAY"
    enum_names "$MH/pregenerated/doom_eternal_properties_generated.hpp" de_prop_e prp_; } > "$TI/meathook-property-names-6.66.txt"
fi
if need_copy "$TI/kex-cvarlist-2024.tsv"; then
  python3 - "$CACHE/cvarlist/DOOM Eternal - Complete CVAR List.xlsx" "$TI/kex-cvarlist-2024.tsv" <<'PY'
import sys,zipfile,xml.etree.ElementTree as ET
z=zipfile.ZipFile(sys.argv[1]); M='{http://schemas.openxmlformats.org/spreadsheetml/2006/main}'
ss=[''.join(t.text or '' for t in si.iter(M+'t')) for si in ET.fromstring(z.read('xl/sharedStrings.xml')).findall(M+'si')]
with open(sys.argv[2],'w') as f:
    f.write('# Source: https://github.com/Official-KEX/doom-eternal-full-cvarlist (MIT)\n# Columns: name, description, value (value at dump time), type/range\n')
    for s in sorted(n for n in z.namelist() if n.startswith('xl/worksheets/sheet')):
        for r in ET.fromstring(z.read(s)).iter(M+'row'):
            vals=[]
            for c in r.findall(M+'c'):
                v=c.find(M+'v')
                if v is None:
                    i=c.find(M+'is'); vals.append(''.join(x.text or '' for x in i.iter(M+'t')) if i is not None else '')
                else:
                    vals.append(ss[int(v.text)] if c.get('t')=='s' else v.text)
            vals=[(x or '').replace('\t',' ').replace('\n',' ') for x in vals]+['','','','']
            f.write('\t'.join(vals[:4])+'\n')
PY
fi
AO="$CACHE/de_advancedoptions"
for pair in "DE/idLib_Vanilla.h:advancedoptions-idLib_Vanilla-rev3.h" "DE/idLib_Sandbox.h:advancedoptions-idLib_Sandbox-rev3.h" "DE/Sigs.h:advancedoptions-Sigs-rev3.h"; do
  src="${pair%%:*}"; dst="$TI/${pair##*:}"
  if need_copy "$dst"; then
    { printf '// Source: https://github.com/SteamKaibz/DE_AdvancedOptionsModPublic/blob/%s/%s\n// Fetched: %s. BSD-2-Clause (see advancedoptions-LICENSE.txt). Targets DOOM Eternal 6.66 Rev 3.\n\n' "$(git -C "$AO" rev-parse HEAD)" "$src" "$TODAY"; cat "$AO/$src"; } > "$dst"
  fi
done
need_copy "$TI/advancedoptions-LICENSE.txt" && cp "$AO/LICENSE.txt" "$TI/advancedoptions-LICENSE.txt"
echo "[note] typeinfo/vr-relevant-cvars.md is generated from kex-cvarlist-2024.tsv by a one-off script; edit by hand or regenerate deliberately."
echo "done."
