#include "DeclBindingTest.h"

#include "AST/AST.h"
#include "AST/Operations.h"
#include "AST/TranslationUnit.h"
#include "AST/TypeContext.h"
#include "CodeGen/LLVMIR/Intrinsics.h"
#include "Lex/Lexer.h"
#include "Lex/Macro.h"
#include "SourceManager.h"
#include "Util/Diagnostic.h"

#include <iostream>
#include <string>
#include <string_view>

namespace {

template <typename T> T *RequireNode(ASTNode *node, std::string_view context) {
  if (!node) {
    std::cerr << "FAIL declaration binding: missing node for " << context
              << '\n';
    return nullptr;
  }
  T *result = node->DynCast<T>();
  if (!result) {
    std::cerr << "FAIL declaration binding: unexpected "
              << node->GetNodeTypeName() << " for " << context << '\n';
  }
  return result;
}

template <typename T> T *RequireDecl(DeclBase *decl, std::string_view context) {
  if (!decl) {
    std::cerr << "FAIL declaration binding: missing declaration for " << context
              << '\n';
    return nullptr;
  }
  T *result = decl->DynCast<T>();
  if (!result) {
    std::cerr << "FAIL declaration binding: unexpected declaration for "
              << context << '\n';
  }
  return result;
}

bool RequireEqual(const void *actual, const void *expected,
                  std::string_view context) {
  if (actual == expected) {
    return true;
  }
  std::cerr << "FAIL declaration binding: wrong declaration for " << context
            << '\n';
  return false;
}

} // namespace

int RunDeclBindingSelfTest() {
  constexpr std::string_view source_text = R"(
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

  SourceManager sources;
  SourceHandle source =
      sources.LoadSource(std::string(source_text), "<decl-binding>");
  MacroContext macros;
  TypeContext types;
  DiagnosticContext diagnostics(sources);
  CompileContext context(types, sources, diagnostics);
  RegisterBuiltinIntrinsics();
  RawLexer raw_lexer(source, macros, sources, context);
  Lexer lexer(raw_lexer);
  auto translation_unit =
      ParseTranslationUnit(lexer, types, sources, diagnostics);
  if (diagnostics.HasErrors() || translation_unit->decls.size() != 4) {
    std::cerr << "FAIL declaration binding: unexpected parse result\n";
    return 1;
  }

  auto *global =
      RequireNode<VarDecl>(translation_unit->decls[0].get(), "global");
  auto *color =
      RequireNode<EnumDecl>(translation_unit->decls[1].get(), "Color");
  auto *pick =
      RequireNode<FunctionDecl>(translation_unit->decls[2].get(), "pick");
  auto *main =
      RequireNode<FunctionDecl>(translation_unit->decls[3].get(), "main");
  if (!global || !color || !pick || !main || color->values.empty() ||
      pick->parameters.size() != 1 || pick->body->statements.size() != 3 ||
      main->body->statements.size() != 1) {
    std::cerr << "FAIL declaration binding: unexpected declaration shape\n";
    return 1;
  }

  ParamDecl *parameter = pick->parameters[0].get();
  EnumElemDecl *red = color->values[0].get();
  auto *local = RequireNode<VarDecl>(pick->body->statements[0].get(), "local");
  auto *inner_block =
      RequireNode<CompoundStmt>(pick->body->statements[1].get(), "inner block");
  if (!local || !inner_block || inner_block->statements.size() != 2) {
    return 1;
  }
  auto *local_initializer = RequireDecl<ValueInitializer>(
      local->initializer.get(), "local initializer");
  auto *local_value_ref =
      local_initializer
          ? RequireNode<VarRef>(local_initializer->value.get(), "local value")
          : nullptr;
  auto *inner_value =
      RequireNode<VarDecl>(inner_block->statements[0].get(), "inner value");
  auto *assignment =
      RequireNode<BinaryOp>(inner_block->statements[1].get(), "assignment");
  if (!local_value_ref || !inner_value || !assignment) {
    return 1;
  }
  auto *inner_initializer = RequireDecl<ValueInitializer>(
      inner_value->initializer.get(), "inner value initializer");
  auto *inner_global_ref =
      inner_initializer ? RequireNode<VarRef>(inner_initializer->value.get(),
                                              "inner value initializer")
                        : nullptr;
  auto *assignment_local =
      RequireNode<VarRef>(assignment->left.get(), "assignment local");
  auto *assignment_value =
      RequireNode<VarRef>(assignment->right.get(), "assignment value");
  auto *return_stmt =
      RequireNode<ReturnStmt>(pick->body->statements[2].get(), "pick return");
  auto *return_expr =
      return_stmt
          ? RequireNode<BinaryOp>(return_stmt->ret_value.get(), "pick return")
          : nullptr;
  if (!inner_global_ref || !assignment_local || !assignment_value ||
      !return_expr) {
    return 1;
  }
  auto *return_local =
      RequireNode<VarRef>(return_expr->left.get(), "return local");
  auto *return_red =
      RequireNode<VarRef>(return_expr->right.get(), "return Red");
  auto *main_return =
      RequireNode<ReturnStmt>(main->body->statements[0].get(), "main return");
  auto *main_call = main_return ? RequireNode<FunctionInvocation>(
                                      main_return->ret_value.get(), "main call")
                                : nullptr;
  auto *pick_ref = main_call ? RequireNode<VarRef>(main_call->function.get(),
                                                   "main call target")
                             : nullptr;
  if (!return_local || !return_red || !pick_ref) {
    return 1;
  }

  bool ok = true;
  ok &= RequireEqual(local_value_ref->decl, parameter, "parameter value");
  ok &=
      RequireEqual(inner_global_ref->decl, global, "translation-unit variable");
  ok &= RequireEqual(assignment_local->decl, local, "outer local");
  ok &= RequireEqual(assignment_value->decl, inner_value, "inner local");
  ok &= RequireEqual(return_local->decl, local, "restored outer local");
  ok &= RequireEqual(return_red->decl, red, "enum constant");
  ok &= RequireEqual(pick_ref->decl, pick, "function declaration");
  if (!ok) {
    return 1;
  }

  std::cout << "PASS declaration binding\n";
  return 0;
}
