cmake_minimum_required(VERSION 3.28)

project(ccompiler)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

option(AST_PRINT_FIELD_NAMES "Print AST field name prefixes" ON)

add_library(ccompiler_frontend STATIC src/SourceManager.cpp)
target_include_directories(ccompiler_frontend PUBLIC ${PROJECT_SOURCE_DIR}/src)

if(AST_PRINT_FIELD_NAMES)
  set(AST_PRINT_FIELD_NAMES_DEFINE 1)
else()
  set(AST_PRINT_FIELD_NAMES_DEFINE 0)
endif()
target_compile_definitions(
  ccompiler_frontend
  PUBLIC AST_PRINT_FIELD_NAMES=${AST_PRINT_FIELD_NAMES_DEFINE})

add_executable(ccompiler src/Main.cpp)

add_subdirectory(src/Util)
add_subdirectory(src/AST)
add_subdirectory(src/Lex)
add_subdirectory(src/Sema)

add_subdirectory(src/CodeGen)

target_compile_options(ccompiler PRIVATE -Wall -Wreturn-type)
target_compile_options(ccompiler_frontend PRIVATE -Wall -Wreturn-type)
target_link_libraries(ccompiler PRIVATE ccompiler_frontend)

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
