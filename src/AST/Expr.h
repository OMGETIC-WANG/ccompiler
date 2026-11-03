#pragma once

#include "../Lexer.h"
#include "ASTNode.h"
#include "Context.h"

std::unique_ptr<ASTNode> ParseExpr(FilteredLexer &lexer,
                                   CompileContext &context);
