#pragma once

#include "AST.h"
#include "TypeContext.h"

class TranslationUnit : public ASTNode {
  ASTNodeImpl(TranslationUnit);
  TreeNodePrintFields(body);

public:
  std::unique_ptr<CompoundStmt> body;
};

std::unique_ptr<StructDecl> ParseStructDecl(Lexer &lexer, std::string_view name,
                                            CompileContext &context);

std::unique_ptr<UnionDecl> ParseUnionDecl(Lexer &lexer, std::string_view name,
                                          CompileContext &context);

std::unique_ptr<EnumDecl> ParseEnumDecl(Lexer &lexer, std::string_view name,
                                        CompileContext &context);

std::unique_ptr<TranslationUnit>
ParseTranslationUnit(Lexer &lexer, TypeContext &types, SourceManager &sources);
