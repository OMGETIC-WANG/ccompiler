#include "AST/AST.h"
#include "AST/Operations.h"
#include "AST/TranslationUnit.h"
#include "AST/TypeContext.h"
#include "CodeGen/LLVMIR/Intrinsics.h"
#include "Lex/Lexer.h"
#include "Lex/Macro.h"
#include "SourceManager.h"
#include "Util/Diagnostic.h"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view kSource = R"(
int global = 1;

enum Color {
  Red = 3
};

int pick(int value) {
  int local = value;
  {
    int value = global;
    local = value;
  }
  return local + Red;
}

int main(void) {
  return pick(4);
}
)";

} // namespace

class DeclarationBinding : public ::testing::Test {
protected:
  void SetUp() override {
    SourceHandle source =
        sources.LoadSource(std::string(kSource), "<decl-binding>");
    CompileContext context(types, sources, diagnostics);
    RegisterBuiltinIntrinsics();
    RawLexer raw_lexer(source, macros, sources, context);
    Lexer lexer(raw_lexer);
    translation_unit = ParseTranslationUnit(lexer, types, sources, diagnostics);

    ASSERT_FALSE(diagnostics.HasErrors());
    ASSERT_TRUE(translation_unit.has_value());
    ASSERT_NE(*translation_unit, nullptr);
    TranslationUnit &unit = **translation_unit;
    ASSERT_EQ(unit.decls.size(), 4);

    global = unit.decls[0]->DynCast<VarDecl>();
    color = unit.decls[1]->DynCast<EnumDecl>();
    pick = unit.decls[2]->DynCast<FunctionDecl>();
    main = unit.decls[3]->DynCast<FunctionDecl>();
    ASSERT_NE(global, nullptr);
    ASSERT_NE(color, nullptr);
    ASSERT_NE(pick, nullptr);
    ASSERT_NE(main, nullptr);
    ASSERT_FALSE(color->values.empty());
    ASSERT_EQ(pick->parameters.size(), 1);
    ASSERT_NE(pick->body, nullptr);
    ASSERT_EQ(pick->body->statements.size(), 3);
    ASSERT_NE(main->body, nullptr);
    ASSERT_EQ(main->body->statements.size(), 1);

    parameter = pick->parameters[0].get();
    red = color->values[0].get();
    local = pick->body->statements[0]->DynCast<VarDecl>();
    inner_block = pick->body->statements[1]->DynCast<CompoundStmt>();
    ASSERT_NE(local, nullptr);
    ASSERT_NE(inner_block, nullptr);
    ASSERT_EQ(inner_block->statements.size(), 2);

    auto *local_initializer = local->initializer->DynCast<ValueInitializer>();
    inner_value = inner_block->statements[0]->DynCast<VarDecl>();
    auto *assignment = inner_block->statements[1]->DynCast<BinaryOp>();
    ASSERT_NE(local_initializer, nullptr);
    ASSERT_NE(inner_value, nullptr);
    ASSERT_NE(assignment, nullptr);

    local_value_ref = local_initializer->value->DynCast<VarRef>();
    auto *inner_initializer =
        inner_value->initializer->DynCast<ValueInitializer>();
    ASSERT_NE(inner_initializer, nullptr);
    inner_global_ref = inner_initializer->value->DynCast<VarRef>();
    assignment_local = assignment->left->DynCast<VarRef>();
    assignment_value = assignment->right->DynCast<VarRef>();
    ASSERT_NE(local_value_ref, nullptr);
    ASSERT_NE(inner_global_ref, nullptr);
    ASSERT_NE(assignment_local, nullptr);
    ASSERT_NE(assignment_value, nullptr);

    auto *return_stmt = pick->body->statements[2]->DynCast<ReturnStmt>();
    ASSERT_NE(return_stmt, nullptr);
    auto *return_expr = return_stmt->ret_value->DynCast<BinaryOp>();
    ASSERT_NE(return_expr, nullptr);
    return_local = return_expr->left->DynCast<VarRef>();
    return_red = return_expr->right->DynCast<VarRef>();
    ASSERT_NE(return_local, nullptr);
    ASSERT_NE(return_red, nullptr);

    auto *main_return = main->body->statements[0]->DynCast<ReturnStmt>();
    ASSERT_NE(main_return, nullptr);
    auto *main_call = main_return->ret_value->DynCast<FunctionInvocation>();
    ASSERT_NE(main_call, nullptr);
    pick_ref = main_call->function->DynCast<VarRef>();
    ASSERT_NE(pick_ref, nullptr);
  }

  SourceManager sources;
  DiagnosticContext diagnostics{sources};
  MacroContext macros;
  TypeContext types;
  std::optional<std::unique_ptr<TranslationUnit>> translation_unit;

  VarDecl *global = nullptr;
  EnumDecl *color = nullptr;
  FunctionDecl *pick = nullptr;
  FunctionDecl *main = nullptr;
  ParamDecl *parameter = nullptr;
  EnumElemDecl *red = nullptr;
  VarDecl *local = nullptr;
  VarDecl *inner_value = nullptr;
  CompoundStmt *inner_block = nullptr;
  VarRef *local_value_ref = nullptr;
  VarRef *inner_global_ref = nullptr;
  VarRef *assignment_local = nullptr;
  VarRef *assignment_value = nullptr;
  VarRef *return_local = nullptr;
  VarRef *return_red = nullptr;
  VarRef *pick_ref = nullptr;
};

TEST_F(DeclarationBinding, BindsParametersGlobalsAndLocals) {
  EXPECT_EQ(local_value_ref->decl, parameter);
  EXPECT_EQ(inner_global_ref->decl, global);
  EXPECT_EQ(assignment_local->decl, local);
  EXPECT_EQ(assignment_value->decl, inner_value);
  EXPECT_EQ(return_local->decl, local);
}

TEST_F(DeclarationBinding, BindsEnumAndFunctionReferences) {
  EXPECT_EQ(return_red->decl, red);
  EXPECT_EQ(pick_ref->decl, pick);
}
