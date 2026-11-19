#include "Diagnostics/DiagnosticFixtureTest.h"

#include "AST/TranslationUnit.h"
#include "AST/TypeContext.h"
#include "CodeGen/LLVMIR/Intrinsics.h"
#include "Diagnostics/Errors.generated.h"
#include "Lex/Lexer.h"
#include "Sema/SemanticAnalysis.h"
#include "SourceManager.h"
#include "Util/Diagnostic.h"

#include <format>
#include <iostream>
#include <regex>
#include <string>
#include <vector>
#include <yaml-cpp/yaml.h>

namespace {

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

} // namespace

int RunStructuredDiagnosticTest(const std::filesystem::path &source_path,
                                const std::filesystem::path &expected_path) {
  SourceManager sources;
  SourceHandle source = sources.LoadSourceFrom(source_path.string());
  const std::filesystem::path normalized_source_path =
      std::filesystem::absolute(source_path).lexically_normal();
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
  if (!VerifyDiagnostics(messages, expected, normalized_source_path, sources)) {
    std::cerr << "FAIL " << source_path << '\n';
    return 1;
  }
  std::cout << "PASS " << source_path << '\n';
  return 0;
}
