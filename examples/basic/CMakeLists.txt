cmake_minimum_required(VERSION 3.16)
project(vclg_example_basic)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(basic ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)
target_link_libraries(basic PRIVATE vclg)

file(GLOB vcl_files "nodes/*.vcl")

foreach(file ${vcl_files})
    get_filename_component(filename "${file}" NAME)
    configure_file("nodes/${filename}" "${CMAKE_CURRENT_BINARY_DIR}/nodes/${filename}" COPYONLY)
endforeach()