# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Rouyashiki <https://rouyashiki.org>

cmake_minimum_required(VERSION 3.22)
project(YukiPLTConsumer LANGUAGES C CXX)
find_package(YukiPLT CONFIG REQUIRED)
add_executable(c_consumer main.c session_main.c)
set_target_properties(c_consumer PROPERTIES
    LINKER_LANGUAGE CXX C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_link_libraries(c_consumer PRIVATE YukiPLT::yukiplt ${CMAKE_DL_LIBS})
add_executable(cpp_consumer cpp_main.cpp)
set_target_properties(cpp_consumer PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_link_libraries(cpp_consumer PRIVATE YukiPLT::yukiplt ${CMAKE_DL_LIBS})

add_library(sample_provider SHARED provider.c)
add_library(hook_target SHARED target.c)
target_link_libraries(hook_target PRIVATE sample_provider)
foreach(fixture IN ITEMS sample_provider hook_target)
    target_link_options(${fixture} PRIVATE -Wl,-z,max-page-size=16384
        -Wl,-z,relro -Wl,-z,now -Wl,--hash-style=gnu)
endforeach()
add_dependencies(c_consumer hook_target)
add_dependencies(cpp_consumer hook_target)

enable_testing()
add_test(NAME c_consumer COMMAND c_consumer)
add_test(NAME cpp_consumer COMMAND cpp_consumer)

get_target_property(_yukiplt_type YukiPLT::yukiplt TYPE)
if(_yukiplt_type STREQUAL "STATIC_LIBRARY")
    add_library(fork_host SHARED fork_host.cpp)
    target_link_libraries(fork_host PRIVATE YukiPLT::yukiplt)
    target_link_options(fork_host PRIVATE -Wl,-z,max-page-size=16384
        -Wl,-Bsymbolic-functions -Wl,--exclude-libs,ALL)
    target_compile_definitions(cpp_consumer PRIVATE YUKIPLT_TEST_UNLOAD=1)
    add_dependencies(cpp_consumer fork_host)
endif()
