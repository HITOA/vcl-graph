cmake_minimum_required(VERSION 3.16)
project(vclg)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/vcl)

add_library(vclg
    STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMetadata.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/NodeProcessor.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/RenamingScope.hpp

        ${CMAKE_CURRENT_SOURCE_DIR}/src/Graph.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Node.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/NodeMetadata.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/NodeProcessor.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Port.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/RenamingScope.cpp)
target_include_directories(vclg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

set_property(TARGET vclg PROPERTY COMPILE_WARNING_AS_ERROR ON)

target_link_libraries(vclg vcl)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/examples)