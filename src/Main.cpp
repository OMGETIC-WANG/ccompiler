#include "AST/Declaration.h"
#include "AST/TranslationUnit.h"
#include "AST/Type.h"
#include "AST/TypeContext.h"
#include "CodeGen/LLVMIR/Generator.h"
#include "Lexer.h"
#include "Util/CmdLine.h"
#include "Util/File.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>
#include <string>

static std::string PrintModule(llvm::Module &module) {
  std::string output;
  llvm::raw_string_ostream stream(output);
  module.print(stream, nullptr);
  stream.flush();
  return output;
}

CMDLINE_ARG(bool, show_ast, false);
CMDLINE_ARG(std::string, output_file, "o", "");

int main(int argc, char **argv) {
  std::vector<std::string_view> rest_args;
  try {
    rest_args = CmdLine::Parse(argc, argv);
  } catch (const CmdLineArgParseError &e) {
    std::println(std::cerr, "Failed to parse command line arguments: {}",
                 e.what());
    return 1;
  } catch (const std::exception &e) {
    std::println(std::cerr, "Error: {}", e.what());
    return 1;
  }

  std::string source;
  if (rest_args.size()) {
    source = ReadFileContent(std::string(rest_args[0]));
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

  if (show_ast) {
    TU->Print(std::cout);
  }

  llvm::LLVMContext llvm_context;
  auto module = GenerateLLVMModule(TU.get(), types, llvm_context);
  std::string ir = PrintModule(*module);

  if (output_file_declared) {
    auto out_path = std::filesystem::path(output_file);
    if (!std::filesystem::exists(out_path.parent_path())) {
      std::cerr << "Invalid output path: " << out_path.parent_path()
                << std::endl;
      return 1;
    }
    std::ofstream out(out_path);
    out << ir;
    out.close();
  } else {
    std::cout << ir;
  }

  return 0;
}
