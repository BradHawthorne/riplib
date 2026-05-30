#!/usr/bin/env bash
# Sync this riplib working tree into an A2GSPU repo's vendored
# libraries/libriplib/{src,include,fonts,icons}.
#
# Usage:  scripts/sync-to-a2gspu.sh [--dry-run] PATH_TO_A2GSPU_REPO
#
# PATH_TO_A2GSPU_REPO is the root of the A2GSPU repo (the directory
# that contains the A2GSPU-Firmware/ subtree).  The script targets
# A2GSPU-Firmware/libraries/libriplib/ under that root.
#
# --dry-run shows which directories would be replaced and prints the
# would-be SYNC_REF without touching the destination.
#
# Does NOT touch:
#   - libraries/libriplib/platform_a2gspu.c   (A2GSPU's platform stubs)
#   - libraries/libriplib/CMakeLists.txt      (A2GSPU's build glue)
#   - libraries/libriplib/README.md           (A2GSPU integration notes)
#
# Also intentionally does NOT copy:
#   - consumer-handoff/                        (A2GSPU-bound content
#                                               extracted from RIPlib per
#                                               audit C-001; A2GSPU's
#                                               maintainers absorb it into
#                                               their own integration docs
#                                               separately, not via the
#                                               source-tree sync)
#
# Atomicity: each top-level subdir (src/include/fonts/icons) is staged
# to a sibling `*.new` directory, then atomically swapped in via mv.
# If the script is interrupted partway through, the live tree is
# either fully old or fully new for every subdir — never half-copied.
#
# Writes the current HEAD commit to libraries/libriplib/SYNC_REF.

set -euo pipefail

DRY_RUN=0
if [ "${1:-}" = "--dry-run" ]; then
    DRY_RUN=1
    shift
fi

if [ $# -ne 1 ]; then
    echo "usage: $0 [--dry-run] PATH_TO_A2GSPU_REPO" >&2
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

# Track staged temp directories so a SIGINT/SIGTERM mid-sync leaves no
# orphan *.new directories in the destination.
STAGED=()
cleanup_on_signal() {
    if [ ${#STAGED[@]} -gt 0 ]; then
        echo
        echo "interrupted — cleaning up ${#STAGED[@]} staged temp dir(s)" >&2
        for d in "${STAGED[@]}"; do
            [ -d "$d" ] && rm -rf "$d"
        done
    fi
    exit 130
}
trap cleanup_on_signal INT TERM

if [ "$DRY_RUN" = "1" ]; then
    echo "DRY RUN — would sync riplib -> $DST"
else
    echo "Syncing riplib -> $DST"
fi

for dir in src include fonts icons; do
    if [ ! -d "$RIPLIB_ROOT/$dir" ]; then
        echo "error: missing $RIPLIB_ROOT/$dir" >&2
        exit 1
    fi
    if [ "$DRY_RUN" = "1" ]; then
        echo "  would replace $DST/$dir/  (from $RIPLIB_ROOT/$dir/)"
        continue
    fi
    echo "  $dir/"
    STAGE="$DST/$dir.new"
    STAGED+=("$STAGE")
    rm -rf "$STAGE"
    cp -r "$RIPLIB_ROOT/$dir" "$STAGE"
    # Atomic swap: old directory aside, new one renamed in, old one wiped.
    if [ -d "$DST/$dir" ]; then
        mv "$DST/$dir" "$DST/$dir.old"
    fi
    mv "$STAGE" "$DST/$dir"
    rm -rf "$DST/$dir.old"
    # Remove from the cleanup list now that swap succeeded.
    unset 'STAGED[${#STAGED[@]}-1]'
done

# Record the source ref so future syncs and parity checks know
# what point in riplib history this copy represents.
ref="$(git -C "$RIPLIB_ROOT" rev-parse HEAD)"

if [ "$DRY_RUN" = "1" ]; then
    echo
    echo "would write SYNC_REF: $ref"
    echo "(dry-run — no changes made)"
    exit 0
fi

echo "$ref" > "$DST/SYNC_REF"
echo
echo "SYNC_REF: $ref"
echo "Done.  Remember to commit the changes in the A2GSPU repo."
