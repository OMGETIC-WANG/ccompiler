cmake_minimum_required(VERSION 3.28)

project(ccompiler)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

add_executable(ccompiler src/Main.cpp src/Lexer.cpp)
add_subdirectory(src/Util)
add_subdirectory(src/AST)
