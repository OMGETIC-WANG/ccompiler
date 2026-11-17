set(CODEGEN_JIT_CASES
    AggregatesPointers
    ConstantFlow
    Conversions
    ControlFlow
    DeclBindings
    EnumShadowing
    Functions
    LongDouble
    LongDoublePrecision
    Operators
    Qualifiers
    Recursion
    SemanticTypes
    SizeofTypes)

foreach(case_name IN LISTS CODEGEN_JIT_CASES)
  ccompiler_add_jit_case(CodeGen.${case_name} ${case_name}.c "codegen;frontend")
endforeach()

file(GLOB codegen_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
foreach(source IN LISTS codegen_sources)
  get_filename_component(case_name "${source}" NAME_WE)
  list(FIND CODEGEN_JIT_CASES "${case_name}" case_index)
  if(case_index EQUAL -1)
    message(
      FATAL_ERROR "CodeGen source is neither registered nor deferred: ${source}"
    )
  endif()
endforeach()

ccompiler_add_self_test(CodeGen.QualifiedIR --qualified-ir-self-test
                        "codegen;frontend")
ccompiler_add_self_test(CodeGen.LongDoubleLowering
                        --long-double-lowering-self-test "codegen;frontend")
