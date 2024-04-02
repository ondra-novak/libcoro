cmake_minimum_required(VERSION 3.12)

file(GLOB SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.h)

file(MAKE_DIRECTORY ${CMAKE_HEADER_OUTPUT_DIRECTORY})


add_custom_command(
    OUTPUT "${CMAKE_HEADER_OUTPUT_DIRECTORY}/coro.h"  
    COMMAND to_single_header ARGS coro_single.h ${CMAKE_HEADER_OUTPUT_DIRECTORY}/coro.h 
    DEPENDS ${SOURCES} 
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Generating single header"
    VERBATIM
)


add_custom_target(libcoro_single_header ALL DEPENDS 
    "${CMAKE_HEADER_OUTPUT_DIRECTORY}/coro.h"
)
add_dependencies(libcoro_single_header to_single_header)



