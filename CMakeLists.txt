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
option(VCLG_CHECK_ALWAYS_WRITTEN "Check at run time that always-written outputs are written (a canary in each temporary output)." OFF)

if (VCLG_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/vcl)

add_library(vclg STATIC
    src/AST/ASTAutoParameterSubstitution.cpp
    src/AST/ASTInputConstWriter.cpp
    src/AST/ASTParameterWriter.cpp
    src/AST/ASTPortTypeOverrideWriter.cpp
    src/CodeGen/CodeGenFrame.cpp
    src/CodeGen/CodeGenGraph.cpp
    src/CodeGen/Migration.cpp
    src/CodeGen/Optimizer.cpp
    src/CodeGen/SlotPlanner.cpp
    src/Core/Diagnostics.cpp
    src/Core/SubstitutionTable.cpp
    src/Graph/BuiltinNodes.cpp
    src/Graph/Definition.cpp
    src/Graph/Directives.cpp
    src/Graph/Elaboration.cpp
    src/Graph/GraphContext.cpp
    src/Graph/GraphInstance.cpp
    src/Graph/Node.cpp
    src/Translation/NodeModel.cpp
    src/Translation/NodeRules.cpp
    src/Translation/SourceNodeCompilation.cpp
    src/Translation/StorageLowering.cpp
    src/Translation/Translation.cpp
)
target_include_directories(vclg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)


if(CMAKE_BUILD_TYPE EQUAL "Release")
    set_property(TARGET vclg PROPERTY COMPILE_WARNING_AS_ERROR ON)
endif()

target_link_libraries(vclg PUBLIC vcl)
if (VCLG_CHECK_ALWAYS_WRITTEN)
    target_compile_definitions(vclg PUBLIC VCLG_CHECK_ALWAYS_WRITTEN)
endif()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/example)

if (VCLG_BUILD_TEST)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/test)
endif()