find_package(yaml-cpp REQUIRED)
find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

set_source_files_properties("${CCOMPILER_ERROR_VERIFY_SOURCE}"
                            PROPERTIES GENERATED TRUE)

add_library(ccompiler_ast_test_support STATIC Support/ASTFixtureTest.cpp
                                              Support/ASTVerifier.cpp)
target_include_directories(ccompiler_ast_test_support
                           PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ccompiler_ast_test_support PUBLIC ccompiler_codegen
                                                        yaml-cpp::yaml-cpp)

add_library(ccompiler_lexer_test_support STATIC Support/LexerTest.cpp)
target_include_directories(ccompiler_lexer_test_support
                           PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ccompiler_lexer_test_support PUBLIC ccompiler_frontend)

add_library(
  ccompiler_diagnostic_test_support STATIC Diagnostics/DiagnosticFixtureTest.cpp
                                           "${CCOMPILER_ERROR_VERIFY_SOURCE}")
add_dependencies(ccompiler_diagnostic_test_support ccompiler_generate_errors)
target_include_directories(ccompiler_diagnostic_test_support
                           PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ccompiler_diagnostic_test_support
                      PUBLIC ccompiler_ast_test_support yaml-cpp::yaml-cpp)

add_executable(ccompiler_ast_tests AST/Main.cpp)
target_link_libraries(ccompiler_ast_tests PRIVATE ccompiler_ast_test_support)

add_executable(ccompiler_sema_tests Sema/Main.cpp)
target_link_libraries(ccompiler_sema_tests PRIVATE ccompiler_ast_test_support)

add_executable(ccompiler_lexer_tests Lexer/Main.cpp)
target_link_libraries(ccompiler_lexer_tests
                      PRIVATE ccompiler_lexer_test_support)

add_executable(ccompiler_diagnostics_tests Diagnostics/Main.cpp)
target_link_libraries(ccompiler_diagnostics_tests
                      PRIVATE ccompiler_diagnostic_test_support)

include(cmake/CCCompilerTests.cmake)

add_subdirectory(SelfTests)
add_subdirectory(AST)
add_subdirectory(Sema)
add_subdirectory(Lexer)
add_subdirectory(CodeGen)
add_subdirectory(Diagnostics)
