#include "AST/Declaration.h"
#include "AST/TranslationUnit.h"
#include "AST/Type.h"
#include "AST/TypeContext.h"
#include "CodeGen/LLVMIR/Generator.h"
#include "Lexer.h"
#include "Util/File.h"
#include <filesystem>
#include <fstream>
#include <iostream>

int main(int argc, char **argv) {
  std::string source;
  if (argc == 3) {
    source = ReadFileContent(argv[1]);
  } else {
    std::string line;
    while (std::getline(std::cin, line)) {
      if (line == ";;;") {
        break;
      }
      source += line;
      source += '\n';
    }
  }

  Lexer lexer(source);
  FilteredLexer filtered_lexer(lexer);
  TypeContext types;
  auto TU = ParseTranslationUnit(filtered_lexer, types);

  if (argc == 3) {
    std::string ir = GenerateLLVMIR(TU.get(), types);
    auto out_path = std::filesystem::path(argv[2]);
    std::ofstream out(out_path);
    out << ir;
    out.close();
  } else {
    TU->Print(std::cout);

    std::cout << "-----------------\n";

    std::string ir = GenerateLLVMIR(TU.get(), types);
    std::cout << ir;

    auto out_path = std::filesystem::current_path() / "out.ll";
    std::ofstream out(out_path);
    out << ir;
    out.close();

    std::cout << "-----------------\n" << std::flush;

    system(std::format("lli {}; echo -e \"\\nReturn: $?\\n\"",
                       std::filesystem::absolute(out_path).string())
               .c_str());
  }

  return 0;
}
