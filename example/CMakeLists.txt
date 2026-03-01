cmake_minimum_required(VERSION 3.16)
project(vclg_example)

option(VCLG_BUILD_EXAMPLE "Build example." OFF)

if (VCLG_BUILD_EXAMPLE)
    add_executable(vclg_example main.cpp)
    target_link_libraries(vclg_example PRIVATE vclg)

    file(GLOB vclg_nodes "Nodes/*.vcl")

    foreach(file ${vclg_nodes})
        get_filename_component(filename "${file}" NAME)
        configure_file("Nodes/${filename}" "${CMAKE_CURRENT_BINARY_DIR}/Nodes/${filename}" COPYONLY)
    endforeach()
endif()