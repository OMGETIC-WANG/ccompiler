#pragma once

#include "AST.h"
#include "TypeContext.h"

#include <memory>
#include <optional>
#include <vector>

class TranslationUnit : public ASTNode {
  ASTNodeImpl(TranslationUnit, ASTNode);
  AST_PRINT_ROLE_DECL(TranslationUnit);
  AST_PRINT_CHILDREN_DECL();

public:
  std::vector<std::unique_ptr<ASTNode>> decls;
  bool is_semantically_analyzed = false;
};

OptUnique<CompoundStmt> ParseCompoundStmt(Lexer &lexer,
                                          CompileContext &context);

OptUnique<TranslationUnit> ParseTranslationUnit(Lexer &lexer,
                                                TypeContext &types,
                                                SourceManager &sources,
                                                DiagnosticContext &diagnostics);
