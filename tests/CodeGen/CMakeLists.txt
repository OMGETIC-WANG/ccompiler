ccompiler_add_jit_case(CodeGen.DeclBindings DeclBindings.c "codegen;frontend")
ccompiler_add_jit_case(CodeGen.EnumShadowing EnumShadowing.c "codegen;frontend")
ccompiler_add_jit_case(CodeGen.Recursion Recursion.c "codegen;frontend")
ccompiler_add_jit_case(CodeGen.SemanticTypes SemanticTypes.c "codegen;frontend")
ccompiler_add_jit_case(CodeGen.ConstantFlow ConstantFlow.c "codegen;frontend")
ccompiler_add_jit_case(CodeGen.SizeofTypes SizeofTypes.c "codegen;frontend")
ccompiler_add_jit_case(CodeGen.Conversions Conversions.c "codegen;frontend")
ccompiler_add_jit_case(CodeGen.Qualifiers Qualifiers.c "codegen;frontend")
ccompiler_add_jit_case(CodeGen.LongDouble LongDouble.c "codegen;frontend")
ccompiler_add_jit_case(CodeGen.LongDoublePrecision LongDoublePrecision.c
                       "codegen;frontend")
ccompiler_add_self_test(CodeGen.QualifiedIR --qualified-ir-self-test
                        "codegen;frontend")
ccompiler_add_self_test(CodeGen.LongDoubleLowering
                        --long-double-lowering-self-test "codegen;frontend")
