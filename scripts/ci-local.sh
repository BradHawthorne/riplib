#!/usr/bin/env bash
#
# Run the CI workflow's jobs locally, as closely as this machine allows.
#
# Written because the pipeline gained a job that had never been observed
# green -- the doc checks were added, pushed, and taken on trust.  A CI
# step nobody has watched run is a claim, not a check.
#
# This does NOT emulate GitHub Actions (no act, no Docker here).  It runs
# each job's actual commands against the local toolchain and reports
# PASS, FAIL, or SKIP-with-a-reason.  A SKIP is not a pass and is never
# counted as one: the summary prints skips separately and the exit code
# ignores them, so "3 passed, 4 skipped" cannot be misread as green.
#
#   scripts/ci-local.sh              run everything runnable
#   scripts/ci-local.sh build docs   run only the named jobs
#   scripts/ci-local.sh --list       show jobs and whether each can run
#
# Exit status is the number of FAILED jobs, so 0 means everything that
# ran, passed.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 99

# The DLL and the .RIP corpus are third-party and deliberately unvendored.
# Point these at a RIPtel install to enable the driver-backed checks.
RIPLIB_DLL="${RIPLIB_DLL:-C:/RIPtel/RIPSCRIP.DLL}"
RIPLIB_CORPUS="${RIPLIB_CORPUS:-C:/RIPtel}"

PASS=0; FAIL=0; SKIP=0
declare -a FAILED=() SKIPPED=()

say()  { printf '\n\033[1m== %s\033[0m\n' "$*"; }
ok()   { printf '   \033[32mPASS\033[0m  %s\n' "$*"; PASS=$((PASS+1)); }
bad()  { printf '   \033[31mFAIL\033[0m  %s\n' "$*"; FAIL=$((FAIL+1)); FAILED+=("$*"); }
skip() { printf '   \033[33mSKIP\033[0m  %s\n          reason: %s\n' "$1" "$2"
         SKIP=$((SKIP+1)); SKIPPED+=("$1 -- $2"); }

have() { command -v "$1" >/dev/null 2>&1; }

# On Windows, 'python3' is usually a Microsoft Store stub that is on PATH,
# resolves to an executable, and then refuses to run.  Being on PATH is not
# evidence that an interpreter works, so probe it.
find_python() {
    local c
    for c in python3 python py; do
        command -v "$c" >/dev/null 2>&1 || continue
        "$c" -c 'import sys; sys.exit(0)' >/dev/null 2>&1 && { echo "$c"; return 0; }
    done
    return 1
}
PY="$(find_python || true)"

# msys/cygwin gcc needs its own bin on PATH or cc1 cannot load its libs.
[ -d /c/msys64/usr/bin ] && PATH="/c/msys64/usr/bin:$PATH"
CC="${CC:-$(command -v gcc || true)}"

# CMake on Windows cannot resolve a POSIX-style compiler path: it is handed
# "/c/msys64/usr/bin/gcc", fails to find it, and reports only "tell CMake
# where to find the compiler", which reads like the compiler is missing
# rather than mis-spelled.  Convert when a converter exists.
if [ -n "$CC" ] && have cygpath; then
    CC="$(cygpath -m "$CC" 2>/dev/null || echo "$CC")"
    [ -f "$CC" ] || [ -f "$CC.exe" ] || CC="$(command -v gcc)"
fi

# Does this compiler actually have the sanitizer runtimes?  Presence of
# the -fsanitize flag proves nothing; the Cygwin build accepts the flag
# and then fails to link for want of libasan.
# The probe file goes under the repo, not /tmp.  Git Bash and a Cygwin
# gcc resolve /tmp to DIFFERENT directories -- the shell writes to
# %LOCALAPPDATA%\Temp and the compiler looks in C:\msys64\tmp -- so a
# /tmp path here silently fails to be found by the very tool it is for.
have_sanitizers() {
    local d="$ROOT/.ci-local-probe"
    mkdir -p "$d" || return 1
    printf 'int main(void){return 0;}\n' > "$d/p.c" || { rm -rf "$d"; return 1; }
    "$CC" -fsanitize=address,undefined -o "$d/p.exe" "$d/p.c" >/dev/null 2>&1
    local rc=$?; rm -rf "$d"; return $rc
}

job_wanted() {
    [ ${#WANTED[@]} -eq 0 ] && return 0
    local j; for j in "${WANTED[@]}"; do [ "$j" = "$1" ] && return 0; done
    return 1
}

# ---------------------------------------------------------------- jobs

job_build() {
    say "build  (matrix: Debug + Release)"
    [ -n "$CC" ] || { skip "build" "no C compiler on PATH"; return; }
    local bt
    for bt in Debug Release; do
        local d="build-ci-${bt,,}"
        rm -rf "$d"
        if ! cmake -B "$d" -G Ninja -DCMAKE_BUILD_TYPE="$bt" \
                -DCMAKE_C_COMPILER="$CC" -DRIPLIB_BUILD_TESTS=ON \
                -DBUILD_TESTING=ON -DRIPLIB_CORPUS_DIR="$RIPLIB_CORPUS" \
                >/dev/null 2>&1; then
            bad "build/$bt: configure failed"; continue
        fi
        if ! cmake --build "$d" >/dev/null 2>&1; then
            bad "build/$bt: compile failed"; continue
        fi
        if ctest --test-dir "$d" --output-on-failure >/dev/null 2>&1; then
            ok "build/$bt: configure, compile, ctest"
        else
            bad "build/$bt: ctest failed"
            ctest --test-dir "$d" --output-on-failure 2>&1 | tail -20
        fi
    done
}

job_sanitizers() {
    say "sanitizers  (UBSan + ASan)"
    if [ -z "$CC" ]; then skip "sanitizers" "no C compiler on PATH"; return; fi
    if ! have_sanitizers; then
        skip "sanitizers" \
             "this $( "$CC" -dumpmachine 2>/dev/null ) gcc has no libasan/libubsan -- the flag is accepted but the link fails. CI runs this on ubuntu-latest; it cannot be reproduced here."
        return
    fi
    rm -rf build-ci-san
    cmake -B build-ci-san -G Ninja -DCMAKE_C_COMPILER="$CC" \
          -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1" \
          -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" \
          -DRIPLIB_BUILD_TESTS=ON -DBUILD_TESTING=ON >/dev/null 2>&1 \
      && cmake --build build-ci-san >/dev/null 2>&1 \
      && ctest --test-dir build-ci-san --output-on-failure >/dev/null 2>&1 \
      && ok "sanitizers: build + ctest clean" \
      || bad "sanitizers: failed"
}

job_coverage() {
    say "coverage  (gcov per-file floors)"
    if [ -z "$CC" ] || ! have gcov; then
        skip "coverage" "gcov or a C compiler is missing"; return
    fi
    rm -rf build-ci-cov
    if ! cmake -B build-ci-cov -G Ninja -DCMAKE_C_COMPILER="$CC" \
            -DCMAKE_C_FLAGS="-fprofile-arcs -ftest-coverage -O0 -g" \
            -DCMAKE_EXE_LINKER_FLAGS="-fprofile-arcs -ftest-coverage" \
            -DRIPLIB_BUILD_TESTS=ON -DBUILD_TESTING=ON \
            -DRIPLIB_CORPUS_DIR="$RIPLIB_CORPUS" >/dev/null 2>&1 \
       || ! cmake --build build-ci-cov >/dev/null 2>&1; then
        bad "coverage: build failed"; return
    fi
    ctest --test-dir build-ci-cov --output-on-failure >/dev/null 2>&1
    # Same floors as .github/workflows/build.yml.  Kept in step manually;
    # the mismatch check below shouts if they drift apart.
    local names=(bgi_font drawing rip_icn rip_icons ripscrip ripscrip2
                 rip_preproc rip_variables rip_clipboard)
    local floors=(83 88 95 84 87 87 80 77 72)
    local i failed=0
    for i in "${!names[@]}"; do
        local f="${names[$i]}" floor="${floors[$i]}"
        local yml
        # Match the literal ']' with a bracket expression, not a backslash
        # escape.  GNU grep reads "\[" as a literal bracket; the msys/Cygwin
        # grep reads it as OPENING a character class, so "\[bgi_font\]"
        # matched any one of {b,g,i,_,f,o,n,t} and happily found "b=2048"
        # in an unrelated line -- reporting a floor mismatch that did not
        # exist.  "[]]" is the portable spelling and behaves the same in
        # both.
        yml=$(grep -oE "${f}[]]=[0-9]+" .github/workflows/build.yml | head -1 | cut -d= -f2)
        if [ -n "$yml" ] && [ "$yml" != "$floor" ]; then
            bad "coverage: floor for $f is $floor here but $yml in build.yml"
            failed=1; continue
        fi
        local out pct
        out=$(cd build-ci-cov && gcov -b -o CMakeFiles/riplib.dir/src \
                "CMakeFiles/riplib.dir/src/${f}.c.gcda" 2>&1)
        pct=$(echo "$out" | awk -v want="src/${f}.c" \
                '$0 ~ "File.*"want"'"'"'" {found=1; next} found && /^Lines executed/ {gsub(/[^0-9.]/,"",$2); print $2; exit}')
        if [ -z "$pct" ]; then
            bad "coverage: $f could not be parsed"; failed=1; continue
        fi
        if [ "${pct%.*}" -lt "$floor" ]; then
            bad "coverage: $f ${pct}% < floor ${floor}%"; failed=1
        else
            printf '          %-14s %6s%%  >= %s%%\n' "$f" "$pct" "$floor"
        fi
    done
    [ $failed -eq 0 ] && ok "coverage: every file at or above its floor"
}

job_embedded() {
    say "embedded-rp2350  (arm-none-eabi cross build)"
    have arm-none-eabi-gcc || {
        skip "embedded-rp2350" "arm-none-eabi-gcc not on PATH"; return; }
    rm -rf build-ci-pico2
    if ! cmake -B build-ci-pico2 -G Ninja \
            -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake >/dev/null 2>&1 \
       || ! cmake --build build-ci-pico2 >/dev/null 2>&1; then
        bad "embedded-rp2350: cross build failed"; return
    fi
    [ -f build-ci-pico2/libriplib.a ] || { bad "embedded-rp2350: no archive"; return; }
    local objs missing=0 f
    objs=$(ar t build-ci-pico2/libriplib.a | sort)
    for f in drawing bgi_font ripscrip ripscrip2 rip_icons rip_icn \
             rip_preproc rip_variables rip_clipboard riplib_version \
             rip_icons_data rip_icns_data; do
        echo "$objs" | grep -q "${f}\.c\.obj" || {
            bad "embedded-rp2350: missing ${f}.c.obj"; missing=1; }
    done
    [ $missing -eq 0 ] && ok "embedded-rp2350: archive holds all 12 units"
    if have arm-none-eabi-size; then
        printf '          archive total: %s\n' \
          "$(arm-none-eabi-size --totals build-ci-pico2/libriplib.a | tail -1 | tr -s ' ')"
    fi
}

job_branding() {
    say "branding"
    bash scripts/check-branding.sh --verbose >/dev/null 2>&1 \
        && ok "branding: no consumer names outside sanctioned locations" \
        || { bad "branding: leak found"; bash scripts/check-branding.sh --verbose 2>&1 | tail -10; }
}

job_docs() {
    say "command-docs"
    local py="$PY"
    [ -n "$py" ] || { skip "command-docs" "no working python interpreter"; return; }
    "$py" scripts/check-command-docs.py >/dev/null 2>&1 \
        && ok "check-command-docs.py" \
        || { bad "check-command-docs.py"; "$py" scripts/check-command-docs.py 2>&1 | tail -10; }
    # CI runs this WITHOUT the DLL, which is the mode that must stay green.
    "$py" scripts/check-spec-examples.py >/dev/null 2>&1 \
        && ok "check-spec-examples.py (no-DLL mode, as CI runs it)" \
        || { bad "check-spec-examples.py (no-DLL mode)"; "$py" scripts/check-spec-examples.py 2>&1 | tail -20; }
}

job_analyzer() {
    say "static-analysis  (-fanalyzer)"
    [ -n "$CC" ] || { skip "static-analysis" "no C compiler on PATH"; return; }
    rm -rf build-ci-analyzer
    if ! cmake -B build-ci-analyzer -G Ninja -DCMAKE_C_COMPILER="$CC" \
            -DCMAKE_C_FLAGS="-fanalyzer -O1" -DRIPLIB_BUILD_TESTS=ON \
            -DBUILD_TESTING=ON >/dev/null 2>&1; then
        bad "static-analysis: configure failed"; return
    fi
    cmake --build build-ci-analyzer > analyzer-local.log 2>&1
    if grep -qE '\-Wanalyzer-' analyzer-local.log; then
        bad "static-analysis: analyzer warnings"
        grep -E '\-Wanalyzer-' analyzer-local.log | head -10
    else
        ok "static-analysis: no -Wanalyzer- diagnostics"
        rm -f analyzer-local.log
    fi
}

# ------------------------------------------------------------- driver

job_driver_checks() {
    say "driver-backed checks  (not in CI -- the DLL is not vendored)"
    local py="$PY"
    if [ -z "$py" ]; then skip "driver-backed checks" "no working python interpreter"; return; fi
    if [ ! -f "$RIPLIB_DLL" ]; then
        skip "driver-backed checks" \
             "no RIPSCRIP.DLL at $RIPLIB_DLL (set RIPLIB_DLL=...).  These never run in CI either."
        return
    fi
    local s
    for s in "dll-conformance.py" "dll-validate-claims.py" "check-dll-table.py" \
             "check-spec-examples.py" "ref-compare.py" "check-field-names.py"; do
        if "$py" "scripts/$s" "$RIPLIB_DLL" >/dev/null 2>&1; then
            ok "$s"
        else
            bad "$s"; "$py" "scripts/$s" "$RIPLIB_DLL" 2>&1 | tail -12
        fi
    done
}

ALL=(build sanitizers coverage embedded branding docs analyzer driver_checks)

if [ "${1:-}" = "--list" ]; then
    echo "jobs: ${ALL[*]}"
    printf '\ntoolchain:\n'
    printf '  cc                %s\n' "${CC:-<none>}"
    printf '  sanitizers        %s\n' "$(have_sanitizers && echo available || echo 'UNAVAILABLE (no libasan/libubsan)')"
    printf '  gcov              %s\n' "$(have gcov && echo available || echo missing)"
    printf '  arm-none-eabi-gcc %s\n' "$(have arm-none-eabi-gcc && echo available || echo missing)"
    printf '  RIPSCRIP.DLL      %s\n' "$([ -f "$RIPLIB_DLL" ] && echo "$RIPLIB_DLL" || echo 'not found')"
    exit 0
fi

WANTED=("$@")
job_wanted build         && job_build
job_wanted sanitizers    && job_sanitizers
job_wanted coverage      && job_coverage
job_wanted embedded      && job_embedded
job_wanted branding      && job_branding
job_wanted docs          && job_docs
job_wanted analyzer      && job_analyzer
job_wanted driver_checks && job_driver_checks

say "summary"
printf '   passed  %d\n   failed  %d\n   skipped %d\n' "$PASS" "$FAIL" "$SKIP"
if [ ${#SKIPPED[@]} -gt 0 ]; then
    printf '\n   A SKIP IS NOT A PASS.  Not verified here:\n'
    printf '     - %s\n' "${SKIPPED[@]}"
fi
if [ ${#FAILED[@]} -gt 0 ]; then
    printf '\n   failures:\n'
    printf '     - %s\n' "${FAILED[@]}"
fi
exit "$FAIL"
