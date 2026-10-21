#pragma once

#include "AST.h"
#include "TypeContext.h"

class TranslationUnit : public ASTNode {
  ASTNodeImpl(TranslationUnit);
  TreeNodePrintFields(body);

public:
  std::unique_ptr<CompoundStmt> body;
};

std::unique_ptr<StructDecl> ParseStructDecl(FilteredLexer &lexer,
                                            std::string_view name,
                                            CompileContext &context);

std::unique_ptr<EnumDecl> ParseEnumDecl(FilteredLexer &lexer,
                                        std::string_view name,
                                        CompileContext &context);

std::unique_ptr<TranslationUnit> ParseTranslationUnit(FilteredLexer &lexer,
                                                      TypeContext &types);
