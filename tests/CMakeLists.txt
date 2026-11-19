find_package(yaml-cpp REQUIRED)

set_source_files_properties("${CCOMPILER_ERROR_VERIFY_SOURCE}"
                            PROPERTIES GENERATED TRUE)
add_executable(
  ccompiler_tests
  Main.cpp
  Support/ASTVerifier.cpp
  Support/DeclBindingTest.cpp
  Support/ASTPrintTest.cpp
  Support/CmdLineTest.cpp
  Support/LexerTest.cpp
  Support/QualifierCodeGenTest.cpp
  Support/LongDoubleLoweringTest.cpp
  "${PROJECT_SOURCE_DIR}/src/Util/CmdLine.cpp"
  "${CCOMPILER_ERROR_VERIFY_SOURCE}")
add_dependencies(ccompiler_tests ccompiler_generate_errors)
target_include_directories(ccompiler_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ccompiler_tests PRIVATE ccompiler_codegen
                                              yaml-cpp::yaml-cpp)

include(cmake/CCCompilerTests.cmake)

ccompiler_add_self_test(CmdLine.Parsing --cmdline-self-test "diagnostics")

add_subdirectory(AST)
add_subdirectory(Sema)
add_subdirectory(Lexer)
add_subdirectory(CodeGen)
add_subdirectory(Diagnostics)

ccompiler_add_self_test(SourceManager.LineContinuations
                        --source-manager-self-test "frontend")
