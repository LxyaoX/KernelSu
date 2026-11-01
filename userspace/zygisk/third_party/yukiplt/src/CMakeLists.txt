# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Rouyashiki <https://rouyashiki.org>

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
find_package(Threads REQUIRED)
add_library(yukiplt ${YUKIPLT_LIBRARY_TYPE} ${YUKIPLT_SOURCES})
add_library(YukiPLT::yukiplt ALIAS yukiplt)
target_compile_features(yukiplt PUBLIC cxx_std_17)
set_target_properties(yukiplt PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden C_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
target_include_directories(yukiplt PUBLIC
    "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(yukiplt PRIVATE ${YUKIPLT_COMPILE_OPTIONS}
    "$<$<AND:$<CONFIG:Release>,$<COMPILE_LANG_AND_ID:CXX,Clang>>:-Oz>")
target_link_libraries(yukiplt PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
if(WIN32)
    target_link_libraries(yukiplt PRIVATE bcrypt)
endif()
if(YUKIPLT_LIBRARY_TYPE STREQUAL "STATIC")
    target_compile_definitions(yukiplt PUBLIC YUKIPLT_STATIC=1)
else()
    set_target_properties(yukiplt PROPERTIES LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/yukiplt.map")
    target_link_options(yukiplt PRIVATE -Wl,-z,max-page-size=16384 -Wl,-z,nodelete
        "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/yukiplt.map"
        -Wl,--exclude-libs,ALL -Wl,-Bsymbolic-functions)
endif()
install(TARGETS yukiplt EXPORT YukiPLTTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT production
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT production)
install(EXPORT YukiPLTTargets NAMESPACE YukiPLT::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/YukiPLT COMPONENT production)
configure_package_config_file("${PROJECT_SOURCE_DIR}/cmake/YukiPLTConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/YukiPLTConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/YukiPLT)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/YukiPLTConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY ExactVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/YukiPLTConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/YukiPLTConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/YukiPLT COMPONENT production)
install(FILES "${PROJECT_SOURCE_DIR}/include/yukiplt.h"
    "${PROJECT_SOURCE_DIR}/include/yukiplt.hpp"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT production)
install(FILES "${PROJECT_SOURCE_DIR}/LICENSE"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/YukiPLT COMPONENT production)
install(DIRECTORY "${PROJECT_SOURCE_DIR}/docs/"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/YukiPLT COMPONENT production FILES_MATCHING PATTERN "*.md")
