# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Rouyashiki <https://rouyashiki.org>

cmake_minimum_required(VERSION 3.22)
project(YukiPLTRegressions LANGUAGES CXX)
set(YUKIPLT_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
enable_testing()
if(CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "Use the consumer project for cross-compilation")
endif()
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Native regressions require Linux and Clang")
endif()
include(FetchContent)
set(BUILD_GMOCK OFF)
set(INSTALL_GTEST OFF)
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
    FIND_PACKAGE_ARGS)
FetchContent_MakeAvailable(googletest)
find_package(Threads REQUIRED)

add_library(yukiplt_regression_core STATIC
    "${YUKIPLT_ROOT}/src/c_api.cpp"
    "${YUKIPLT_ROOT}/src/c_metadata.cpp"
    "${YUKIPLT_ROOT}/src/c_rules.cpp"
    "${YUKIPLT_ROOT}/src/c_session.cpp"
    "${YUKIPLT_ROOT}/src/imports.cpp"
    "${YUKIPLT_ROOT}/src/yukiplt.cpp"
    "${YUKIPLT_ROOT}/src/elf.cpp"
    "${YUKIPLT_ROOT}/src/session.cpp"
    "${YUKIPLT_ROOT}/src/memory.cpp"
    "${YUKIPLT_ROOT}/src/maps_cache.cpp"
    "${YUKIPLT_ROOT}/src/fork.cpp"
    "${YUKIPLT_ROOT}/src/registry.cpp"
    "${YUKIPLT_ROOT}/src/rules.cpp"
    "${YUKIPLT_ROOT}/tests/overrides.cpp"
)
target_compile_features(yukiplt_regression_core PUBLIC cxx_std_17)
set_target_properties(yukiplt_regression_core PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(yukiplt_regression_core PUBLIC
    "${YUKIPLT_ROOT}/include" "${YUKIPLT_ROOT}/src")
target_compile_options(yukiplt_regression_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_definitions(yukiplt_regression_core PUBLIC YUKIPLT_STATIC=1)

include(GoogleTest)
add_executable(yukiplt_regressions elf_builder.cpp parser.cpp transaction.cpp lifecycle.cpp)
add_executable(yukiplt_allocation_regressions elf_builder.cpp allocation.cpp)
foreach(target IN ITEMS yukiplt_regressions yukiplt_allocation_regressions)
    target_link_libraries(${target} PRIVATE yukiplt_regression_core
        GTest::gtest_main Threads::Threads ${CMAKE_DL_LIBS})
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
    gtest_discover_tests(${target} PROPERTIES TIMEOUT 60)
endforeach()

# Fault injection wraps cross-unit runtime calls without production switches.
target_link_options(yukiplt_regression_core INTERFACE
    -Wl,--wrap=pthread_mutex_unlock
    -Wl,--wrap=pthread_key_create
    -Wl,--wrap=pthread_getspecific
    -Wl,--wrap=dl_iterate_phdr
    -Wl,--wrap=_ZN7yukiplt6detail10active_opsEv
    -Wl,--wrap=_ZN7yukiplt6detail23active_image_enumeratorEv)
