if(NOT DEFINED TEST_EXECUTABLE)
  message(FATAL_ERROR "ExpectProcess.cmake requires TEST_EXECUTABLE")
endif()

foreach(
  variable
  TEST_SOURCE_FILE
  TEST_ARGS_FILE
  TEST_AST_EXPECTED_FILE
  TEST_OUTPUT_FILE
  TEST_EXPECTED_STDOUT_FILE
  TEST_EXPECTED_STDERR_FILE
  TEST_SOURCE_ROOT
  TEST_BINARY_ROOT
  TEST_JIT_RUN
  EXPECT_SUCCESS)
  if(NOT DEFINED ${variable})
    set(${variable} "")
  endif()
endforeach()

function(expand_test_placeholders input output_variable)
  set(value "${input}")
  string(REPLACE "@SOURCE_FILE@" "${TEST_SOURCE_FILE}" value "${value}")
  string(REPLACE "@SOURCE_ROOT@" "${TEST_SOURCE_ROOT}" value "${value}")
  string(REPLACE "@BINARY_ROOT@" "${TEST_BINARY_ROOT}" value "${value}")
  set("${output_variable}"
      "${value}"
      PARENT_SCOPE)
endfunction()

function(read_expected_output path output_variable)
  if(path)
    if(NOT EXISTS "${path}")
      message(FATAL_ERROR "Missing expected output file: ${path}")
    endif()
    file(READ "${path}" value)
  else()
    set(value "")
  endif()
  set("${output_variable}"
      "${value}"
      PARENT_SCOPE)
endfunction()

set(test_command "${TEST_EXECUTABLE}")

if(TEST_ARGS_FILE)
  if(NOT EXISTS "${TEST_ARGS_FILE}")
    message(FATAL_ERROR "Missing test args file: ${TEST_ARGS_FILE}")
  endif()
  file(STRINGS "${TEST_ARGS_FILE}" test_arguments ENCODING UTF-8)
  foreach(argument IN LISTS test_arguments)
    expand_test_placeholders("${argument}" argument)
    list(APPEND test_command "${argument}")
  endforeach()
endif()

if(TEST_SOURCE_FILE)
  list(APPEND test_command "${TEST_SOURCE_FILE}")
endif()

if(TEST_AST_EXPECTED_FILE)
  list(APPEND test_command "${TEST_AST_EXPECTED_FILE}")
endif()

if(TEST_OUTPUT_FILE)
  list(APPEND test_command -o "${TEST_OUTPUT_FILE}")
endif()

if(TEST_JIT_RUN)
  list(APPEND test_command -jit-run)
endif()

execute_process(
  COMMAND ${test_command}
  RESULT_VARIABLE test_result
  OUTPUT_VARIABLE test_stdout
  ERROR_VARIABLE test_stderr)

if(EXPECT_SUCCESS)
  if(NOT "${test_result}" STREQUAL "0")
    message(
      FATAL_ERROR
        "Expected success, got exit ${test_result}.\nstdout:\n${test_stdout}\nstderr:\n${test_stderr}"
    )
  endif()
else()
  if("${test_result}" STREQUAL "0")
    message(
      FATAL_ERROR
        "Expected failure, got exit 0.\nstdout:\n${test_stdout}\nstderr:\n${test_stderr}"
    )
  endif()
endif()

read_expected_output("${TEST_EXPECTED_STDOUT_FILE}" expected_stdout)
read_expected_output("${TEST_EXPECTED_STDERR_FILE}" expected_stderr)
expand_test_placeholders("${expected_stdout}" expected_stdout)
expand_test_placeholders("${expected_stderr}" expected_stderr)

string(STRIP "${test_stdout}" actual_stdout)
string(STRIP "${expected_stdout}" expected_stdout)
if(NOT "${actual_stdout}" STREQUAL "${expected_stdout}")
  message(
    FATAL_ERROR
      "Unexpected stdout.\nExpected:\n'${expected_stdout}'\nActual:\n'${test_stdout}'"
  )
endif()

string(STRIP "${test_stderr}" actual_stderr)
string(STRIP "${expected_stderr}" expected_stderr)
if(NOT "${actual_stderr}" STREQUAL "${expected_stderr}")
  message(
    FATAL_ERROR
      "Unexpected stderr.\nExpected:\n'${expected_stderr}'\nActual:\n'${test_stderr}'"
  )
endif()
