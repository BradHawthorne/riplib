cmake_minimum_required(VERSION 3.13)
project(riplib VERSION 1.3.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
# Only pull in CTest (and its Experimental/Nightly/… targets + BUILD_TESTING)
# when RIPlib is the top-level project.  A parent that add_subdirectory()'s
# RIPlib (the vendoring path) shouldn't inherit them (C-016).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(CTest)
endif()

# ── Version-header drift check ───────────────────────────────────────
# include/riplib_version.h is hand-maintained; assert it matches the
# project() version above so releases can't ship with a stale header.
# Bump both the project() line and the header at release time.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/include/riplib_version.h"
     _riplib_version_h)
string(REGEX MATCH "RIPLIB_VERSION_STRING +\"([0-9]+\\.[0-9]+\\.[0-9]+)\""
       _ "${_riplib_version_h}")
set(_riplib_header_version "${CMAKE_MATCH_1}")
if(NOT _riplib_header_version STREQUAL "${PROJECT_VERSION}")
    message(FATAL_ERROR
        "RIPlib version mismatch: CMakeLists.txt says ${PROJECT_VERSION}, "
        "but include/riplib_version.h says ${_riplib_header_version}. "
        "Update both in lockstep at release time.")
endif()
# Also assert the numeric MAJOR/MINOR/PATCH macros compose to the same
# version, so RIPLIB_VERSION_INT (used by consumers' #if guards) cannot
# silently disagree with the string (C-015).
string(REGEX MATCH "RIPLIB_VERSION_MAJOR[ \t]+([0-9]+)" _ "${_riplib_version_h}")
set(_riplib_vmaj "${CMAKE_MATCH_1}")
string(REGEX MATCH "RIPLIB_VERSION_MINOR[ \t]+([0-9]+)" _ "${_riplib_version_h}")
set(_riplib_vmin "${CMAKE_MATCH_1}")
string(REGEX MATCH "RIPLIB_VERSION_PATCH[ \t]+([0-9]+)" _ "${_riplib_version_h}")
set(_riplib_vpat "${CMAKE_MATCH_1}")
if(NOT "${_riplib_vmaj}.${_riplib_vmin}.${_riplib_vpat}" STREQUAL "${PROJECT_VERSION}")
    message(FATAL_ERROR
        "RIPlib version mismatch: numeric macros in riplib_version.h say "
        "${_riplib_vmaj}.${_riplib_vmin}.${_riplib_vpat} but project() says "
        "${PROJECT_VERSION}.  Update RIPLIB_VERSION_MAJOR/MINOR/PATCH in lockstep.")
endif()

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
add_library(riplib STATIC
    src/drawing.c
    src/bgi_font.c
    src/ripscrip.c
    src/ripscrip2.c
    src/rip_icons.c
    src/rip_icn.c
    src/rip_preproc.c
    src/rip_variables.c
    src/rip_clipboard.c
    src/riplib_version.c
    icons/rip_icons_data.c
    icons/rip_icns_data.c
)
add_library(riplib::riplib ALIAS riplib)

target_include_directories(riplib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/fonts>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/icons>
    $<INSTALL_INTERFACE:include>
)

if(NOT MSVC)
    # PUBLIC, not PRIVATE: riplib is a STATIC library that calls libm
    # (sinf/cosf/atan2f/sqrtf in drawing.c + ripscrip2.c).  A static archive
    # does not resolve its own external symbols — they stay undefined (nm shows
    # `U sinf` etc.) until the final executable link.  With PRIVATE the `m`
    # dependency would not propagate to consumers (tests, examples, downstream
    # apps), so their link fails with "undefined reference to sinf" under
    # gcc/clang.  MSVC gets math from its CRT and needs no -lm.
    target_link_libraries(riplib PUBLIC m)
endif()

# ── Optional: build example ─────────────────────────────────────────
option(RIPLIB_BUILD_EXAMPLES "Build example programs" OFF)
option(RIPLIB_BUILD_TESTS "Build test suite" OFF)
option(RIPLIB_BUILD_FUZZ "Build libFuzzer harness (requires clang + -fsanitize=fuzzer)" OFF)
option(RIPLIB_INSTALL "Generate install() rules and package config" ON)

if(RIPLIB_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(RIPLIB_BUILD_TESTS AND BUILD_TESTING)
    add_subdirectory(tests)
endif()

# ── Install rules + find_package() support ──────────────────────────
# Downstream consumers can either vendor the source tree (sync-style)
# OR install it system-wide and use:
#     find_package(riplib 1.2 REQUIRED)
#     target_link_libraries(my_app PRIVATE riplib::riplib)
#
# Turn off with -DRIPLIB_INSTALL=OFF when embedding RIPlib as a CMake
# subproject (e.g. via FetchContent or add_subdirectory) where install()
# rules would pollute the parent project's installed artifacts.
if(RIPLIB_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    install(TARGETS riplib
        EXPORT  riplibTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
    install(DIRECTORY include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.h"
    )
    install(DIRECTORY fonts/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.h"
    )
    install(DIRECTORY icons/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.h"
    )
    install(EXPORT riplibTargets
        FILE        riplibTargets.cmake
        NAMESPACE   riplib::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/riplib
    )

    configure_package_config_file(
        cmake/riplibConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/riplibConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/riplib
    )
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/riplibConfigVersion.cmake
        VERSION       ${PROJECT_VERSION}
        COMPATIBILITY SameMinorVersion
    )
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/riplibConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/riplibConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/riplib
    )
endif()
