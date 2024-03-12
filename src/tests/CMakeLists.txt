set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/)

set(testFiles all_compile.cpp
               async_int.cpp
               async_void.cpp
               shared_future.cpp
               generator.cpp
               distributor.cpp
                )
foreach (testFile ${testFiles})
    string(REGEX MATCH "([^\/]+$)" filename ${testFile})
    string(REGEX MATCH "[^.]*" executable_name test_${filename})
    add_executable(${executable_name} ${testFile})
    add_dependencies(${executable_name} libcoro_single_header)
    target_link_libraries(${executable_name} ${STANDARD_LIBRARIES} )
    add_test(NAME ${executable_name} COMMAND ${executable_name})
endforeach ()
