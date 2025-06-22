cmake_minimum_required(VERSION 3.16)
project(vcl)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/vcl)

add_library(vcl-graph
    STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Graph.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Node.cpp)
target_include_directories(vcl-graph PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

set_property(TARGET vcl-graph PROPERTY COMPILE_WARNING_AS_ERROR ON)

target_link_libraries(vcl-graph vcl)