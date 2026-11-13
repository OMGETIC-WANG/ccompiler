if(NOT DEFINED TEST_EXECUTABLE
   OR NOT DEFINED SOURCE_FILE
   OR NOT DEFINED EXPECTED_FILE
   OR NOT DEFINED EXPECTED_TEXT)
  message(FATAL_ERROR "ExpectFailure.cmake requires all test arguments")
endif()

execute_process(
  COMMAND "${TEST_EXECUTABLE}" "${SOURCE_FILE}" "${EXPECTED_FILE}"
  RESULT_VARIABLE test_result
  OUTPUT_VARIABLE test_stdout
  ERROR_VARIABLE test_stderr)

if(test_result EQUAL 0)
  message(FATAL_ERROR "Expected nonzero exit code")
endif()

set(test_output "${test_stdout}\n${test_stderr}")
string(FIND "${test_output}" "${EXPECTED_TEXT}" match_index)
if(match_index EQUAL -1)
  message(
    FATAL_ERROR "Missing expected output '${EXPECTED_TEXT}':\n${test_output}")
endif()
