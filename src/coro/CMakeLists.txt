cmake_minimum_required(VERSION 3.12)

file(GLOB SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.h)

file(MAKE_DIRECTORY ${CMAKE_HEADER_OUTPUT_DIRECTORY})


add_custom_command(
    OUTPUT "${CMAKE_HEADER_OUTPUT_DIRECTORY}/coro_commented.h"  
    COMMAND ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/to_single_header ARGS coro_single.h ${CMAKE_HEADER_OUTPUT_DIRECTORY}/coro_commented.h 
    DEPENDS ${SOURCES} 
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Generating single header"
    VERBATIM
)

add_custom_command(
  OUTPUT ${CMAKE_HEADER_OUTPUT_DIRECTORY}/coro.h
  COMMAND "${CMAKE_C_COMPILER}" ARGS -Wno-pragmas -fpreprocessed -P -dD -E ${CMAKE_HEADER_OUTPUT_DIRECTORY}/coro_commented.h -o ${CMAKE_HEADER_OUTPUT_DIRECTORY}/coro.h  
  MAIN_DEPENDENCY ${CMAKE_HEADER_OUTPUT_DIRECTORY}/coro_commented.h
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMENT "Generating single header without comments"
  VERBATIM
)

# Vytvoření cíle pro spuštění vlastního příkazu
add_custom_target(single_header ALL DEPENDS "${CMAKE_HEADER_OUTPUT_DIRECTORY}/coro.h")
add_dependencies(single_header to_single_header)



