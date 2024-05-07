cmake_minimum_required(VERSION 3.5)

link_libraries(${STANDARD_LIBRARIES})

add_executable (usecase_json main.cpp)
add_executable (usecase_parser_test parser_test.cpp no_inline.c)
