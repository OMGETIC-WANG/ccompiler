add_subdirectory(Declarations)
add_subdirectory(Expressions)
add_subdirectory(Initializers)
add_subdirectory(Intrinsics)
add_subdirectory(Pipeline)
add_subdirectory(Schema)
add_subdirectory(Statements)
add_subdirectory(Types)

ccompiler_add_self_test(AST.Printing --ast-print-self-test "ast;frontend")

add_test(
  NAME AST.PrintingCLI
  COMMAND
    "${CMAKE_COMMAND}" "-DTEST_EXECUTABLE=$<TARGET_FILE:ccompiler>"
    "-DTEST_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/Declarations/FunctionSignatures.c"
    "-DTEST_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/AST.PrintingCLI.ll"
    "-DTEST_PRINT_FIELD_NAMES=${AST_PRINT_FIELD_NAMES}" -P
    "${PROJECT_SOURCE_DIR}/tests/cmake/CheckASTPrintCLI.cmake")
ccompiler_set_test_properties(AST.PrintingCLI "ast;frontend")
