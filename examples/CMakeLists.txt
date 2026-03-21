add_executable(riplib_demo demo.c platform_stubs.c)
target_link_libraries(riplib_demo PRIVATE riplib)
