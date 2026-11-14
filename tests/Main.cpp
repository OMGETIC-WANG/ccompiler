#include "AST/Context.h"
#include "AST/TranslationUnit.h"
#include "AST/Type.h"
#include "AST/TypeContext.h"
#include "CodeGen/LLVMIR/Intrinsics.h"
#include "Lex/Lexer.h"
#include "Lex/Macro.h"
#include "SourceManager.h"
#include "Support/ASTVerifier.h"
#include "Support/DeclBindingTest.h"
#include "Util/Diagnostic.h"

#include <array>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::string_view StripLeadingAndTrailingWhitespace(std::string_view value) {
  constexpr std::string_view whitespace = " \t\n\r\f\v";
  const std::size_t first = value.find_first_not_of(whitespace);
  if (first == std::string_view::npos) {
    return {};
  }
  const std::size_t last = value.find_last_not_of(whitespace);
  return value.substr(first, last - first + 1);
}

bool OutputsMatch(std::string_view actual, std::string_view expected) {
  return StripLeadingAndTrailingWhitespace(actual) ==
         StripLeadingAndTrailingWhitespace(expected);
}

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
  DiagnosticContext diagnostics(sources);
  CompileContext context(types, sources, diagnostics);
  RegisterBuiltinIntrinsics();
  RawLexer raw_lexer(source, macros, sources, context);
  Lexer lexer(raw_lexer);
  auto translation_unit =
      ParseTranslationUnit(lexer, types, sources, diagnostics);

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

int RunDiagnosticSelfTest() {
  FixedLexer recovery_lexer({
      Token(TokenType::Identifier),
      Token(TokenType::RBrace),
      Token(TokenType::Semicolon),
      Token(TokenType::Identifier),
  });
  Lexer lexer(recovery_lexer);
  constexpr std::array recovery_stops{
      TokenType::Semicolon,
      TokenType::RBrace,
  };
  if (!lexer.ConsumeUntil(recovery_stops) ||
      lexer.PeekNextToken().type != TokenType::RBrace) {
    std::cerr << "ConsumeUntil did not retain its stop token\n";
    return 1;
  }
  lexer.ConsumeNextToken();
  if (!lexer.ConsumeUntil(recovery_stops) ||
      lexer.PeekNextToken().type != TokenType::Semicolon) {
    std::cerr << "ConsumeUntil did not recognize all stop tokens\n";
    return 1;
  }
  lexer.ConsumeNextToken();
  constexpr std::array eof_stops{TokenType::RBrace};
  if (lexer.ConsumeUntil(eof_stops) ||
      lexer.PeekNextToken().type != TokenType::EndOfFile) {
    std::cerr << "ConsumeUntil did not stop at EOF\n";
    return 1;
  }

  SourceManager sources;
  SourceHandle source = sources.LoadSource("abc\n", "<stdin>");
  DiagnosticContext diagnostics(sources);
  SourceLocation location{
      .source_id = source.id,
      .begin = 1,
      .end = 2,
  };

  diagnostics.Error(location, "localized");
  std::string formatted = diagnostics.Format(diagnostics.Messages());
  if (!OutputsMatch(formatted, "<stdin>:1:2: error: localized")) {
    std::cerr << "Unexpected localized diagnostic: " << formatted << '\n';
    return 1;
  }
  if (!diagnostics.HasErrors() || diagnostics.Messages().size() != 1) {
    std::cerr << "Localized diagnostic was not recorded\n";
    return 1;
  }

  diagnostics.Error(location, "second");
  bool collected_error_thrown = false;
  try {
    diagnostics.ThrowIfErrors();
  } catch (const DiagnosticError &error) {
    collected_error_thrown = true;
    formatted = diagnostics.Format(error.Messages());
    if (!OutputsMatch(formatted, "<stdin>:1:2: error: localized\n"
                                 "<stdin>:1:2: error: second")) {
      std::cerr << "Unexpected collected diagnostics: " << formatted << '\n';
      return 1;
    }
  }
  if (!collected_error_thrown) {
    std::cerr << "Collected diagnostics were not raised\n";
    return 1;
  }

  DiagnosticMessage global{.message = "global"};
  formatted = diagnostics.Format(global);
  if (!OutputsMatch(formatted, "ccompiler: error: global")) {
    std::cerr << "Unexpected global diagnostic: " << formatted << '\n';
    return 1;
  }

  DiagnosticContext raised(sources);
  raised.Error(location, "collected");
  try {
    raised.Fail(location, "raised");
  } catch (const DiagnosticError &error) {
    if (std::string_view(error.what()) != "raised") {
      std::cerr << "Unexpected fatal diagnostic payload: " << error.what()
                << '\n';
      return 1;
    }
    formatted = raised.Format(error.Messages());
    if (!OutputsMatch(formatted, "<stdin>:1:2: error: collected\n"
                                 "<stdin>:1:2: error: raised")) {
      std::cerr << "Unexpected raised diagnostic: " << formatted << '\n';
      return 1;
    }
    std::cout << "PASS diagnostic formatting\n";
    return 0;
  }

  std::cerr << "DiagnosticError was not raised\n";
  return 1;
}

} // namespace

int main(int argc, char **argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--diagnostic-self-test") {
    return RunDiagnosticSelfTest();
  }

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

  if (argc == 2 && std::string_view(argv[1]) == "--decl-binding-self-test") {
    return RunDeclBindingSelfTest();
  }

  if (argc < 2 || argc > 3) {
    std::cerr << "Usage: " << argv[0]
              << " <source.c> [expected.ast.yaml]\n"
                 "       "
              << argv[0]
              << " --check-verifiers\n"
                 "       "
              << argv[0]
              << " --diagnostic-self-test\n"
                 "       "
              << argv[0] << " --decl-binding-self-test\n";
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
