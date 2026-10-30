#include "AST/Declaration.h"
#include "AST/TranslationUnit.h"
#include "AST/Type.h"
#include "AST/TypeContext.h"
#include "CodeGen/LLVMIR/Generator.h"
#include "JITRunner/JITRunner.h"
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
CMDLINE_ARG(bool, jit_run, false);
CMDLINE_ARG(std::string, output_file, "o", "");

int main(int argc, char **argv) {
  std::vector<std::string_view> rest = CmdLine::Parse(argc, argv);

  std::string source;
  if (rest.size() > 0) {
    source = ReadFileContent(std::string(rest[0]));
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

  auto llvm_context = std::make_unique<llvm::LLVMContext>();
  auto module = GenerateLLVMModule(TU.get(), types, *llvm_context);
  std::string ir = PrintModule(*module);

  if (output_file.size()) {
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

  if (jit_run) {
    std::cout.flush();
    auto result = RunLLVMModule(std::move(module), std::move(llvm_context));
    if (!result) {
      std::cerr << "JIT execution failed: "
                << llvm::toString(result.takeError()) << '\n';
      return 1;
    }
    std::cout << "Return: " << *result << '\n';
  }

  return 0;
}
