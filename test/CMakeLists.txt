cmake_minimum_required(VERSION 3.16)
project(vclg_test)

find_package(Catch2 3 REQUIRED)

add_executable(vclg_test
    Graph/Diagnostics.cpp
    Graph/Editing.cpp
    Graph/Elaboration.cpp
    Graph/Execution.cpp
    Graph/Inference.cpp
    Graph/Lifetime.cpp
    Graph/Mangling.cpp
    Graph/Order.cpp
    Graph/Subgraph.cpp
    Library/Smoke.cpp
)
target_link_libraries(vclg_test PRIVATE vclg Catch2::Catch2WithMain)
target_include_directories(vclg_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Test nodes and libraries are loaded relative to the test's working directory.
file(GLOB_RECURSE vclg_test_sources RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "Nodes/*.vcl" "Libraries/*.vcl")
foreach(file ${vclg_test_sources})
    configure_file("${file}" "${CMAKE_CURRENT_BINARY_DIR}/${file}" COPYONLY)
endforeach()
