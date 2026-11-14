cmake_minimum_required(VERSION 3.28)

project(ccompiler)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

add_library(ccompiler_frontend STATIC src/SourceManager.cpp)
target_include_directories(ccompiler_frontend PUBLIC ${PROJECT_SOURCE_DIR}/src)

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
