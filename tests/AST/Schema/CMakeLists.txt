function(add_ast_schema_failure test_case file_stem)
  set(expected_file "${CMAKE_CURRENT_SOURCE_DIR}/${file_stem}.ast.yaml")
  set(expected_stderr "${CMAKE_CURRENT_SOURCE_DIR}/${file_stem}.stderr")
  set(schema_source "${PROJECT_SOURCE_DIR}/tests/Support/SchemaFixture.c")

  ccompiler_require_test_file("${expected_file}"
                              "schema expectation for ${test_case}")
  ccompiler_require_test_file("${expected_stderr}"
                              "schema stderr for ${test_case}")
  ccompiler_require_test_file("${schema_source}" "schema fixture source")

  ccompiler_add_expected_process_test(
    NAME
    "AST.Schema.${test_case}"
    EXECUTABLE
    "$<TARGET_FILE:ccompiler_tests>"
    SOURCE_FILE
    "${schema_source}"
    AST_EXPECTED_FILE
    "${expected_file}"
    EXPECTED_STDERR_FILE
    "${expected_stderr}"
    EXPECT_SUCCESS
    OFF
    LABELS
    "ast;schema")
endfunction()

ccompiler_add_self_test(AST.Schema.VerifierRegistry --check-verifiers
                        "ast;schema")

add_ast_schema_failure(UnknownField unknown_field)
add_ast_schema_failure(WrongNode wrong_node)
add_ast_schema_failure(NonMapping non_mapping)
add_ast_schema_failure(SequenceLength sequence_length)
add_ast_schema_failure(ScalarMismatch scalar_mismatch)
add_ast_schema_failure(LiteralMismatch literal_mismatch)
add_ast_schema_failure(TypeTagMismatch type_tag_mismatch)
add_ast_schema_failure(UnknownTypeTag unknown_type_tag)
add_ast_schema_failure(NullMismatch null_mismatch)
