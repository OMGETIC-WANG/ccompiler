find_package(yaml-cpp REQUIRED)

set_source_files_properties("${CCOMPILER_ERROR_VERIFY_SOURCE}"
                            PROPERTIES GENERATED TRUE)
add_executable(
  ccompiler_tests
  Main.cpp
  Support/ASTVerifier.cpp
  Support/DeclBindingTest.cpp
  Support/ASTPrintTest.cpp
  Support/QualifierCodeGenTest.cpp
  Support/LongDoubleLoweringTest.cpp
  "${CCOMPILER_ERROR_VERIFY_SOURCE}")
add_dependencies(ccompiler_tests ccompiler_generate_errors)
target_include_directories(ccompiler_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ccompiler_tests PRIVATE ccompiler_codegen
                                              yaml-cpp::yaml-cpp)

include(cmake/CCCompilerTests.cmake)

add_subdirectory(AST)
add_subdirectory(Sema)
add_subdirectory(CodeGen)
add_subdirectory(Include)
add_subdirectory(Diagnostics)
