#include "AST/Declaration.h"
#include "AST/TranslationUnit.h"
#include "AST/Type.h"
#include "Lexer.h"
#include <iostream>

int main() {
  std::string source;
  std::string line;
  while (std::getline(std::cin, line)) {
    if (line == ";;;") {
      break;
    }
    source += line;
    source += '\n';
  }

  // {
  //   Lexer lexer(source);
  //   while (lexer.PeekNextToken().type != TokenType::EndOfFile) {
  //     Token tok = lexer.ConsumeNextToken();
  //     std::cout << EnumRefl<TokenType>::GetName(tok.type) << " " << tok.value
  //               << "\n";
  //   }
  // }
  // std::cout << "------\n";

  Lexer lexer(source);
  FilteredLexer filtered_lexer(lexer);
  auto TU = ParseTranslationUnit(filtered_lexer);
  TU->Print(std::cout);

  // auto declaration = ParseDeclaration(filtered_lexer);
  // declaration->Print(std::cout);
  // auto types = ParseType(declaration.get());
  // for (const auto &type : types) {
  //   type->Print(std::cout);
  // }
  // auto expr = ParseExpr(filtered_lexer);
  // expr->Print(std::cout);
  return 0;
}
