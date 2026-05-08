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
