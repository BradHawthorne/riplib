cmake_minimum_required(VERSION 3.13)
project(riplib_consumer_smoke LANGUAGES C CXX)

# Exercise both supported integration paths from outside the library build.
if(RIPLIB_SOURCE_DIR)
    add_subdirectory("${RIPLIB_SOURCE_DIR}" riplib EXCLUDE_FROM_ALL)
else()
    find_package(riplib CONFIG REQUIRED)
endif()
add_executable(consumer main.cpp platform.c)
target_link_libraries(consumer PRIVATE riplib::riplib)
enable_testing()
add_test(NAME consumer_smoke COMMAND consumer)
