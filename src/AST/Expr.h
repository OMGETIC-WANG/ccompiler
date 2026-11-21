#pragma once

#include "../Lex/Lexer.h"
#include "ASTNode.h"
#include "Context.h"

#include <optional>

std::optional<std::unique_ptr<ASTNode>>
ParseAssignmentExpr(Lexer &lexer, CompileContext &context);

std::optional<std::unique_ptr<ASTNode>> ParseExpr(Lexer &lexer,
                                                  CompileContext &context);
