#pragma once

#include "AST.h"

class TranslationUnit : public ASTNode {
  ASTNodeImpl(TranslationUnit);
  TreeNodePrintFields(body);

public:
  std::unique_ptr<CompoundStmt> body;
};

std::unique_ptr<ASTNode> ParseExpr(FilteredLexer &lexer, int min_prec = 0);

std::unique_ptr<TranslationUnit> ParseTranslationUnit(FilteredLexer &lexer);
