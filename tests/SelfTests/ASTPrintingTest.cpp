#include "AST/AST.h"
#include "AST/ASTPrint.h"
#include "AST/Declaration.h"
#include "AST/Intrinsic.h"
#include "AST/TranslationUnit.h"
#include "AST/TypeContext.h"

#include <gtest/gtest.h>

#include <cstdint>
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

} // namespace

class ASTPrinting : public ::testing::Test {
protected:
  void SetUp() override {
    unit.decls.push_back(MakeAggregateVars(types));
    unit.decls.push_back(MakeFieldVars(types));
  }

  TypeContext types;
  TranslationUnit unit;
};

TEST_F(ASTPrinting, PrintsAggregateTree) {
  std::ostringstream output;
  unit.Print(output, {.use_color = false, .print_field_names = true});

  constexpr std::string_view expected =
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
  EXPECT_EQ(NormalizeAddresses(output.str()), expected);
}

TEST_F(ASTPrinting, OmitsWrapperAndTypeNodes) {
  std::ostringstream output;
  unit.Print(output, {.use_color = false, .print_field_names = true});
  const std::string normalized = NormalizeAddresses(output.str());

  EXPECT_EQ(normalized.find("ValueInitializer"), std::string::npos);
  EXPECT_EQ(normalized.find("ArrayInitializer"), std::string::npos);
  EXPECT_EQ(normalized.find("FieldInitializer"), std::string::npos);
  EXPECT_EQ(normalized.find("BuiltinType"), std::string::npos);
  EXPECT_EQ(normalized.find("ArrayType"), std::string::npos);
}

TEST_F(ASTPrinting, PrintsIntrinsicArguments) {
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

  std::ostringstream output;
  intrinsic.Print(output, {.use_color = false, .print_field_names = true});
  const std::string normalized = NormalizeAddresses(output.str());

  EXPECT_NE(normalized.find("arg0_type=int"), std::string::npos);
  EXPECT_NE(normalized.find("value=5"), std::string::npos);
}

TEST_F(ASTPrinting, SupportsKeylessOutput) {
  std::ostringstream output;
  unit.Print(output, {.use_color = false, .print_field_names = false});
  const std::string normalized = NormalizeAddresses(output.str());

  EXPECT_NE(normalized.find("TranslationUnit 0xADDR"), std::string::npos);
  EXPECT_EQ(normalized.find("address="), std::string::npos);
  EXPECT_EQ(normalized.find("name="), std::string::npos);
  EXPECT_EQ(normalized.find("type="), std::string::npos);
}

TEST_F(ASTPrinting, EmitsColor) {
  std::ostringstream output;
  unit.Print(output, {.use_color = true, .print_field_names = true});
  const std::string &colored = output.str();

  EXPECT_NE(colored.find("\x1b[38;2;0;204;0mTranslationUnit\x1b[0m"),
            std::string::npos);
  EXPECT_NE(colored.find("\x1b[38;2;198;141;255mVarDecl\x1b[0m"),
            std::string::npos);
  EXPECT_NE(colored.find("\x1b[38;2;127;140;141mname\x1b[0m="),
            std::string::npos);
  EXPECT_NE(colored.find("\x1b[38;2;51;153;255mint\x1b[0m"), std::string::npos);
  EXPECT_NE(colored.find("\x1b[38;2;0;153;76m0x"), std::string::npos);
}

TEST_F(ASTPrinting, ParsesColorModes) {
  EXPECT_EQ(ParseASTPrintColorMode("auto"), ASTPrintColorMode::Auto);
  EXPECT_EQ(ParseASTPrintColorMode("always"), ASTPrintColorMode::Always);
  EXPECT_EQ(ParseASTPrintColorMode("never"), ASTPrintColorMode::Never);
  EXPECT_FALSE(ParseASTPrintColorMode("invalid").has_value());
}

TEST_F(ASTPrinting, ResolvesAutomaticColor) {
  EXPECT_TRUE(ShouldEnableASTColor(ASTPrintColorMode::Auto, true, false));
  EXPECT_FALSE(ShouldEnableASTColor(ASTPrintColorMode::Auto, false, false));
  EXPECT_FALSE(ShouldEnableASTColor(ASTPrintColorMode::Auto, true, true));
  EXPECT_TRUE(ShouldEnableASTColor(ASTPrintColorMode::Always, false, true));
  EXPECT_FALSE(ShouldEnableASTColor(ASTPrintColorMode::Never, true, false));
}
