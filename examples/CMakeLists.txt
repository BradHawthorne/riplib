add_executable(riplib_demo demo.c platform_stubs.c)
target_link_libraries(riplib_demo PRIVATE riplib)

add_executable(rip2ppm rip2ppm.c platform_stubs.c)
target_link_libraries(rip2ppm PRIVATE riplib)
