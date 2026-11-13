#include "AST/Context.h"
#include "AST/TranslationUnit.h"
#include "AST/Type.h"
#include "AST/TypeContext.h"
#include "CodeGen/LLVMIR/Intrinsics.h"
#include "Lex/Lexer.h"
#include "Lex/Macro.h"
#include "SourceManager.h"
#include "Support/ASTVerifier.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::filesystem::path ExpectedASTPath(const std::filesystem::path &source) {
  std::filesystem::path expected = source;
  expected.replace_extension(".ast.yaml");
  return expected;
}

int RunTest(const std::filesystem::path &source_path,
            const std::filesystem::path &expected_path) {
  SourceManager sources;
  SourceHandle source = sources.LoadSourceFrom(source_path.string());

  MacroContext macros;
  TypeContext types;
  CompileContext context(types, sources);
  RegisterBuiltinIntrinsics();
  RawLexer raw_lexer(source, macros, sources, context);
  Lexer lexer(raw_lexer);
  auto translation_unit = ParseTranslationUnit(lexer, types, sources);

  YAML::Node expected = YAML::LoadFile(expected_path.string());
  VerifyContext verify_context;
  verify_context.path_stack.emplace_back("AST");
  VerifyTreeNode(translation_unit.get(), expected, verify_context);
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

} // namespace

int main(int argc, char **argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--check-verifiers") {
    std::vector<std::string> missing = FindMissingVerifierNames();
    if (missing.empty()) {
      std::cout << "All AST and type verifiers are registered\n";
      return 0;
    }
    std::cerr << "Missing verifiers:\n";
    for (const std::string &name : missing) {
      std::cerr << name << '\n';
    }
    return 1;
  }

  if (argc < 2 || argc > 3) {
    std::cerr << "Usage: " << argv[0]
              << " <source.c> [expected.ast.yaml]\n"
                 "       "
              << argv[0] << " --check-verifiers\n";
    return 2;
  }

  try {
    std::filesystem::path source_path = argv[1];
    std::filesystem::path expected_path = argc == 3
                                              ? std::filesystem::path(argv[2])
                                              : ExpectedASTPath(source_path);
    return RunTest(source_path, expected_path);
  } catch (const std::exception &error) {
    std::cerr << "FAIL " << argv[1] << '\n' << error.what() << '\n';
    return 1;
  }
}
