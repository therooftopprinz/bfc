cmake_minimum_required(VERSION 3.15)
project(bfc VERSION 2.0.0 LANGUAGES CXX)  # 2.0: a project bfc_config is required

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -ggdb3")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -Ofast -g")

add_compile_options(-Wall)

################################################################

add_library(bfc INTERFACE)

target_include_directories(bfc INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>)

# bfc has no default callback or reactor types: the top-level project provides
# <bfc_config.hpp> (see src/bfc/config.hpp) through a `bfc_config` INTERFACE
# target. Standing alone (tests) bfc supplies its own; as a subproject it
# requires the parent's, so one config governs the whole build.
if(NOT TARGET bfc_config)
    if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
        # BFC_TEST_CONFIG: counting (custom allocator type), std (std::allocator with
        # std::function) or bfc (std::allocator with bfc function types only).
        set(BFC_TEST_CONFIG "counting" CACHE STRING "bfc test config: counting, std or bfc")
        if(NOT BFC_TEST_CONFIG MATCHES "^(counting|std|bfc)$")
            message(FATAL_ERROR "BFC_TEST_CONFIG must be counting, std or bfc, not \"${BFC_TEST_CONFIG}\"")
        endif()
        if(BFC_TEST_CONFIG STREQUAL "bfc")
            set(bfc_test_config_dir tests/config_bfc)
        elseif(BFC_TEST_CONFIG STREQUAL "std")
            set(bfc_test_config_dir tests/config_std)
        else()
            set(bfc_test_config_dir tests/config)
        endif()
        add_library(bfc_config INTERFACE)
        target_include_directories(bfc_config INTERFACE
            ${CMAKE_CURRENT_SOURCE_DIR}/${bfc_test_config_dir})
    else()
        message(FATAL_ERROR
            "bfc needs a bfc_config target (an INTERFACE library whose include "
            "directory holds <bfc_config.hpp>); the top-level project defines it "
            "before adding bfc. See src/bfc/config.hpp.")
    endif()
endif()
target_link_libraries(bfc INTERFACE $<BUILD_INTERFACE:bfc_config>)

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

# The tests assume tests/config (std::function, epoll, counting allocator), so
# they are built only when bfc is the top-level project.
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    FILE(GLOB bfc_tests_SRC
        tests/ut/*.cpp
        tests/e2e/*.cpp
        tests/gtestmain.cpp)
    # Ensure new test files are included (GLOB may be cached)
    list(APPEND bfc_tests_SRC
        tests/ut/simple_shared_buffer_view_tests.cpp
        tests/ut/sized_buffer_tests.cpp
        tests/ut/metric_tests.cpp)
    list(REMOVE_DUPLICATES bfc_tests_SRC)

    add_executable(bfc_tests ${bfc_tests_SRC} gtest/gmock-gtest-all.cc)
    target_include_directories(bfc_tests PUBLIC bfc gtest)
    target_link_libraries(bfc_tests PRIVATE bfc pthread)
endif()
