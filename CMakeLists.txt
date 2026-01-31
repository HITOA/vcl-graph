cmake_minimum_required(VERSION 3.16)
project(vclg)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/vcl)

file(GLOB_RECURSE vclg_source_files "src/*/*.*")
add_library(vclg STATIC ${vclg_source_files})
target_include_directories(vclg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

set_property(TARGET vclg PROPERTY COMPILE_WARNING_AS_ERROR ON)

target_link_libraries(vclg vcl)