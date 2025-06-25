cmake_minimum_required(VERSION 3.16)
project(vclg_examples)

option(VCLG_BUILD_EXAMPLES "Build vcl graph examples." OFF)

if (VCLG_BUILD_EXAMPLES)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/basic")
endif()