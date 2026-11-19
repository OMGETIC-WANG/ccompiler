add_executable(
  ccompiler_self_tests
  ASTPrintingTest.cpp
  ASTNodeTest.cpp
  CmdLineTest.cpp
  CodeGenTestSupport.cpp
  DeclarationBindingTest.cpp
  DiagnosticsFormattingTest.cpp
  LexerFixtureTest.cpp
  LongDoubleLoweringTest.cpp
  QualifiedCodeGenTest.cpp
  SourceManagerTest.cpp
  VerifierRegistryTest.cpp
  "${PROJECT_SOURCE_DIR}/src/Util/CmdLine.cpp")

target_include_directories(
  ccompiler_self_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests
                               ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
  ccompiler_self_tests
  PRIVATE ccompiler_codegen ccompiler_ast_test_support
          ccompiler_lexer_test_support ccompiler_diagnostic_test_support
          GTest::gtest_main yaml-cpp::yaml-cpp)

gtest_discover_tests(
  ccompiler_self_tests
  TEST_PREFIX "SelfTests."
  PROPERTIES
  TIMEOUT 15)

# Preserve all category labels after Google Test discovery.
set_property(
  DIRECTORY
  APPEND
  PROPERTY TEST_INCLUDE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/SetLabels.cmake")
