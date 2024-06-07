cmake_minimum_required(VERSION 3.5)

link_libraries(${STANDARD_LIBRARIES})

if(NOT TARGET to_single_header)

add_executable (to_single_header to_single_header.cpp)

endif()


add_executable (vistrace vistrace.cpp)
