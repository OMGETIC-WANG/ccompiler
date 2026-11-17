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

function(add_frontend_diagnostic_case test_case file_stem)
  set(source_file "${CMAKE_CURRENT_SOURCE_DIR}/${file_stem}.c")
  set(expected_file "${CMAKE_CURRENT_SOURCE_DIR}/${file_stem}.error.yaml")
  ccompiler_require_test_file("${source_file}"
                              "source for Diagnostics.${test_case}")
  ccompiler_require_test_file("${expected_file}"
                              "structured diagnostics for ${test_case}")
  add_test(NAME "Diagnostics.${test_case}"
           COMMAND $<TARGET_FILE:ccompiler_tests> --diagnostics
                   "${source_file}" "${expected_file}")
  ccompiler_set_test_properties("Diagnostics.${test_case}"
                                "diagnostics;frontend")
endfunction()

add_frontend_diagnostic_case(Lex.UnknownCharacter UnknownCharacter)
add_frontend_diagnostic_case(Lex.InvalidExponent Exponent)
add_frontend_diagnostic_case(Preprocessor.ErrorDirective ErrorDirective)
add_frontend_diagnostic_case(Preprocessor.MissingIfdefIdentifier
                             MissingIfdefIdentifier)
add_frontend_diagnostic_case(Preprocessor.MissingIfndefIdentifier
                             MissingIfndefIdentifier)
add_frontend_diagnostic_case(Preprocessor.MissingUndefIdentifier
                             MissingUndefIdentifier)
add_frontend_diagnostic_case(Preprocessor.UnknownDirective UnknownDirective)
add_frontend_diagnostic_case(Parser.MissingColon MissingColon)
add_frontend_diagnostic_case(Parser.MultipleStatements MultipleStatements)
add_frontend_diagnostic_case(Parser.MultipleTopLevel MultipleTopLevel)
add_frontend_diagnostic_case(Parser.MultipleStorageClasses
                             ParserMultipleStorageClasses)
add_frontend_diagnostic_case(Parser.MissingSemicolon MissingSemicolon)
add_frontend_diagnostic_case(Parser.InvalidLongDouble ParserInvalidLongDouble)
add_frontend_diagnostic_case(Parser.AbstractDeclaratorName
                             AbstractDeclaratorName)
add_frontend_diagnostic_case(Parser.NestedRecovery NestedRecovery)
add_frontend_diagnostic_case(Parser.MissingBrace MissingBrace)
add_frontend_diagnostic_case(Sema.UnsupportedType UnsupportedType)
add_frontend_diagnostic_case(Parser.ExternParameter ParserExternParameter)
add_frontend_diagnostic_case(Parser.ExternField ParserExternField)
add_frontend_diagnostic_case(Parser.UnknownIdentifier UnknownIdentifier)
add_frontend_diagnostic_case(Parser.OutOfScopeBlock OutOfScopeBlock)
add_frontend_diagnostic_case(Parser.OutOfScopeFor OutOfScopeFor)
add_frontend_diagnostic_case(Sema.StringTooLong StringTooLong)
add_frontend_diagnostic_case(Sema.ReturnType SemaReturnType)
add_frontend_diagnostic_case(Sema.CallArgument SemaCallArgument)
add_frontend_diagnostic_case(Sema.ConstAssignment SemaConstAssignment)
add_frontend_diagnostic_case(Sema.ConstStructMember SemaConstStructMember)
add_frontend_diagnostic_case(Sema.CaseConstant SemaCaseConstant)
add_frontend_diagnostic_case(Sema.GlobalConstant SemaGlobalConstant)
add_frontend_diagnostic_case(Sema.LocalStatic SemaLocalStatic)
add_frontend_diagnostic_case(Sema.StaticBlockFunction ParserStaticBlockFunction)
add_frontend_diagnostic_case(Sema.ExternLocalInitializer
                             SemaExternLocalInitializer)
add_frontend_diagnostic_case(Sema.ExternLinkageConflict
                             SemaExternLinkageConflict)
add_frontend_diagnostic_case(Sema.ExternRedefinition SemaExternRedefinition)
add_frontend_diagnostic_case(Sema.ExternTypeConflict SemaExternTypeConflict)
add_frontend_diagnostic_case(Sema.VolatilePointerDiscard
                             SemaVolatilePointerDiscard)
add_frontend_diagnostic_case(Sema.RestrictFunctionPointer
                             SemaRestrictFunctionPointer)
add_diagnostic_case(Driver.MissingInput MissingInput)
add_diagnostic_case(Driver.UnknownOption UnknownOption)
add_diagnostic_case(Driver.MissingInclude MissingInclude)
add_diagnostic_case(Driver.InvalidOutputPath InvalidOutputPath)

ccompiler_add_self_test(Diagnostics.Formatting --diagnostic-self-test
                        "diagnostics;frontend")

add_test(
  NAME Diagnostics.ErrorGenerator
  COMMAND "${CCOMPILER_PYTHON_EXECUTABLE}" -m unittest discover -s tools/tests
          -p "test_*.py"
  WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
ccompiler_set_test_properties("Diagnostics.ErrorGenerator"
                              "diagnostics;frontend")
