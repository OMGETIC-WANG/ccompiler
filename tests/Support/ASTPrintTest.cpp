#include "ASTPrintTest.h"

#include "AST/AST.h"
#include "AST/ASTPrint.h"
#include "AST/Declaration.h"
#include "AST/Intrinsic.h"
#include "AST/TranslationUnit.h"
#include "AST/TypeContext.h"

#include <cstdint>
#include <iostream>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>

namespace {

std::string NormalizeAddresses(std::string text) {
  static const std::regex address_pattern("0x[0-9a-f]+");
  return std::regex_replace(text, address_pattern, "0xADDR");
}

std::unique_ptr<LiteralNum> MakeInteger(TypeContext &types, int64_t value) {
  auto literal = std::make_unique<LiteralNum>();
  literal->type = types.getInt();
  literal->value = value;
  literal->constant_value = value;
  return literal;
}

std::unique_ptr<VarDecl> MakeAggregateVars(TypeContext &types) {
  auto values = std::make_unique<VarDecl>();
  values->name = "values";
  values->type = types.Array(types.getInt(), 2);

  auto array = std::make_unique<ArrayInitializer>();
  for (int64_t value = 1; value <= 2; ++value) {
    auto initializer = std::make_unique<ValueInitializer>();
    initializer->value = MakeInteger(types, value);
    array->initializers.push_back(std::move(initializer));
  }
  values->initializer = std::move(array);

  return values;
}

std::unique_ptr<VarDecl> MakeFieldVars(TypeContext &types) {
  auto point = std::make_unique<VarDecl>();
  point->name = "point";
  point->type = types.GetOrCreateStruct(0, "Point");

  auto fields = std::make_unique<FieldInitializer>();
  auto x = std::make_unique<ValueInitializer>();
  x->value = MakeInteger(types, 3);
  fields->field_initializers.emplace_back("x", std::move(x));
  auto y = std::make_unique<ValueInitializer>();
  y->value = MakeInteger(types, 4);
  fields->field_initializers.emplace_back("y", std::move(y));
  point->initializer = std::move(fields);

  return point;
}

bool CheckText(std::string_view actual, std::string_view expected,
               std::string_view context) {
  if (actual == expected) {
    return true;
  }
  std::cerr << "FAIL AST printing: " << context << "\nExpected:\n"
            << expected << "\nActual:\n"
            << actual;
  return false;
}

bool CheckContains(std::string_view text, std::string_view expected,
                   std::string_view context) {
  if (text.find(expected) != std::string_view::npos) {
    return true;
  }
  std::cerr << "FAIL AST printing: " << context << "\nMissing:\n"
            << expected << "\nActual:\n"
            << text;
  return false;
}

bool CheckNotContains(std::string_view text, std::string_view rejected,
                      std::string_view context) {
  if (text.find(rejected) == std::string_view::npos) {
    return true;
  }
  std::cerr << "FAIL AST printing: " << context << "\nUnexpected:\n"
            << rejected << "\nActual:\n"
            << text;
  return false;
}

} // namespace

int RunASTPrintSelfTest() {
  TypeContext types;
  TranslationUnit unit;
  unit.decls.push_back(MakeAggregateVars(types));
  unit.decls.push_back(MakeFieldVars(types));

  std::ostringstream plain;
  unit.Print(plain, {.use_color = false, .print_field_names = true});
  const std::string normalized = NormalizeAddresses(plain.str());
  constexpr std::string_view expected_plain =
      "TranslationUnit 0xADDR\n"
      "|-VarDecl 0xADDR name=values type=Array<int, 2> "
      "initializer=array elements=2\n"
      "| |-LiteralNum 0xADDR value_category=RValue type=int "
      "constant_value=1 value=1\n"
      "| `-LiteralNum 0xADDR value_category=RValue type=int "
      "constant_value=2 value=2\n"
      "`-VarDecl 0xADDR name=point type=struct Point "
      "initializer=fields field_names=x,y\n"
      "  |-LiteralNum 0xADDR value_category=RValue type=int "
      "constant_value=3 value=3\n"
      "  `-LiteralNum 0xADDR value_category=RValue type=int "
      "constant_value=4 value=4\n";
  if (!CheckText(normalized, expected_plain, "aggregate tree")) {
    return 1;
  }
  if (!CheckNotContains(normalized, "ValueInitializer", "wrapper node") ||
      !CheckNotContains(normalized, "ArrayInitializer", "wrapper node") ||
      !CheckNotContains(normalized, "FieldInitializer", "wrapper node") ||
      !CheckNotContains(normalized, "BuiltinType", "type node") ||
      !CheckNotContains(normalized, "ArrayType", "type node")) {
    return 1;
  }

  const IntrinsicDesc intrinsic_desc{
      .name = "test_intrinsic",
      .arg_kinds = {},
      .evaluator = nullptr,
      .type_evaluator = nullptr,
  };
  IntrinsicCall intrinsic;
  intrinsic.desc = &intrinsic_desc;
  intrinsic.type = types.getInt();
  intrinsic.args.emplace_back(TypeRef(types.getInt()));
  intrinsic.args.emplace_back(MakeInteger(types, 5));
  std::ostringstream intrinsic_output;
  intrinsic.Print(intrinsic_output,
                  {.use_color = false, .print_field_names = true});
  const std::string normalized_intrinsic =
      NormalizeAddresses(intrinsic_output.str());
  if (!CheckContains(normalized_intrinsic, "arg0_type=int",
                     "intrinsic type argument") ||
      !CheckContains(normalized_intrinsic, "value=5",
                     "intrinsic expression child")) {
    return 1;
  }

  std::ostringstream keyless;
  unit.Print(keyless, {.use_color = false, .print_field_names = false});
  const std::string normalized_keyless = NormalizeAddresses(keyless.str());
  if (!CheckContains(normalized_keyless, "TranslationUnit 0xADDR",
                     "keyless root") ||
      !CheckNotContains(normalized_keyless, "address=", "keyless address") ||
      !CheckNotContains(normalized_keyless, "name=", "keyless name") ||
      !CheckNotContains(normalized_keyless, "type=", "keyless type")) {
    return 1;
  }

  std::ostringstream colored;
  unit.Print(colored, {.use_color = true, .print_field_names = true});
  if (!CheckContains(colored.str(), "\x1b[38;2;0;204;0mTranslationUnit\x1b[0m",
                     "translation-unit color") ||
      !CheckContains(colored.str(), "\x1b[38;2;198;141;255mVarDecl\x1b[0m",
                     "declaration color") ||
      !CheckContains(colored.str(), "\x1b[38;2;127;140;141mname\x1b[0m=",
                     "field prefix color") ||
      !CheckContains(colored.str(), "\x1b[38;2;51;153;255mint\x1b[0m",
                     "type color") ||
      !CheckContains(colored.str(), "\x1b[38;2;0;153;76m0x", "address color") ||
      !CheckNotContains(normalized, "address=", "bare node address")) {
    return 1;
  }
  if (!CheckNotContains(plain.str(), "\x1b", "plain output")) {
    return 1;
  }

  if (ParseASTPrintColorMode("auto") != ASTPrintColorMode::Auto ||
      ParseASTPrintColorMode("always") != ASTPrintColorMode::Always ||
      ParseASTPrintColorMode("never") != ASTPrintColorMode::Never ||
      ParseASTPrintColorMode("invalid").has_value()) {
    std::cerr << "FAIL AST printing: color mode parsing\n";
    return 1;
  }
  if (!ShouldEnableASTColor(ASTPrintColorMode::Auto, true, false) ||
      ShouldEnableASTColor(ASTPrintColorMode::Auto, false, false) ||
      ShouldEnableASTColor(ASTPrintColorMode::Auto, true, true) ||
      !ShouldEnableASTColor(ASTPrintColorMode::Always, false, true) ||
      ShouldEnableASTColor(ASTPrintColorMode::Never, true, false)) {
    std::cerr << "FAIL AST printing: color mode resolution\n";
    return 1;
  }

  std::cout << "PASS AST printing\n";
  return 0;
}
