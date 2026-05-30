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
