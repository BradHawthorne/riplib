#!/usr/bin/env bash
# Diff this riplib working tree's library sources against the
# vendored copy inside an A2GSPU repo.  Prints any drift and exits
# non-zero if the trees differ.
#
# Usage:  scripts/check-a2gspu-parity.sh PATH_TO_A2GSPU_REPO
#
# Compares src/, include/, fonts/, icons/.  Ignores the A2GSPU-side-
# only files (platform_a2gspu.c, CMakeLists.txt, README.md, SYNC_REF).
#
# Diagnostic improvements over the original (per audit C-008):
#   - Files only-in-riplib are flagged "AHEAD (in riplib, not A2GSPU)"
#   - Files only-in-A2GSPU are flagged "BEHIND (in A2GSPU, not riplib)"
#   - Files in both that differ are flagged "DIVERGED"
#   - SYNC_REF mismatch is reported separately from content drift.

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
ahead_count=0
behind_count=0
diverged_count=0

for dir in src include fonts icons; do
    # diff -r emits "Only in <path>: <file>" for one-sided, "Files X and
    # Y differ" for two-sided differences.  We re-classify each line so
    # the operator knows which direction the drift is in.
    out=$(diff -r -q "$RIPLIB_ROOT/$dir" "$SRC_DST/$dir" 2>&1 || true)
    if [ -z "$out" ]; then continue; fi
    drift=1
    while IFS= read -r line; do
        case "$line" in
            "Only in $RIPLIB_ROOT/$dir"*)
                echo "  AHEAD     ${line#Only in $RIPLIB_ROOT/}"
                ahead_count=$((ahead_count + 1))
                ;;
            "Only in $SRC_DST/$dir"*)
                echo "  BEHIND    ${line#Only in $SRC_DST/}"
                behind_count=$((behind_count + 1))
                ;;
            "Files "*" differ")
                # Pull just the riplib-side path to keep output compact.
                file=$(echo "$line" | sed -E "s#^Files $RIPLIB_ROOT/([^ ]+) and .*#\\1#")
                echo "  DIVERGED  $file"
                diverged_count=$((diverged_count + 1))
                ;;
            *)
                echo "  $line"
                ;;
        esac
    done <<< "$out"
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
    echo "PARITY DRIFT detected:"
    echo "  ${ahead_count} file(s) AHEAD    (in riplib, not yet in A2GSPU)"
    echo "  ${behind_count} file(s) BEHIND   (in A2GSPU, not in riplib)"
    echo "  ${diverged_count} file(s) DIVERGED (different content on both sides)"
    echo
    echo "  - If riplib is the source of truth:"
    echo "      scripts/sync-to-a2gspu.sh $A2GSPU_ROOT"
    echo "      (use --dry-run first to preview)"
    echo "  - If A2GSPU has fixes that belong upstream: port them to riplib first,"
    echo "    commit there, then sync forward."
    exit 1
fi
