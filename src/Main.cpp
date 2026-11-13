#include "AST/Declaration.h"
#include "AST/TranslationUnit.h"
#include "AST/Type.h"
#include "AST/TypeContext.h"
#include "CodeGen/LLVMIR/Generator.h"
#include "CodeGen/LLVMIR/Intrinsics.h"
#include "JITRunner/JITRunner.h"
#include "Lex/Lexer.h"
#include "SourceManager.h"
#include "Util/CmdLine.h"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

static std::string PrintModule(llvm::Module &module) {
  std::string output;
  llvm::raw_string_ostream stream(output);
  module.print(stream, nullptr);
  stream.flush();
  return output;
}

CMDLINE_ARG(bool, show_ast, false);
CMDLINE_ARG(bool, show_tokens, false);
CMDLINE_ARG(bool, lex_only, false);
CMDLINE_ARG(bool, jit_run, false);
CMDLINE_ARG(std::string, output_file, "o", "");

int main(int argc, char **argv) {
  std::vector<std::string_view> rest = CmdLine::Parse(argc, argv);

  SourceManager source_manager;

  SourceHandle source;
  if (rest.size() > 0) {
    source = source_manager.LoadSourceFrom(std::string(rest[0]));
  } else {
    std::string s;
    std::string line;
    while (std::getline(std::cin, line)) {
      if (line == ";;;") {
        break;
      }
      s += line;
      s += '\n';
    }
    source = source_manager.LoadSource(std::move(s));
  }

  if (show_tokens || lex_only) {
    MacroContext macro_context;
    TypeContext types;
    CompileContext compile_context(types, source_manager);
    RawLexer raw_lexer(source, macro_context, source_manager, compile_context);
    Lexer lexer(raw_lexer);
    while (true) {
      Token tok = lexer.ConsumeNextToken();
      std::cout << EnumRefl<TokenType>::GetName(tok.type) << "]"
                << source_manager.GetText(tok) << "\n";
      if (tok.type == TokenType::EndOfFile) {
        break;
      }
    }
  }

  if (lex_only) {
    return 0;
  }

  RegisterBuiltinIntrinsics();

  MacroContext macro_context;
  TypeContext types;
  CompileContext compile_context(types, source_manager);
  RawLexer lexer(source, macro_context, source_manager, compile_context);
  Lexer filtered_lexer(lexer);
  auto TU = ParseTranslationUnit(filtered_lexer, types, source_manager);

  if (show_ast) {
    TU->Print(std::cout);
  }

  auto llvm_context = std::make_unique<llvm::LLVMContext>();
  auto module = GenerateLLVMModule(TU.get(), types, *llvm_context);
  std::string ir = PrintModule(*module);

  std::string verification_error;
  llvm::raw_string_ostream error_stream(verification_error);
  if (llvm::verifyModule(*module, &error_stream)) {
    std::cout << ir;
    error_stream.flush();
    RAISE_ERROR("Invalid LLVM module:\n", verification_error);
  }

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
