#include "Support/ASTFixtureTest.h"

#include "AST/TranslationUnit.h"
#include "AST/TypeContext.h"
#include "CodeGen/LLVMIR/Intrinsics.h"
#include "Lex/Lexer.h"
#include "Sema/SemanticAnalysis.h"
#include "SourceManager.h"
#include "Support/ASTVerifier.h"
#include "Util/Diagnostic.h"

#include <iostream>
#include <string>

std::filesystem::path
ExpectedASTPath(const std::filesystem::path &source_path) {
  std::filesystem::path expected = source_path;
  expected.replace_extension(".ast.yaml");
  return expected;
}

int RunASTFixtureTest(const std::filesystem::path &source_path,
                      const std::filesystem::path &expected_path,
                      ASTFixtureMode mode) {
  SourceManager sources;
  SourceHandle source = sources.LoadSourceFrom(source_path.string());

  MacroContext macros;
  TypeContext types;
  DiagnosticContext diagnostics(sources);
  CompileContext context(types, sources, diagnostics);
  RegisterBuiltinIntrinsics();
  RawLexer raw_lexer(source, macros, sources, context);
  Lexer lexer(raw_lexer);
  auto translation_unit =
      ParseTranslationUnit(lexer, types, sources, diagnostics);
  if (!translation_unit) {
    std::cerr << diagnostics.Format(diagnostics.Messages()) << '\n';
    return 1;
  }
  if (mode == ASTFixtureMode::ParseAndAnalyze) {
    if (diagnostics.HasErrors()) {
      std::cerr << diagnostics.Format(diagnostics.Messages()) << '\n';
      return 1;
    }
    if (!AnalyzeTranslationUnit(**translation_unit, types, diagnostics)) {
      std::cerr << diagnostics.Format(diagnostics.Messages()) << '\n';
      return 1;
    }
  }

  YAML::Node expected = YAML::LoadFile(expected_path.string());
  VerifyContext verify_context;
  verify_context.path_stack.emplace_back("AST");
  VerifyTreeNode((*translation_unit).get(), expected, verify_context);
  if (!verify_context.messages.empty()) {
    std::cerr << "FAIL " << source_path << '\n';
    for (const std::string &message : verify_context.messages) {
      std::cerr << message << '\n';
    }
    return 1;
  }

  std::cout << "PASS " << source_path << '\n';
  return 0;
}
