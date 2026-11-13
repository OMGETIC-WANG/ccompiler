find_package(yaml-cpp REQUIRED)

set(AST_SCHEMA_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/Support/SchemaFixture.c")

add_executable(ccompiler_tests Main.cpp Support/ASTVerifier.cpp)
target_link_libraries(ccompiler_tests PRIVATE ccompiler_codegen
                                              yaml-cpp::yaml-cpp)

function(add_ast_case ast_source)
  file(RELATIVE_PATH relative_source "${CMAKE_CURRENT_SOURCE_DIR}/AST"
       "${ast_source}")
  string(REGEX REPLACE "\\.c$" ".ast.yaml" ast_expected "${ast_source}")
  if(NOT EXISTS "${ast_expected}")
    message(FATAL_ERROR "Missing AST expectation for ${ast_source}: "
                        "${ast_expected}")
  endif()

  get_filename_component(case_name "${relative_source}" NAME_WE)
  get_filename_component(case_group "${relative_source}" DIRECTORY)
  if(case_group)
    string(REPLACE "/" "." case_group "${case_group}")
    set(test_name "AST.${case_group}.${case_name}")
  else()
    set(test_name "AST.${case_name}")
  endif()

  add_test(NAME "${test_name}" COMMAND $<TARGET_FILE:ccompiler_tests>
                                       "${ast_source}" "${ast_expected}")
  set_tests_properties("${test_name}" PROPERTIES LABELS "ast;frontend" TIMEOUT
                                                 15)
endfunction()

file(GLOB_RECURSE AST_CASE_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/AST/*.c")
list(SORT AST_CASE_SOURCES)
foreach(ast_source IN LISTS AST_CASE_SOURCES)
  add_ast_case("${ast_source}")
endforeach()

add_test(NAME AST.Schema.VerifierRegistry COMMAND $<TARGET_FILE:ccompiler_tests>
                                                  --check-verifiers)
set_tests_properties(AST.Schema.VerifierRegistry PROPERTIES LABELS "ast;schema"
                                                            TIMEOUT 15)

function(add_ast_schema_failure name expected_file expected_text)
  add_test(
    NAME "AST.Schema.${name}"
    COMMAND
      ${CMAKE_COMMAND} -DTEST_EXECUTABLE=$<TARGET_FILE:ccompiler_tests>
      -DSOURCE_FILE=${AST_SCHEMA_SOURCE}
      -DEXPECTED_FILE=${CMAKE_CURRENT_SOURCE_DIR}/AST/Schema/${expected_file}
      "-DEXPECTED_TEXT=${expected_text}" -P
      ${CMAKE_CURRENT_SOURCE_DIR}/ExpectFailure.cmake)
  set_tests_properties("AST.Schema.${name}" PROPERTIES LABELS "ast;schema"
                                                       TIMEOUT 15)
endfunction()

add_ast_schema_failure(UnknownField unknown_field.ast.yaml
                       "unknown field 'nmae'")
add_ast_schema_failure(WrongNode wrong_node.ast.yaml
                       "expected a single 'TranslationUnit' node descriptor")
add_ast_schema_failure(NonMapping non_mapping.ast.yaml
                       "expected a node descriptor mapping")
add_ast_schema_failure(SequenceLength sequence_length.ast.yaml
                       "expected 1 items for field 'decls', got 0")
add_ast_schema_failure(ScalarMismatch scalar_mismatch.ast.yaml
                       "for field 'name'")
add_ast_schema_failure(LiteralMismatch literal_mismatch.ast.yaml
                       "literal value does not match")
add_ast_schema_failure(TypeTagMismatch type_tag_mismatch.ast.yaml
                       "expected type tags [unsigned], got []")
add_ast_schema_failure(UnknownTypeTag unknown_type_tag.ast.yaml
                       "unknown type tag 'packed'")
add_ast_schema_failure(NullMismatch null_mismatch.ast.yaml "expected null")
