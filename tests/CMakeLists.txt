find_package(yaml-cpp REQUIRED)

add_executable(ccompiler_tests Main.cpp Support/ASTVerifier.cpp
                               Support/DeclBindingTest.cpp)
target_link_libraries(ccompiler_tests PRIVATE ccompiler_codegen
                                              yaml-cpp::yaml-cpp)

include(cmake/CCCompilerTests.cmake)

add_subdirectory(AST)
add_subdirectory(CodeGen)
add_subdirectory(Include)
add_subdirectory(Diagnostics)
