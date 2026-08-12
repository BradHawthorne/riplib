if(NOT BUILD_TESTING)
    return()
endif()

add_executable(test_drawing test_drawing.c)
target_link_libraries(test_drawing PRIVATE riplib)

add_test(NAME drawing_tests COMMAND test_drawing)

add_executable(test_ripscrip test_ripscrip.c)
target_link_libraries(test_ripscrip PRIVATE riplib)

add_test(NAME ripscrip_tests COMMAND test_ripscrip)

# Write fixture path to a generated header (avoids platform-specific
# command-line quoting issues with -DSTRING_LITERAL=\"...\").
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/compat_fixture_path.h"
"#pragma once\n#define COMPAT_FIXTURE_DIR \"${CMAKE_CURRENT_SOURCE_DIR}/fixtures/compat\"\n")

add_executable(test_compat test_compat.c)
target_link_libraries(test_compat PRIVATE riplib)
target_include_directories(test_compat PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

add_test(NAME compat_tests COMMAND test_compat)

# ── Optional corpus replay ───────────────────────────────────────
# Replays authentic TeleGrafix .RIP scenes through the parser.  Those files
# are third-party content and are deliberately NOT vendored here; point the
# build at a local RIPterm/RIPtel installation to enable the test:
#
#     cmake -S . -B build -DRIPLIB_CORPUS_DIR=C:/RIPtel
#
# With no corpus configured the test still builds and runs, reporting SKIP,
# so a plain checkout stays green.
set(RIPLIB_CORPUS_DIR "" CACHE PATH
    "Directory of real .RIP scenes to replay in corpus_tests (optional)")

set(_corpus_scenes "")
if(RIPLIB_CORPUS_DIR)
    file(GLOB_RECURSE _corpus_files
         "${RIPLIB_CORPUS_DIR}/*.RIP" "${RIPLIB_CORPUS_DIR}/*.rip")
    # Case-insensitive filesystems match both globs for one file.
    list(REMOVE_DUPLICATES _corpus_files)
    foreach(_f IN LISTS _corpus_files)
        string(APPEND _corpus_scenes "    \"${_f}\",\n")
    endforeach()
    list(LENGTH _corpus_files _corpus_count)
    message(STATUS "Corpus replay: ${_corpus_count} scene(s) from ${RIPLIB_CORPUS_DIR}")
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/corpus_path.h"
"#pragma once\nstatic const char *const riplib_corpus_scenes[] = {\n${_corpus_scenes}    0\n};\n")

add_executable(test_corpus test_corpus.c)
target_link_libraries(test_corpus PRIVATE riplib)
target_include_directories(test_corpus PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

add_test(NAME corpus_tests COMMAND test_corpus)

# ── Seeded mutation fuzz ─────────────────────────────────────────
# Plain C99 with a fixed seed, so it runs everywhere the rest of the suite
# runs (fuzz_parser below needs clang + libFuzzer and stays optional).
# Takes an iteration count.  20k keeps ctest under ~10s in a Debug build;
# run it directly with a larger count for a deeper sweep:
#     ./build/tests/test_fuzz_seeded 1000000
add_executable(test_fuzz_seeded test_fuzz_seeded.c)
target_link_libraries(test_fuzz_seeded PRIVATE riplib)

add_test(NAME fuzz_seeded_tests COMMAND test_fuzz_seeded 20000)

# ── Optional libFuzzer harness ───────────────────────────────────
# Build with: cmake -DRIPLIB_BUILD_FUZZ=ON -DCMAKE_C_COMPILER=clang
#
# IMPORTANT: the `-fsanitize=fuzzer` flag is applied ONLY to the fuzz_parser
# target below, never as a global CMAKE_C_FLAGS.  The fuzzer runtime supplies
# its own main(), so putting `fuzzer` in the global flags makes CMake's
# compiler-probe test program ("does this compiler produce a working exe?")
# link two main()s → "multiple definition of `main`" → configure fails before
# anything is built.  Scope it to the target instead.  (address,undefined are
# safe to pass globally if desired, but fuzzer must be target-only.)
#
# The target is not registered with ctest because libFuzzer drives its own
# runner.  Run manually:  ./build/tests/fuzz_parser -max_total_time=60
if(RIPLIB_BUILD_FUZZ)
    add_executable(fuzz_parser fuzz_parser.c)
    target_link_libraries(fuzz_parser PRIVATE riplib)
    target_compile_options(fuzz_parser PRIVATE
        -fsanitize=fuzzer,address,undefined -g -O1)
    target_link_options(fuzz_parser PRIVATE
        -fsanitize=fuzzer,address,undefined)
endif()
