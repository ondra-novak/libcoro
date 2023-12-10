cmake_minimum_required(VERSION 3.1)
project (libcoro VERSION 1.0.0)
set (CMAKE_CXX_STANDARD 20)
if (MSVC)
	add_compile_options(/W4 /EHsc /DNOMINMAX)
	set(STANDARD_LIBRARIES "")
else()
	add_compile_options(-Wall -Wextra -Wpedantic)
	set(STANDARD_LIBRARIES "pthread")
endif()

if (CMAKE_COMPILER_IS_GNUCC AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 11.0)
    add_compile_options(-fcoroutines)
endif()

exec_program("git submodule update --init")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
	set(CMAKE_INSTALL_PREFIX "/usr/local" CACHE PATH "Default path to install" FORCE)
endif()
include(library.cmake)
#add_subdirectory("src/examples")
enable_testing()
add_subdirectory("src/tests")
add_subdirectory("src/tools")


# Install the header file to the include directory
install(FILES ${CMAKE_HEADER_OUTPUT_DIRECTORY}/coro.h DESTINATION ${CMAKE_INSTALL_PREFIX}/include/libcoro)
install(FILES ${CMAKE_HEADER_OUTPUT_DIRECTORY}/coro_commented.h DESTINATION ${CMAKE_INSTALL_PREFIX}/include/libcoro)

# Generate and install the Config.cmake file
include(CMakePackageConfigHelpers)
configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/Config.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/libcoroConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/cmake/libcoro
)

# Install the Config.cmake file
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libcoroConfig.cmake DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/cmake/libcoro)

# Install the version file
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/libcoroConfigVersion.cmake
  COMPATIBILITY SameMajorVersion
)

# Install the version file
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libcoroConfigVersion.cmake DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/cmake/libcoro)

