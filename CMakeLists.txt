cmake_minimum_required(VERSION 3.16)
project(vclg)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(VCLG_BUILD_TEST "Build vcl-graph tests." OFF)
option(VCLG_SANITIZE "Build vcl-graph, vcl and the tests with AddressSanitizer and UndefinedBehaviorSanitizer." OFF)

if (VCLG_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/vcl)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/tlsf)

file(GLOB_RECURSE vclg_source_files "src/*.*")
add_library(vclg STATIC ${vclg_source_files})
target_include_directories(vclg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)


if(CMAKE_BUILD_TYPE EQUAL "Release")
    set_property(TARGET vclg PROPERTY COMPILE_WARNING_AS_ERROR ON)
endif()

target_link_libraries(vclg PUBLIC vcl tlsf)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/example)

if (VCLG_BUILD_TEST)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/test)
endif()