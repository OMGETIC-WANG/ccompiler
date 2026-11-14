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

function(add_source_diagnostic_failure name source line column message)
  add_test(
    NAME "Diagnostics.${name}"
    COMMAND
      ${CMAKE_COMMAND} -DTEST_EXECUTABLE=$<TARGET_FILE:ccompiler>
      -DSOURCE_DIAGNOSTIC=ON
      -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/Diagnostics/${source}
      -DEXPECTED_LINE=${line} -DEXPECTED_COLUMN=${column}
      "-DEXPECTED_MESSAGE=${message}" -P
      ${CMAKE_CURRENT_SOURCE_DIR}/ExpectDiagnostic.cmake)
  set_tests_properties("Diagnostics.${name}"
                       PROPERTIES LABELS "diagnostics;frontend" TIMEOUT 15)
endfunction()

function(add_source_diagnostic_failures name source expected_file)
  add_test(
    NAME "Diagnostics.${name}"
    COMMAND
      ${CMAKE_COMMAND} -DTEST_EXECUTABLE=$<TARGET_FILE:ccompiler>
      -DSOURCE_DIAGNOSTIC=ON
      -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/Diagnostics/${source}
      -DEXPECTED_DIAGNOSTIC_FILE=${CMAKE_CURRENT_SOURCE_DIR}/Diagnostics/${expected_file}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/ExpectDiagnostic.cmake)
  set_tests_properties("Diagnostics.${name}"
                       PROPERTIES LABELS "diagnostics;frontend" TIMEOUT 15)
endfunction()

function(add_command_diagnostic_failure name expected_message)
  add_test(
    NAME "Diagnostics.${name}"
    COMMAND
      ${CMAKE_COMMAND} -DTEST_EXECUTABLE=$<TARGET_FILE:ccompiler>
      "-DTEST_ARGUMENTS=${ARGN}" "-DEXPECTED_STDERR=${expected_message}" -P
      ${CMAKE_CURRENT_SOURCE_DIR}/ExpectDiagnostic.cmake)
  set_tests_properties("Diagnostics.${name}" PROPERTIES LABELS
                                                        "diagnostics;frontend")
endfunction()

add_source_diagnostic_failure(Lex.UnknownCharacter UnknownCharacter.c 1 25
                              "Unknown character: '@'")
add_source_diagnostic_failure(Lex.InvalidExponent Exponent.c 1 2
                              "Expected decimal exponent after 'e' or 'E'")
add_source_diagnostic_failure(Preprocessor.UnknownDirective UnknownDirective.c
                              1 2 "Unexpected preprocessor name: unknown")
add_source_diagnostic_failure(Parser.MissingColon MissingColon.c 1 30
                              "Expected ':' in conditional expression")
add_source_diagnostic_failures(Parser.MultipleStatements MultipleStatements.c
                               MultipleStatements.stderr)
add_source_diagnostic_failures(Parser.MultipleTopLevel MultipleTopLevel.c
                               MultipleTopLevel.stderr)
add_source_diagnostic_failures(Parser.MissingSemicolon MissingSemicolon.c
                               MissingSemicolon.stderr)
add_source_diagnostic_failures(Parser.NestedRecovery NestedRecovery.c
                               NestedRecovery.stderr)
add_source_diagnostic_failures(Parser.MissingBrace MissingBrace.c
                               MissingBrace.stderr)
add_source_diagnostic_failure(Sema.UnsupportedType UnsupportedType.c 1 1
                              "volatile types are not supported yet")
add_source_diagnostic_failure(Codegen.UnknownIdentifier UnknownIdentifier.c 1
                              25 "Unknown variable or function: missing")
add_source_diagnostic_failure(
  Codegen.StringTooLong StringTooLong.c 1 16
  "Array size is smaller than the literal string size")

set(missing_diagnostic_source
    "${CMAKE_CURRENT_BINARY_DIR}/missing/diagnostic-input.c")
add_command_diagnostic_failure(
  Driver.MissingInput
  "ccompiler: error: Failed to open file: ${missing_diagnostic_source}"
  "${missing_diagnostic_source}")
add_command_diagnostic_failure(
  Driver.UnknownOption "ccompiler: error: Unknown argument: unknown-option"
  "--unknown-option")
set(valid_diagnostic_source
    "${CMAKE_CURRENT_SOURCE_DIR}/AST/Statements/Empty.c")
set(invalid_diagnostic_output
    "${CMAKE_CURRENT_BINARY_DIR}/missing-output/main.ll")
add_command_diagnostic_failure(
  Driver.InvalidOutputPath
  "ccompiler: error: Invalid output path: ${CMAKE_CURRENT_BINARY_DIR}/missing-output"
  "${valid_diagnostic_source}"
  -o
  "${invalid_diagnostic_output}")
add_test(NAME Diagnostics.Formatting COMMAND $<TARGET_FILE:ccompiler_tests>
                                             --diagnostic-self-test)
set_tests_properties(Diagnostics.Formatting PROPERTIES LABELS
                                                       "diagnostics;frontend")
