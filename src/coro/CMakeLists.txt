cmake_minimum_required(VERSION 3.12)

file(GLOB SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.h)

file(MAKE_DIRECTORY ${CMAKE_HEADER_OUTPUT_DIRECTORY})

set(OUTPUT "${CMAKE_HEADER_OUTPUT_DIRECTORY}/coro.h")

add_custom_command(
    OUTPUT "${CMAKE_HEADER_OUTPUT_DIRECTORY}/coro.h"  
    COMMAND to_single_header ARGS coro_single.h ${CMAKE_HEADER_OUTPUT_DIRECTORY}/coro.h 
    DEPENDS ${SOURCES} 
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Generating single header"
    VERBATIM
)

if (ENABLE_FIBER) 

add_custom_command(
    OUTPUT "${CMAKE_HEADER_OUTPUT_DIRECTORY}/coro_fiber.h"  
    COMMAND to_single_header ARGS coro_fiber.h ${CMAKE_HEADER_OUTPUT_DIRECTORY}/coro_fiber.h 
    DEPENDS ${SOURCES} 
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Generating single header (coro_fiber.h)"
    VERBATIM
)

set(OUTPUT ${OUTPUT} "${CMAKE_HEADER_OUTPUT_DIRECTORY}/coro_fiber.h")

endif()

add_custom_target(libcoro_single_header ALL DEPENDS ${OUTPUT})
add_dependencies(libcoro_single_header to_single_header)



