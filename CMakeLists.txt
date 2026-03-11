cmake_minimum_required(VERSION 3.15)
project(bfc VERSION 1.0.56 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -ggdb3")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -Ofast -g")

add_compile_options(-Wall)

################################################################

add_library(bfc INTERFACE)

target_include_directories(bfc INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>)

install(
    DIRECTORY src/bfc
    DESTINATION include
    FILES_MATCHING PATTERN "*.hpp")

install(
    TARGETS bfc
    EXPORT bfc
    INCLUDES DESTINATION include
)

include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/bfcConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/Config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/bfcConfig.cmake
    INSTALL_DESTINATION lib/cmake/bfc)

install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/bfcConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/bfcConfigVersion.cmake
    DESTINATION lib/cmake/bfc)

install(
    EXPORT bfc
    FILE bfcTargets.cmake
    NAMESPACE bfc::
    DESTINATION lib/cmake/bfc)

################################################################

FILE(GLOB bfc_tests_SRC
    tests/ut/*.cpp
    tests/e2e/*.cpp
    tests/gtestmain.cpp)

add_executable(bfc_tests ${bfc_tests_SRC} gtest/gmock-gtest-all.cc)
target_include_directories(bfc_tests PUBLIC bfc gtest)
target_link_libraries(bfc_tests PRIVATE bfc pthread)
