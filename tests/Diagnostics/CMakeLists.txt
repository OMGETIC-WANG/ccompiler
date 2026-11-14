function(add_diagnostic_case test_case file_stem)
  set(source_file "${CMAKE_CURRENT_SOURCE_DIR}/${file_stem}.c")
  set(args_file "${CMAKE_CURRENT_SOURCE_DIR}/${file_stem}.args")
  set(expected_stderr "${CMAKE_CURRENT_SOURCE_DIR}/${file_stem}.stderr")

  ccompiler_require_test_file("${expected_stderr}"
                              "stderr expectation for ${test_case}")
  if(NOT EXISTS "${source_file}")
    set(source_file "")
  endif()
  if(NOT EXISTS "${args_file}")
    set(args_file "")
  endif()
  if(NOT source_file AND NOT args_file)
    message(FATAL_ERROR "Diagnostic ${test_case} has neither source nor args")
  endif()

  ccompiler_add_expected_process_test(
    NAME
    "Diagnostics.${test_case}"
    EXECUTABLE
    "$<TARGET_FILE:ccompiler>"
    SOURCE_FILE
    "${source_file}"
    ARGS_FILE
    "${args_file}"
    EXPECTED_STDERR_FILE
    "${expected_stderr}"
    EXPECT_SUCCESS
    OFF
    LABELS
    "diagnostics;frontend")
endfunction()

add_diagnostic_case(Lex.UnknownCharacter UnknownCharacter)
add_diagnostic_case(Lex.InvalidExponent Exponent)
add_diagnostic_case(Preprocessor.ErrorDirective ErrorDirective)
add_diagnostic_case(Preprocessor.MissingIfdefIdentifier MissingIfdefIdentifier)
add_diagnostic_case(Preprocessor.MissingIfndefIdentifier
                    MissingIfndefIdentifier)
add_diagnostic_case(Preprocessor.MissingUndefIdentifier MissingUndefIdentifier)
add_diagnostic_case(Preprocessor.UnknownDirective UnknownDirective)
add_diagnostic_case(Parser.MissingColon MissingColon)
add_diagnostic_case(Parser.MultipleStatements MultipleStatements)
add_diagnostic_case(Parser.MultipleTopLevel MultipleTopLevel)
add_diagnostic_case(Parser.MissingSemicolon MissingSemicolon)
add_diagnostic_case(Parser.NestedRecovery NestedRecovery)
add_diagnostic_case(Parser.MissingBrace MissingBrace)
add_diagnostic_case(Sema.UnsupportedType UnsupportedType)
add_diagnostic_case(Parser.UnknownIdentifier UnknownIdentifier)
add_diagnostic_case(Parser.OutOfScopeBlock OutOfScopeBlock)
add_diagnostic_case(Parser.OutOfScopeFor OutOfScopeFor)
add_diagnostic_case(Sema.StringTooLong StringTooLong)
add_diagnostic_case(Sema.ReturnType SemaReturnType)
add_diagnostic_case(Sema.CallArgument SemaCallArgument)
add_diagnostic_case(Sema.ConstAssignment SemaConstAssignment)
add_diagnostic_case(Sema.CaseConstant SemaCaseConstant)
add_diagnostic_case(Sema.GlobalConstant SemaGlobalConstant)
add_diagnostic_case(Sema.LocalStatic SemaLocalStatic)
add_diagnostic_case(Driver.MissingInput MissingInput)
add_diagnostic_case(Driver.UnknownOption UnknownOption)
add_diagnostic_case(Driver.MissingInclude MissingInclude)
add_diagnostic_case(Driver.InvalidOutputPath InvalidOutputPath)

ccompiler_add_self_test(Diagnostics.Formatting --diagnostic-self-test
                        "diagnostics;frontend")
