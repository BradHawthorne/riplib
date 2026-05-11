#!/usr/bin/env bash
# Diff this riplib working tree's library sources against the
# vendored copy inside an A2GSPU repo.  Prints any drift and exits
# non-zero if the trees differ.
#
# Usage:  scripts/check-a2gspu-parity.sh PATH_TO_A2GSPU_REPO
#
# Compares src/, include/, fonts/, icons/.  Ignores the A2GSPU-side-
# only files (platform_a2gspu.c, CMakeLists.txt, README.md, SYNC_REF).

set -euo pipefail

if [ $# -ne 1 ]; then
    echo "usage: $0 PATH_TO_A2GSPU_REPO" >&2
    exit 1
fi

RIPLIB_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
A2GSPU_ROOT="$(cd "$1" && pwd)"
SRC_DST="$A2GSPU_ROOT/A2GSPU-Firmware/libraries/libriplib"

if [ ! -d "$SRC_DST" ]; then
    echo "error: $SRC_DST not found" >&2
    exit 1
fi

drift=0
for dir in src include fonts icons; do
    if ! diff -r -q "$RIPLIB_ROOT/$dir" "$SRC_DST/$dir" 2>&1; then
        drift=1
    fi
done

echo
if [ "$drift" -eq 0 ]; then
    riplib_head="$(git -C "$RIPLIB_ROOT" rev-parse HEAD)"
    a2gspu_ref="$(cat "$SRC_DST/SYNC_REF" 2>/dev/null || echo unknown)"
    echo "PARITY OK: riplib HEAD = $riplib_head"
    echo "           A2GSPU SYNC_REF = $a2gspu_ref"
    if [ "$riplib_head" != "$a2gspu_ref" ]; then
        echo
        echo "  (sources match but SYNC_REF is stale.  Re-run sync-to-a2gspu.sh"
        echo "   to refresh the recorded commit hash.)"
    fi
    exit 0
else
    echo "PARITY DRIFT detected — see diff above."
    echo "  - If the riplib side is correct: scripts/sync-to-a2gspu.sh $A2GSPU_ROOT"
    echo "  - If the A2GSPU side has fixes that belong upstream: port them to riplib first,"
    echo "    commit there, then sync forward."
    exit 1
fi
