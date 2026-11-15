target_sources(
  ccompiler_frontend
  PRIVATE AST.cpp
          ASTPrint.cpp
          Context.cpp
          Declaration.cpp
          TranslationUnit.cpp
          Type.cpp
          TypeContext.cpp
          Expr.cpp
          Intrinsic.cpp
          Operations.cpp)
