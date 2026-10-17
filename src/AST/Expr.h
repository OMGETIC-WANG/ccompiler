#pragma once

#include "../Lexer.h"
#include "ASTNode.h"

std::unique_ptr<ASTNode> ParseExpr(FilteredLexer &lexer, int min_prec = 0);
