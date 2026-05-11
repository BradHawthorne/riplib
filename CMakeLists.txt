cmake_minimum_required(VERSION 3.13)
project(riplib VERSION 1.2.1 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
include(CTest)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    # RIPlib intentionally stores framebuffer geometry in 16-bit fields.
    # Keep the useful warning set, but suppress low-signal conversion noise
    # from bounded coordinate narrowing in the parser and renderer.
    # -Werror=implicit-function-declaration catches missing prototypes
    # (which on GCC <14 was only a warning) so they cannot slip into a release.
    add_compile_options(
        -Wall -Wextra
        -Wno-conversion -Wno-sign-conversion
        -Wno-unused-parameter
        -Werror=implicit-function-declaration
    )
endif()

# ── Library ─────────────────────────────────────────────────────────
add_library(riplib
    src/drawing.c
    src/bgi_font.c
    src/ripscrip.c
    src/ripscrip2.c
    src/rip_icons.c
    src/rip_icn.c
    icons/rip_icons_data.c
    icons/rip_icns_data.c
)

target_include_directories(riplib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/fonts
    ${CMAKE_CURRENT_SOURCE_DIR}/icons
)

if(NOT MSVC)
    target_link_libraries(riplib PRIVATE m)  # math library (not needed on MSVC)
endif()

# ── Optional: build example ─────────────────────────────────────────
option(RIPLIB_BUILD_EXAMPLES "Build example programs" OFF)
option(RIPLIB_BUILD_TESTS "Build test suite" OFF)

if(RIPLIB_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(RIPLIB_BUILD_TESTS AND BUILD_TESTING)
    add_subdirectory(tests)
endif()
