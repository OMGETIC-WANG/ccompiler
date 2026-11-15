#include "AST/Declaration.h"
#include "AST/TranslationUnit.h"
#include "AST/Type.h"
#include "AST/TypeContext.h"
#include "CodeGen/LLVMIR/Generator.h"
#include "CodeGen/LLVMIR/Intrinsics.h"
#include "JITRunner/JITRunner.h"
#include "Lex/Lexer.h"
#include "Sema/SemanticAnalysis.h"
#include "SourceManager.h"
#include "Util/CmdLine.h"
#include "Util/Diagnostic.h"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/Process.h>
#include <llvm/Support/raw_ostream.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

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
CMDLINE_ARG(bool, no_ir, false)
CMDLINE_ARG(std::string, output_file, "o", "");
CMDLINE_ARG(std::vector<std::string>, include_paths, "I", {});
CMDLINE_ARG(std::string, color, "auto");

static int Run(int argc, char **argv, SourceManager &source_manager,
               DiagnosticContext &diagnostics) {
  std::vector<std::string_view> rest = CmdLine::Parse(argc, argv);
  const auto color_mode = ParseASTPrintColorMode(color);
  if (!color_mode) {
    throw CmdLineArgParseError("Invalid --color value: " + color +
                               " (expected auto, always, or never)");
  }
  const char *no_color_env = std::getenv("NO_COLOR");
  const bool no_color_requested =
      no_color_env != nullptr && no_color_env[0] != '\0';
  const bool use_ast_color = ShouldEnableASTColor(
      *color_mode, llvm::sys::Process::StandardOutIsDisplayed(),
      no_color_requested);
  for (const std::string &include_path : include_paths) {
    source_manager.AddIncludePath(include_path);
  }

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
    CompileContext compile_context(types, source_manager, diagnostics);
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
  CompileContext compile_context(types, source_manager, diagnostics);
  RawLexer lexer(source, macro_context, source_manager, compile_context);
  Lexer filtered_lexer(lexer);
  auto TU =
      ParseTranslationUnit(filtered_lexer, types, source_manager, diagnostics);

  if (!diagnostics.HasErrors()) {
    AnalyzeTranslationUnit(*TU, types, diagnostics);
  }

  if (show_ast) {
    TU->Print(std::cout, {.use_color = use_ast_color});
  }

  if (diagnostics.HasErrors()) {
    std::cerr
        << DiagnosticError(diagnostics.Messages()).ToString(source_manager);
    return 1;
  }

  if (no_ir) {
    return 0;
  }

  auto llvm_context = std::make_unique<llvm::LLVMContext>();
  auto module = GenerateLLVMModule(TU.get(), types, *llvm_context, diagnostics);
  std::string ir = PrintModule(*module);

  std::string verification_error;
  llvm::raw_string_ostream error_stream(verification_error);
  if (llvm::verifyModule(*module, &error_stream)) {
    error_stream.flush();
    throw Exception("Invalid LLVM module:\n" + verification_error);
  }

  if (output_file.size()) {
    auto out_path = std::filesystem::path(output_file);
    if (!out_path.parent_path().empty() &&
        !std::filesystem::exists(out_path.parent_path())) {
      throw DriverError("Invalid output path: " +
                        out_path.parent_path().string());
    }
    std::ofstream out(out_path);
    if (!out.is_open()) {
      throw DriverError("Failed to open output file: " + out_path.string());
    }
    out << ir;
    if (!out) {
      throw DriverError("Failed to write output file: " + out_path.string());
    }
  } else {
    std::cout << ir;
  }

  if (jit_run) {
    std::cout.flush();
    auto result = RunLLVMModule(std::move(module), std::move(llvm_context));
    if (!result) {
      throw DriverError("JIT execution failed: " +
                        llvm::toString(result.takeError()));
    }
    std::cout << "Return: " << *result << '\n';
  }

  return 0;
}

int main(int argc, char **argv) {
  SourceManager source_manager;
  DiagnosticContext diagnostics(source_manager);

  try {
    return Run(argc, argv, source_manager, diagnostics);
  } catch (const DiagnosticError &error) {
    std::cerr << diagnostics.Format(error.Messages()) << '\n';
    return 1;
  } catch (const DriverError &error) {
    std::cerr << "ccompiler: error: " << error.what() << '\n';
    return 1;
  } catch (const CmdLineArgParseError &error) {
    std::cerr << "ccompiler: error: " << error.what() << '\n';
    return 1;
  } catch (const std::exception &error) {
    std::cerr << "ccompiler: internal compiler error: " << error.what() << '\n';
    return 1;
  } catch (...) {
    std::cerr << "ccompiler: internal compiler error: unknown exception\n";
    return 1;
  }
}
