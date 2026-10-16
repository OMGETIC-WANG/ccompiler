#pragma once

#include "AST.h"

class TranslationUnit : public ASTNode {
  ASTNodeImpl(TranslationUnit);

public:
  size_t GetChildCount() const override { return body ? 1 : 0; }

  ASTNode *GetChild(size_t index) const override {
    return index == 0 ? body.get() : nullptr;
  }

  std::unique_ptr<CompoundStmt> body;
};

std::unique_ptr<ASTNode> ParseExpr(FilteredLexer &lexer, int min_prec = 0);

std::unique_ptr<TranslationUnit> ParseTranslationUnit(FilteredLexer &lexer);
