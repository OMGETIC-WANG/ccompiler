include_guard(GLOBAL)

set(CCCOMPILER_EXPECT_PROCESS_SCRIPT
    "${CMAKE_CURRENT_LIST_DIR}/ExpectProcess.cmake")

function(ccompiler_require_test_file path description)
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "Missing ${description}: ${path}")
  endif()
endfunction()

function(ccompiler_set_test_properties test_name labels)
  set_tests_properties("${test_name}" PROPERTIES LABELS "${labels}" TIMEOUT 15)
endfunction()

function(ccompiler_add_ast_cases group)
  file(GLOB ast_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
  list(SORT ast_sources)

  foreach(ast_source IN LISTS ast_sources)
    string(REGEX REPLACE "\\.c$" ".ast.yaml" ast_expected "${ast_source}")
    ccompiler_require_test_file("${ast_expected}"
                                "AST expectation for ${ast_source}")

    get_filename_component(case_name "${ast_source}" NAME_WE)
    set(test_name "AST.${group}.${case_name}")
    add_test(NAME "${test_name}" COMMAND $<TARGET_FILE:ccompiler_ast_tests>
                                         "${ast_source}" "${ast_expected}")
    ccompiler_set_test_properties("${test_name}" "ast;frontend")
  endforeach()

  file(GLOB ast_expectations CONFIGURE_DEPENDS
       "${CMAKE_CURRENT_SOURCE_DIR}/*.ast.yaml")
  foreach(ast_expected IN LISTS ast_expectations)
    string(REGEX REPLACE "\\.ast\\.yaml$" ".c" ast_source "${ast_expected}")
    if(NOT EXISTS "${ast_source}")
      message(FATAL_ERROR "Orphan AST expectation without source: "
                          "${ast_expected}")
    endif()
  endforeach()
endfunction()

function(ccompiler_add_sema_cases group)
  file(GLOB sema_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
  list(SORT sema_sources)

  foreach(sema_source IN LISTS sema_sources)
    string(REGEX REPLACE "\\.c$" ".ast.yaml" sema_expected "${sema_source}")
    ccompiler_require_test_file("${sema_expected}"
                                "Sema expectation for ${sema_source}")

    get_filename_component(case_name "${sema_source}" NAME_WE)
    set(test_name "Sema.${group}.${case_name}")
    add_test(NAME "${test_name}" COMMAND $<TARGET_FILE:ccompiler_sema_tests>
                                         "${sema_source}" "${sema_expected}")
    ccompiler_set_test_properties("${test_name}" "sema;frontend")
  endforeach()

  file(GLOB sema_expectations CONFIGURE_DEPENDS
       "${CMAKE_CURRENT_SOURCE_DIR}/*.ast.yaml")
  foreach(sema_expected IN LISTS sema_expectations)
    string(REGEX REPLACE "\\.ast\\.yaml$" ".c" sema_source "${sema_expected}")
    if(NOT EXISTS "${sema_source}")
      message(FATAL_ERROR "Orphan Sema expectation without source: "
                          "${sema_expected}")
    endif()
  endforeach()
endfunction()

function(ccompiler_add_lexer_cases)
  set(aliases_file "${CMAKE_CURRENT_SOURCE_DIR}/.TokenTypeAliases")
  ccompiler_require_test_file("${aliases_file}" "Lexer token type aliases")

  file(GLOB lexer_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
  list(SORT lexer_sources)

  foreach(lexer_source IN LISTS lexer_sources)
    string(REGEX REPLACE "\\.c$" ".tokens" lexer_expected "${lexer_source}")
    string(REGEX REPLACE "\\.c$" ".args" lexer_args_file "${lexer_source}")
    ccompiler_require_test_file("${lexer_expected}"
                                "Lexer expectation for ${lexer_source}")

    set(lexer_args)
    if(EXISTS "${lexer_args_file}")
      file(STRINGS "${lexer_args_file}" lexer_args ENCODING UTF-8)
      list(TRANSFORM lexer_args REPLACE "@SOURCE_FILE@" "${lexer_source}")
      list(TRANSFORM lexer_args REPLACE "@SOURCE_ROOT@" "${PROJECT_SOURCE_DIR}")
      list(TRANSFORM lexer_args REPLACE "@BINARY_ROOT@"
                                        "${CMAKE_CURRENT_BINARY_DIR}")
    endif()

    get_filename_component(case_name "${lexer_source}" NAME_WE)
    set(test_name "Lexer.${case_name}")
    add_test(NAME "${test_name}"
             COMMAND $<TARGET_FILE:ccompiler_lexer_tests> "${lexer_source}"
                     "${lexer_expected}" "${aliases_file}" ${lexer_args})
    ccompiler_set_test_properties("${test_name}" "lexer;frontend")
  endforeach()

  file(GLOB lexer_expectations CONFIGURE_DEPENDS
       "${CMAKE_CURRENT_SOURCE_DIR}/*.tokens")
  foreach(lexer_expected IN LISTS lexer_expectations)
    string(REGEX REPLACE "\\.tokens$" ".c" lexer_source "${lexer_expected}")
    if(NOT EXISTS "${lexer_source}")
      message(FATAL_ERROR "Orphan lexer expectation without source: "
                          "${lexer_expected}")
    endif()
  endforeach()

  file(GLOB lexer_args_files CONFIGURE_DEPENDS
       "${CMAKE_CURRENT_SOURCE_DIR}/*.args")
  foreach(lexer_args_file IN LISTS lexer_args_files)
    string(REGEX REPLACE "\\.args$" ".c" lexer_source "${lexer_args_file}")
    if(NOT EXISTS "${lexer_source}")
      message(FATAL_ERROR "Orphan lexer args without source: "
                          "${lexer_args_file}")
    endif()
  endforeach()
endfunction()

function(ccompiler_add_expected_process_test)
  cmake_parse_arguments(
    CASE
    ""
    "NAME;EXECUTABLE;SOURCE_FILE;ARGS_FILE;AST_EXPECTED_FILE;OUTPUT_FILE;BINARY_ROOT;EXPECTED_STDOUT_FILE;EXPECTED_STDERR_FILE;EXPECT_SUCCESS;JIT_RUN"
    "LABELS"
    ${ARGN})

  if(NOT CASE_NAME
     OR NOT CASE_EXECUTABLE
     OR NOT DEFINED CASE_EXPECT_SUCCESS)
    message(FATAL_ERROR "ccompiler_add_expected_process_test requires NAME, "
                        "EXECUTABLE, and EXPECT_SUCCESS")
  endif()

  foreach(file_variable SOURCE_FILE ARGS_FILE AST_EXPECTED_FILE
                        EXPECTED_STDOUT_FILE EXPECTED_STDERR_FILE)
    if(CASE_${file_variable})
      ccompiler_require_test_file("${CASE_${file_variable}}"
                                  "${file_variable} for ${CASE_NAME}")
    endif()
  endforeach()

  if(NOT CASE_BINARY_ROOT)
    set(CASE_BINARY_ROOT "${CMAKE_CURRENT_BINARY_DIR}")
  endif()

  add_test(
    NAME "${CASE_NAME}"
    COMMAND
      "${CMAKE_COMMAND}" "-DTEST_EXECUTABLE=${CASE_EXECUTABLE}"
      "-DTEST_SOURCE_FILE=${CASE_SOURCE_FILE}"
      "-DTEST_ARGS_FILE=${CASE_ARGS_FILE}"
      "-DTEST_AST_EXPECTED_FILE=${CASE_AST_EXPECTED_FILE}"
      "-DTEST_OUTPUT_FILE=${CASE_OUTPUT_FILE}" "-DTEST_JIT_RUN=${CASE_JIT_RUN}"
      "-DTEST_EXPECTED_STDOUT_FILE=${CASE_EXPECTED_STDOUT_FILE}"
      "-DTEST_EXPECTED_STDERR_FILE=${CASE_EXPECTED_STDERR_FILE}"
      "-DEXPECT_SUCCESS=${CASE_EXPECT_SUCCESS}"
      "-DTEST_SOURCE_ROOT=${PROJECT_SOURCE_DIR}"
      "-DTEST_BINARY_ROOT=${CASE_BINARY_ROOT}" -P
      "${CCCOMPILER_EXPECT_PROCESS_SCRIPT}")
  ccompiler_set_test_properties("${CASE_NAME}" "${CASE_LABELS}")
endfunction()

function(ccompiler_add_jit_case test_name source_file labels)
  set(source_path "${CMAKE_CURRENT_SOURCE_DIR}/${source_file}")
  string(REGEX REPLACE "\\.c$" ".stdout" expected_stdout "${source_path}")
  string(REGEX REPLACE "\\.c$" ".args" args_file "${source_path}")

  ccompiler_require_test_file("${source_path}" "JIT source for ${test_name}")
  ccompiler_require_test_file("${expected_stdout}"
                              "stdout expectation for ${test_name}")
  if(NOT EXISTS "${args_file}")
    set(args_file "")
  endif()

  ccompiler_add_expected_process_test(
    NAME
    "${test_name}"
    EXECUTABLE
    "$<TARGET_FILE:ccompiler>"
    SOURCE_FILE
    "${source_path}"
    ARGS_FILE
    "${args_file}"
    OUTPUT_FILE
    "${CMAKE_CURRENT_BINARY_DIR}/${test_name}.ll"
    JIT_RUN
    ON
    EXPECTED_STDOUT_FILE
    "${expected_stdout}"
    EXPECT_SUCCESS
    ON
    LABELS
    "${labels}")
endfunction()
