# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Rouyashiki <https://rouyashiki.org>

# This manifest is also usable by source-embedding hosts.
set(YUKIPLT_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/../src/c_metadata.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/c_rules.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/c_session.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/c_api.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/imports.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/yukiplt.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/elf.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/session.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/memory.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/maps_cache.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/fork.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/registry.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/rules.cpp"
)
