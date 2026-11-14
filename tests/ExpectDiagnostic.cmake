if(NOT DEFINED TEST_EXECUTABLE)
  message(FATAL_ERROR "ExpectDiagnostic.cmake requires TEST_EXECUTABLE")
endif()

if(SOURCE_DIAGNOSTIC)
  if(NOT DEFINED SOURCE_FILE
     OR NOT DEFINED EXPECTED_LINE
     OR NOT DEFINED EXPECTED_COLUMN
     OR NOT DEFINED EXPECTED_MESSAGE)
    message(FATAL_ERROR "Source diagnostic test is missing an argument")
  endif()

  execute_process(
    COMMAND "${TEST_EXECUTABLE}" "${SOURCE_FILE}"
    RESULT_VARIABLE test_result
    OUTPUT_VARIABLE test_stdout
    ERROR_VARIABLE test_stderr)

  cmake_path(ABSOLUTE_PATH SOURCE_FILE NORMALIZE OUTPUT_VARIABLE source_path)
  set(expected_stderr
      "${source_path}:${EXPECTED_LINE}:${EXPECTED_COLUMN}: error: ${EXPECTED_MESSAGE}\n"
  )
else()
  if(NOT DEFINED EXPECTED_STDERR)
    message(FATAL_ERROR "Command diagnostic test is missing EXPECTED_STDERR")
  endif()

  execute_process(
    COMMAND "${TEST_EXECUTABLE}" ${TEST_ARGUMENTS}
    RESULT_VARIABLE test_result
    OUTPUT_VARIABLE test_stdout
    ERROR_VARIABLE test_stderr)
  set(expected_stderr "${EXPECTED_STDERR}\n")
endif()

if(test_result EQUAL 0)
  message(FATAL_ERROR "Expected a nonzero exit code")
endif()

if(NOT test_stdout STREQUAL "")
  message(FATAL_ERROR "Expected empty stdout, got:\n${test_stdout}")
endif()

if(NOT test_stderr STREQUAL expected_stderr)
  message(
    FATAL_ERROR
      "Unexpected stderr.\nExpected:\n'${expected_stderr}'\nActual:\n'${test_stderr}'"
  )
endif()
