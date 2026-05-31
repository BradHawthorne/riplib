#!/usr/bin/env bash
#
# repo-status.sh - emit verified repo + CI ground truth in ONE corruption-resistant block.
#
# Why this exists (see design/knowledge.md HR-005): this dev environment
# intermittently corrupts tool output (blank / duplicated / stale reads), which
# has led to fabricated "pushed / CI-green" status claims. This script collapses
# the state checks an agent (or human) needs into a single command whose output is
# ASCII-only, one KEY=VALUE per line, and wrapped in BEGIN/END sentinels so a
# truncated or garbled capture is self-evidently incomplete (no END marker).
#
# It asserts nothing it cannot derive from a primitive: git for local/remote state,
# the GitHub Actions REST API (unauthenticated; no token needed) for CI. Anything
# it cannot determine is reported as UNKNOWN, never guessed.
#
# Usage:   bash scripts/repo-status.sh
# Output:  a >>>REPO-STATUS BEGIN ... >>>REPO-STATUS END block. If you do not see
#          the END line, the capture was truncated/garbled -- re-run, do not infer.
#
# Platform-independent dev tooling: pure git + curl, no project-specific logic.

set -u

emit() { printf '%s\n' "$1"; }

echo ">>>REPO-STATUS BEGIN"

# --- git ground truth -------------------------------------------------------
if ! git rev-parse --git-dir >/dev/null 2>&1; then
    emit "ERROR=not-a-git-repo"
    echo ">>>REPO-STATUS END"
    exit 1
fi

HEAD_FULL=$(git rev-parse HEAD 2>/dev/null || echo UNKNOWN)
HEAD_SHORT=$(git rev-parse --short HEAD 2>/dev/null || echo UNKNOWN)
BRANCH=$(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo UNKNOWN)
UPSTREAM=$(git rev-parse --abbrev-ref --symbolic-full-name '@{u}' 2>/dev/null || echo NONE)

emit "BRANCH=${BRANCH}"
emit "HEAD=${HEAD_SHORT}"
emit "HEAD_FULL=${HEAD_FULL}"
emit "UPSTREAM=${UPSTREAM}"

if [ "${UPSTREAM}" != "NONE" ]; then
    REMOTE_FULL=$(git rev-parse "${UPSTREAM}" 2>/dev/null || echo UNKNOWN)
    emit "REMOTE=$(git rev-parse --short "${UPSTREAM}" 2>/dev/null || echo UNKNOWN)"
    if [ "${HEAD_FULL}" = "${REMOTE_FULL}" ]; then
        emit "SYNCED=YES"
    else
        emit "SYNCED=NO"
    fi
    # ahead/behind relative to the upstream (left=behind, right=ahead)
    AB=$(git rev-list --left-right --count "${UPSTREAM}...HEAD" 2>/dev/null || echo "? ?")
    emit "BEHIND=$(echo "${AB}" | awk '{print $1}')"
    emit "AHEAD=$(echo "${AB}" | awk '{print $2}')"
else
    emit "SYNCED=UNKNOWN(no-upstream)"
fi

# tracked, uncommitted changes (excludes untracked .claude/ noise but counts other untracked)
DIRTY=$(git status --porcelain | grep -vc '^?? \.claude/' || true)
emit "DIRTY_ENTRIES=${DIRTY}"

# latest tag reachable + whether HEAD is exactly a tag
DESCRIBE=$(git describe --tags --always 2>/dev/null || echo UNKNOWN)
emit "DESCRIBE=${DESCRIBE}"

# --- CI ground truth (GitHub Actions, unauthenticated) ----------------------
# Derive owner/repo from the origin URL; query the Actions run for HEAD.
ORIGIN_URL=$(git config --get remote.origin.url 2>/dev/null || echo "")
SLUG=$(printf '%s' "${ORIGIN_URL}" | sed -E 's#^.*github\.com[:/]+##; s#\.git$##')

if [ -z "${SLUG}" ] || ! command -v curl >/dev/null 2>&1; then
    emit "CI=UNKNOWN(no-slug-or-no-curl)"
else
    API="https://api.github.com/repos/${SLUG}/actions/runs?head_sha=${HEAD_FULL}&per_page=1"
    JSON=$(curl -fsS -H 'Accept: application/vnd.github+json' \
                -H 'User-Agent: repo-status.sh' "${API}" 2>/dev/null || echo "")
    if [ -z "${JSON}" ]; then
        emit "CI=UNKNOWN(api-unreachable-or-rate-limited)"
    else
        # Minimal field scrape without jq: first total_count, first status/conclusion.
        TOTAL=$(printf '%s' "${JSON}" | grep -o '"total_count"[ ]*:[ ]*[0-9]*' | head -1 | grep -o '[0-9]*$')
        TOTAL=${TOTAL:-0}
        emit "CI_RUNS_FOR_HEAD=${TOTAL}"
        if [ "${TOTAL}" = "0" ]; then
            emit "CI=NO-RUN-YET"
        else
            ST=$(printf '%s' "${JSON}" | grep -o '"status"[ ]*:[ ]*"[a-z_]*"' | head -1 | sed -E 's/.*"([a-z_]*)"$/\1/')
            CC=$(printf '%s' "${JSON}" | grep -o '"conclusion"[ ]*:[ ]*[",a-z_]*' | head -1 | sed -E 's/.*: *//; s/"//g; s/,.*//')
            RID=$(printf '%s' "${JSON}" | grep -o '"id"[ ]*:[ ]*[0-9]*' | head -1 | grep -o '[0-9]*$')
            emit "CI_STATUS=${ST:-UNKNOWN}"
            emit "CI_CONCLUSION=${CC:-pending}"
            emit "CI_RUN_ID=${RID:-UNKNOWN}"
            emit "CI_NOTE=for-per-job-detail-query /actions/runs/${RID:-RUNID}/jobs"
        fi
    fi
fi

echo ">>>REPO-STATUS END"
