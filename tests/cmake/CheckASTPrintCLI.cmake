if(NOT TEST_EXECUTABLE
   OR NOT TEST_SOURCE
   OR NOT TEST_OUTPUT
   OR NOT DEFINED TEST_PRINT_FIELD_NAMES)
  message(FATAL_ERROR "AST print CLI test is missing required paths")
endif()

function(run_ast_print color_mode output_var result_var)
  execute_process(
    COMMAND "${TEST_EXECUTABLE}" --show-ast "--color=${color_mode}"
            "${TEST_SOURCE}" -o "${TEST_OUTPUT}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
  set("${output_var}"
      "${output}"
      PARENT_SCOPE)
  set("${result_var}"
      "${result}"
      PARENT_SCOPE)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "AST print failed in ${color_mode} mode:\n${error}")
  endif()
endfunction()

string(ASCII 27 escape)

run_ast_print(never plain_output plain_result)
string(FIND "${plain_output}" "${escape}" escape_position)
if(NOT escape_position EQUAL -1)
  message(FATAL_ERROR "AST output contains ANSI escapes in never mode")
endif()
if(NOT plain_output MATCHES "TranslationUnit 0x[0-9a-f]+")
  message(FATAL_ERROR "AST output is missing the root summary")
endif()
if(TEST_PRINT_FIELD_NAMES)
  if(NOT plain_output MATCHES
     "FunctionDecl .*name=no_args function_type=Func<int>")
    message(FATAL_ERROR "AST output is missing keyed declaration metadata")
  endif()
elseif(NOT plain_output MATCHES "FunctionDecl 0x[0-9a-f]+ no_args Func<int>")
  message(FATAL_ERROR "AST output is missing keyless declaration metadata")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env NO_COLOR=1 "${TEST_EXECUTABLE}" --show-ast
          --color=always "${TEST_SOURCE}" -o "${TEST_OUTPUT}"
  RESULT_VARIABLE colored_result
  OUTPUT_VARIABLE colored_output
  ERROR_VARIABLE colored_error)
if(NOT colored_result EQUAL 0)
  message(FATAL_ERROR "Colored AST print failed:\n${colored_error}")
endif()
if(NOT colored_output MATCHES
   "${escape}\\[38;2;0;204;0mTranslationUnit${escape}\\[0m")
  message(FATAL_ERROR "AST output is missing the unified root color")
endif()

execute_process(
  COMMAND "${TEST_EXECUTABLE}" --show-ast --color=invalid "${TEST_SOURCE}" -o
          "${TEST_OUTPUT}"
  RESULT_VARIABLE invalid_result
  OUTPUT_VARIABLE invalid_output
  ERROR_VARIABLE invalid_error)
if(invalid_result EQUAL 0)
  message(FATAL_ERROR "Invalid AST color mode was accepted")
endif()
if(NOT invalid_error MATCHES "Invalid --color value: invalid")
  message(FATAL_ERROR "Invalid AST color mode reported the wrong error")
endif()
