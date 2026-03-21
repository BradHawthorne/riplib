cmake_minimum_required(VERSION 3.13)
project(riplib VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)

# ── Library ─────────────────────────────────────────────────────────
add_library(riplib
    src/drawing.c
    src/bgi_font.c
    src/ripscrip.c
    src/ripscrip2.c
    src/rip_icons.c
    src/rip_icn.c
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
if(RIPLIB_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
