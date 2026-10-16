#pragma once

#include "Lexer.h"

class Parser {
public:
  Parser(Lexer &lexer) : _lexer(lexer) {}

private:
  Lexer &_lexer;
};
