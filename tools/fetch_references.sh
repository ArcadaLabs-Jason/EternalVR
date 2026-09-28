#!/usr/bin/env bash
# Recreate the local reference library (reference/, ignored by git apart from MANIFEST.md) from its
# public sources. Each topic's script lives in tools/references/<topic>.sh; it is copied to
# reference/<topic>/fetch.sh and run from there, so its paths (the topic folder and reference/_cache/)
# resolve as they always have. Safe to re-run. Pass through any flags/env the topic scripts accept,
# e.g. REFRESH_COPIES=1 tools/fetch_references.sh
set -u
root="$(cd "$(dirname "$0")/.." && pwd)"
status=0
for script in "$root"/tools/references/*.sh; do
  topic="$(basename "$script" .sh)"
  echo "== $topic"
  dir="$root/reference/$topic"
  mkdir -p "$dir"
  cp "$script" "$dir/fetch.sh"
  (cd "$dir" && bash ./fetch.sh "$@") || { echo "!! $topic fetch failed" >&2; status=1; }
done
exit $status
