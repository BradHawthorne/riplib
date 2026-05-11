#!/usr/bin/env bash
# Sync this riplib working tree into an A2GSPU repo's vendored
# libraries/libriplib/{src,include,fonts,icons}.
#
# Usage:  scripts/sync-to-a2gspu.sh PATH_TO_A2GSPU_REPO
#
# PATH_TO_A2GSPU_REPO is the root of the A2GSPU repo (the directory
# that contains the A2GSPU-Firmware/ subtree).  The script targets
# A2GSPU-Firmware/libraries/libriplib/ under that root.
#
# Does NOT touch:
#   - libraries/libriplib/platform_a2gspu.c   (A2GSPU's platform stubs)
#   - libraries/libriplib/CMakeLists.txt      (A2GSPU's build glue)
#   - libraries/libriplib/README.md           (A2GSPU integration notes)
#
# Writes the current HEAD commit to libraries/libriplib/SYNC_REF.

set -euo pipefail

if [ $# -ne 1 ]; then
    echo "usage: $0 PATH_TO_A2GSPU_REPO" >&2
    exit 1
fi

RIPLIB_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
A2GSPU_ROOT="$(cd "$1" && pwd)"
DST="$A2GSPU_ROOT/A2GSPU-Firmware/libraries/libriplib"

if [ ! -d "$DST" ]; then
    echo "error: expected $DST to already exist" >&2
    echo "       (it should contain platform_a2gspu.c, CMakeLists.txt, README.md)" >&2
    exit 1
fi

echo "Syncing riplib  -> $DST"

for dir in src include fonts icons; do
    if [ ! -d "$RIPLIB_ROOT/$dir" ]; then
        echo "error: missing $RIPLIB_ROOT/$dir" >&2
        exit 1
    fi
    echo "  $dir/"
    rm -rf "$DST/$dir"
    cp -r "$RIPLIB_ROOT/$dir" "$DST/$dir"
done

# Record the source ref so future syncs and parity checks know
# what point in riplib history this copy represents.
ref="$(git -C "$RIPLIB_ROOT" rev-parse HEAD)"
echo "$ref" > "$DST/SYNC_REF"
echo
echo "SYNC_REF: $ref"
echo "Done.  Remember to commit the changes in the A2GSPU repo."
