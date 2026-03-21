add_executable(test_drawing test_drawing.c)
target_link_libraries(test_drawing PRIVATE riplib)

enable_testing()
add_test(NAME drawing_tests COMMAND test_drawing)
