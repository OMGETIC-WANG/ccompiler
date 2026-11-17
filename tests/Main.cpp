#include "AST/Context.h"
#include "AST/TranslationUnit.h"
#include "AST/Type.h"
#include "AST/TypeContext.h"
#include "CodeGen/LLVMIR/Intrinsics.h"
#include "Diagnostics/Errors.generated.h"
#include "Lex/Lexer.h"
#include "Lex/Macro.h"
#include "Sema/SemanticAnalysis.h"
#include "SourceManager.h"
#include "Support/ASTPrintTest.h"
#include "Support/ASTVerifier.h"
#include "Support/DeclBindingTest.h"
#include "Support/LongDoubleLoweringTest.h"
#include "Support/QualifierCodeGenTest.h"
#include "Util/Diagnostic.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <format>
#include <iostream>
#include <memory>
#include <regex>
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
            const std::filesystem::path &expected_path,
            bool run_semantic_analysis = false) {
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
  if (run_semantic_analysis) {
    diagnostics.ThrowIfErrors();
    AnalyzeTranslationUnit(*translation_unit, types, diagnostics);
  }

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

std::string ExpandDiagnosticPlaceholders(std::string value,
                                         const std::filesystem::path &source) {
  const std::string source_path = source.string();
  std::size_t position = 0;
  while ((position = value.find("@SOURCE_FILE@", position)) !=
         std::string::npos) {
    value.replace(position, std::string_view("@SOURCE_FILE@").size(),
                  source_path);
    position += source_path.size();
  }
  return value;
}

bool VerifyDiagnosticLocation(const DiagnosticMessage &message,
                              const YAML::Node &expected,
                              const YAML::Node &expected_file,
                              const std::filesystem::path &source,
                              SourceManager &sources, std::string &reason) {
  if (message.locations.empty()) {
    reason = "diagnostic has no location";
    return false;
  }
  const SourceLocation location = message.locations.front();
  if (!location.IsValid()) {
    reason = "diagnostic has an invalid location";
    return false;
  }

  if (expected_file) {
    if (!expected_file.IsScalar()) {
      reason = "expected file must be a scalar";
      return false;
    }
    const std::string expected_name =
        ExpandDiagnosticPlaceholders(expected_file.Scalar(), source);
    if (sources.GetFileName(location) != expected_name) {
      reason = std::format("expected file '{}', got '{}'", expected_name,
                           sources.GetFileName(location));
      return false;
    }
  }

  if (!expected.IsScalar()) {
    reason = "expected location must be a scalar";
    return false;
  }
  const std::string expected_location = expected.Scalar();
  static const std::regex range_pattern(
      R"(^([0-9]+):([0-9]+)-([0-9]+):([0-9]+)$)");
  std::smatch match;
  if (std::regex_match(expected_location, match, range_pattern)) {
    const SourcePosition begin = sources.GetLineColumn(location);
    const SourcePosition end =
        sources.GetLineColumn(SourceLocation{.source_id = location.source_id,
                                             .begin = location.end,
                                             .end = location.end});
    const auto expected_begin_line = std::stoul(match[1].str());
    const auto expected_begin_column = std::stoul(match[2].str());
    const auto expected_end_line = std::stoul(match[3].str());
    const auto expected_end_column = std::stoul(match[4].str());
    if (begin.line != expected_begin_line ||
        begin.column != expected_begin_column ||
        end.line != expected_end_line || end.column != expected_end_column) {
      reason = std::format("expected location '{}', got '{}:{} - {}:{}'",
                           expected_location, begin.line, begin.column,
                           end.line, end.column);
      return false;
    }
    return true;
  }

  const std::string_view actual_text = sources.GetText(location);
  if (actual_text != expected_location) {
    reason = std::format("expected location text '{}', got '{}'",
                         expected_location, actual_text);
    return false;
  }
  return true;
}

bool VerifyDiagnostics(const std::vector<DiagnosticMessage> &actual,
                       const YAML::Node &expected,
                       const std::filesystem::path &source,
                       SourceManager &sources) {
  if (!expected.IsMap() || expected.size() != 1 || !expected["diagnostics"] ||
      !expected["diagnostics"].IsSequence()) {
    std::cerr << "Expected a mapping with a 'diagnostics' sequence\n";
    return false;
  }

  const YAML::Node expected_messages = expected["diagnostics"];
  if (expected_messages.size() != actual.size()) {
    std::cerr << std::format("Expected {} diagnostics, got {}\n",
                             expected_messages.size(), actual.size());
    return false;
  }

  for (std::size_t index = 0; index < actual.size(); ++index) {
    const YAML::Node item = expected_messages[index];
    if (!item.IsMap()) {
      std::cerr << std::format("Diagnostic {} expectation is not a mapping\n",
                               index);
      return false;
    }
    for (const auto &entry : item) {
      if (!entry.first.IsScalar()) {
        std::cerr << std::format("Diagnostic {} has a non-scalar key\n", index);
        return false;
      }
      const std::string key = entry.first.Scalar();
      if (key != "error" && key != "file" && key != "location" &&
          key != "fields") {
        std::cerr << std::format("Diagnostic {} has unknown key '{}'\n", index,
                                 key);
        return false;
      }
    }

    const YAML::Node error_name = item["error"];
    const YAML::Node location = item["location"];
    const YAML::Node fields = item["fields"];
    if (!error_name || !error_name.IsScalar() || !location || !fields ||
        !fields.IsMap()) {
      std::cerr << std::format(
          "Diagnostic {} requires scalar error, location, and mapping "
          "fields\n",
          index);
      return false;
    }

    const DiagnosticMessage &message = actual[index];
    if (!message.error) {
      std::cerr << std::format("Diagnostic {} has no structured error\n",
                               index);
      return false;
    }
    if (!errs::VerifyErrorFields(*message.error, error_name.Scalar(), fields)) {
      std::cerr << std::format(
          "Diagnostic {} does not match error '{}' or its fields\n", index,
          error_name.Scalar());
      return false;
    }

    std::string reason;
    if (!VerifyDiagnosticLocation(message, location, item["file"], source,
                                  sources, reason)) {
      std::cerr << std::format("Diagnostic {} location mismatch: {}\n", index,
                               reason);
      return false;
    }
  }
  return true;
}

int RunStructuredDiagnosticTest(const std::filesystem::path &source_path,
                                const std::filesystem::path &expected_path) {
  SourceManager sources;
  SourceHandle source = sources.LoadSourceFrom(source_path.string());
  DiagnosticContext diagnostics(sources);
  std::vector<DiagnosticMessage> messages;
  MacroContext macros;
  TypeContext types;
  CompileContext context(types, sources, diagnostics);

  try {
    RegisterBuiltinIntrinsics();
    RawLexer raw_lexer(source, macros, sources, context);
    Lexer lexer(raw_lexer);
    auto translation_unit =
        ParseTranslationUnit(lexer, types, sources, diagnostics);
    if (!diagnostics.HasErrors()) {
      AnalyzeTranslationUnit(*translation_unit, types, diagnostics);
    }
    diagnostics.ThrowIfErrors();
  } catch (const DiagnosticError &error) {
    messages = error.Messages();
  }

  if (messages.empty()) {
    messages = diagnostics.Messages();
  }

  YAML::Node expected = YAML::LoadFile(expected_path.string());
  if (!VerifyDiagnostics(messages, expected, source_path, sources)) {
    std::cerr << "FAIL " << source_path << '\n';
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

  diagnostics.Error(location, errs::UnknownVar("localized"));
  const errs::UnknownVar verified_error("localized");
  if (!verified_error.Verify(YAML::Load("{}")) ||
      !verified_error.Verify(YAML::Load("var_name: localized")) ||
      verified_error.Verify(YAML::Load("var_name: different")) ||
      verified_error.Verify(YAML::Load("unknown: localized"))) {
    std::cerr << "Generated error field verification is incorrect\n";
    return 1;
  }
  if (!errs::ArgumentCountMismatch(2).Verify(
          YAML::Load("expected_arguments: 2")) ||
      !errs::UnsupportedExpressionASTNode(ASTNodeType::VarRef)
           .Verify(YAML::Load("node_type: VarRef"))) {
    std::cerr << "Generated scalar field verification is incorrect\n";
    return 1;
  }
  TypeContext types;
  const errs::TypeMismatch mismatch(types.Pointer(types.getInt()),
                                    types.getInt());
  if (mismatch.ToString() != "Type mismatch: expected Ptr<int>, got int") {
    std::cerr << "Generated TypeRef formatting is incorrect\n";
    return 1;
  }
  if (!mismatch.Verify(YAML::Load("expected_type: Ptr<int>")) ||
      mismatch.Verify(YAML::Load("expected_type: int"))) {
    std::cerr << "Generated partial field verification is incorrect\n";
    return 1;
  }
  std::string formatted = diagnostics.Format(diagnostics.Messages());
  if (!OutputsMatch(formatted, "<stdin>:1:2: error: Unknown var: localized")) {
    std::cerr << "Unexpected localized diagnostic: " << formatted << '\n';
    return 1;
  }
  if (!diagnostics.HasErrors() || diagnostics.Messages().size() != 1) {
    std::cerr << "Localized diagnostic was not recorded\n";
    return 1;
  }

  diagnostics.Error(location, errs::ExpectedSemicolon());
  bool collected_error_thrown = false;
  try {
    diagnostics.ThrowIfErrors();
  } catch (const DiagnosticError &error) {
    collected_error_thrown = true;
    formatted = diagnostics.Format(error.Messages());
    if (!OutputsMatch(formatted, "<stdin>:1:2: error: Unknown var: localized\n"
                                 "<stdin>:1:2: error: Expected ';'")) {
      std::cerr << "Unexpected collected diagnostics: " << formatted << '\n';
      return 1;
    }
  }
  if (!collected_error_thrown) {
    std::cerr << "Collected diagnostics were not raised\n";
    return 1;
  }

  DiagnosticMessage global{
      .error = std::make_shared<errs::UnexpectedToken>(),
      .formatted_message = "Unexpected token",
  };
  formatted = diagnostics.Format(global);
  if (!OutputsMatch(formatted, "ccompiler: error: Unexpected token")) {
    std::cerr << "Unexpected global diagnostic: " << formatted << '\n';
    return 1;
  }

  DiagnosticContext raised(sources);
  raised.Error(location, errs::UnexpectedToken());
  try {
    raised.Fail(location, errs::ExpectedSemicolon());
  } catch (const DiagnosticError &error) {
    if (std::string_view(error.what()) != "Expected ';'") {
      std::cerr << "Unexpected fatal diagnostic payload: " << error.what()
                << '\n';
      return 1;
    }
    formatted = raised.Format(error.Messages());
    if (!OutputsMatch(formatted, "<stdin>:1:2: error: Unexpected token\n"
                                 "<stdin>:1:2: error: Expected ';'")) {
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
  bool run_semantic_analysis = false;
  if (argc >= 2 && std::string_view(argv[1]) == "--sema") {
    run_semantic_analysis = true;
    --argc;
    ++argv;
  }

  if (argc == 2 && std::string_view(argv[1]) == "--diagnostic-self-test") {
    return RunDiagnosticSelfTest();
  }

  if (argc == 4 && std::string_view(argv[1]) == "--diagnostics") {
    try {
      return RunStructuredDiagnosticTest(argv[2], argv[3]);
    } catch (const std::exception &error) {
      std::cerr << "FAIL " << argv[2] << '\n' << error.what() << '\n';
      return 1;
    }
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

  if (argc == 2 && std::string_view(argv[1]) == "--ast-print-self-test") {
    return RunASTPrintSelfTest();
  }

  if (argc == 2 && std::string_view(argv[1]) == "--qualified-ir-self-test") {
    return RunQualifierCodeGenSelfTest();
  }

  if (argc == 2 &&
      std::string_view(argv[1]) == "--long-double-lowering-self-test") {
    return RunLongDoubleLoweringSelfTest();
  }

  if (argc < 2 || argc > 3) {
    std::cerr << "Usage: " << argv[0]
              << " <source.c> [expected.ast.yaml]\n"
                 "       "
              << argv[0]
              << " --sema <source.c> [expected.ast.yaml]\n"
                 "       "
              << argv[0]
              << " --check-verifiers\n"
                 "       "
              << argv[0]
              << " --diagnostic-self-test\n"
                 "       "
              << argv[0]
              << " --diagnostics <source.c> <expected.error.yaml>\n"
                 "       "
              << argv[0]
              << " --decl-binding-self-test\n"
                 "       "
              << argv[0]
              << " --ast-print-self-test\n"
                 "       "
              << argv[0]
              << " --qualified-ir-self-test\n"
                 "       "
              << argv[0] << " --long-double-lowering-self-test\n";
    return 2;
  }

  try {
    std::filesystem::path source_path = argv[1];
    std::filesystem::path expected_path = argc == 3
                                              ? std::filesystem::path(argv[2])
                                              : ExpectedASTPath(source_path);
    return RunTest(source_path, expected_path, run_semantic_analysis);
  } catch (const std::exception &error) {
    std::cerr << "FAIL " << argv[1] << '\n' << error.what() << '\n';
    return 1;
  }
}
