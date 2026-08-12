#!/usr/bin/env bash
# Enforce the platform-independence constraint (design/decisions.md).
#
# RIPlib must stay consumer-agnostic. Consumer-specific names must not appear
# in the library itself -- public headers, sources, examples, or public docs --
# outside a small set of sanctioned locations.
#
# This exists because the constraint had no automated guard: a 2026-06-30 scrub
# shipped believed-complete while leaving the material in place, because its
# verification grep was a no-op twice over --
#
#     git grep -in "A2FUSION|Processor.V"
#
# git grep defaults to BASIC regex, so '|' matched a literal pipe rather than
# alternation, and '.' cannot span the space-plus-quote in 'Processor "V"'.
# Always use -E (or -P) here.
#
# Usage:  scripts/check-branding.sh [--verbose]
# Exit:   0 = clean, 1 = violations found

set -uo pipefail
cd "$(git rev-parse --show-toplevel)" || exit 2

VERBOSE=0
[ "${1:-}" = "--verbose" ] && VERBOSE=1

# Forbidden consumer-specific names. Extended regex.
#
# NOTE: RP2350 / RP235XA / RP235XB are deliberately NOT here. They are
# Raspberry Pi's own public silicon and package designations, used
# legitimately by cmake/arm-none-eabi.cmake and by the portability claims.
# What is forbidden is the CARD's private role labels -- Processor "B" and
# Processor "V" -- which identify a specific board, not a chip.
FORBIDDEN='A2FUSION|A2GSPU|ProDOS|Processor +"[BV]"'

# Sanctioned locations, per design/decisions.md:
#   - README.md Origins / Reference target paragraphs
#   - cmake/arm-none-eabi.cmake (legitimately names the cross-compile target)
#   - design/ and consumer-handoff/ (decision record + extraction staging)
#   - docs/historical/ (preserved reverse-engineering record)
#   - scripts/*a2gspu* (consumer sync tooling, named for its target by design)
EXCLUDES=(
  ':!design/'
  ':!consumer-handoff/'
  ':!docs/historical/'
  ':!cmake/arm-none-eabi.cmake'
  ':!scripts/sync-to-a2gspu.sh'
  ':!scripts/check-a2gspu-parity.sh'
  ':!scripts/check-branding.sh'
  ':!.gitignore'
)

hits="$(git grep -nIE "$FORBIDDEN" -- . "${EXCLUDES[@]}" 2>/dev/null || true)"

# README.md carries two sanctioned paragraphs. Allow matches there only when
# the line is inside the Origins section or the Reference-target section.
filtered=""
while IFS= read -r line; do
    [ -z "$line" ] && continue
    file="${line%%:*}"
    if [ "$file" = "README.md" ]; then
        lineno="$(printf '%s' "$line" | cut -d: -f2)"
        # Sanctioned: the Origins paragraph (last section) only.
        origins_start="$(grep -n '^## Origins' README.md | cut -d: -f1)"
        if [ -n "$origins_start" ] && [ "$lineno" -ge "$origins_start" ]; then
            [ "$VERBOSE" -eq 1 ] && echo "  (allowed, Origins) $line"
            continue
        fi
    fi
    filtered="${filtered}${line}"$'\n'
done <<< "$hits"

filtered="$(printf '%s' "$filtered" | sed '/^$/d')"

if [ -n "$filtered" ]; then
    echo "FAIL: consumer-specific names outside sanctioned locations:"
    echo
    printf '%s\n' "$filtered" | sed 's/^/  /'
    echo
    echo "Sanctioned locations are listed in design/decisions.md."
    echo "If a new location is legitimate, amend the constraint WITH a"
    echo "decisions-log row -- not inline (see ADR discipline)."
    exit 1
fi

echo "OK: no consumer-specific names outside sanctioned locations."
[ "$VERBOSE" -eq 1 ] && echo "pattern: $FORBIDDEN"
exit 0
