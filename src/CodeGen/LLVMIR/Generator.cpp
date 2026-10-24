#include "Generator.h"
#include "../../Util/StringVariant.h"
#include "GenContext.h"
#include "GenDecl.h"
#include "GenExpr.h"
#include "Generator.h"
#include "Naming.h"

std::string GenFunctionParamsIR(FunctionDecl *decl, const FunctionType *type,
                                GenContext &context) {
  std::string res;
  for (size_t i = 0; i < type->parameters.size(); i++) {
    auto type_s = GetTypeIRName(type->parameters[i]);
    auto name_s = decl->parameter_names[i];
    res += std::format("{} {} %{}", (i > 0 ? "," : ""), type_s, name_s);
    context.CurrLayer().variables[name_s] =
        IRValue::LocalReg(type->parameters[i], std::string(name_s));
  }
  return res;
}

void GenFunctionBodyIR(CompoundStmt *stmts, ExprEmitter &emitter) {
  for (const auto &statement : stmts->statements) {
    if (auto compound_stmt = statement->DynCast<CompoundStmt>()) {
      GenContextLayerGuard guard(emitter.context);
      GenFunctionBodyIR(compound_stmt, emitter);
    } else {
      emitter.EmitStmt(statement.get());
    }
  }
}

void GenFunction(FunctionDecl *decl, IRBuilder &builder, GenContext &context) {
  TypeRef resolved = decl->function_type;
  ASSERT(resolved->Is<FunctionType>());
  auto *func_type = resolved->As<FunctionType>();
  context.CurrLayer().functions[decl->name] = func_type;
  GenContextLayerGuard guard(context);

  auto ret_s = GetTypeIRName(func_type->return_type);
  auto params_s = GenFunctionParamsIR(decl, func_type, context);
  if (!decl->body->statements.empty()) {
    builder.Buffer() +=
        std::format("define {} @{}({}){{\n", ret_s, decl->name, params_s);
    ExprEmitter emitter(builder, context);
    builder.CreateLabel("entry");
    GenFunctionBodyIR(decl->body.get(), emitter);
    builder.Buffer() += "}\n";
  } else {
    builder.Buffer() +=
        std::format("declare {} @{}({})\n", ret_s, decl->name, params_s);
  }
}

std::string StrToLLVMLiteralStr(std::string_view s) {
  std::string res;
  res.reserve(s.size() * 3);
  for (char c : s) {
    res.push_back('\\');
    uint8_t h0 = c / 16, h1 = c % 16;
    res.push_back(h0 >= 10 ? h0 - 10 + 'a' : h0 + '0');
    res.push_back(h1 >= 10 ? h1 - 10 + 'a' : h1 + '0');
  }
  return res;
}

std::string GenLiteralStrings(GenContext &context) {
  std::string ir;
  for (const auto &[symbol, value] : context.literal_strings) {
    ir +=
        std::format("{}=private unnamed addr constant [{} x i8] c\"{}\\00\"\n",
                    symbol, value.size() + 1, StrToLLVMLiteralStr(value));
  }
  return ir;
}

std::string GenerateLLVMIR(const TranslationUnit *TU, TypeContext &types) {
  std::string ir;
  ir.reserve(16 * 1024);
  GenContext context(types);
  context.PushLayer();
  IRBuilder builder(ir, context);
  for (const auto &child : TU->body->statements) {
    switch (child->GetType()) {
    case ASTNodeType::FunctionDecl: {
      GenFunction(child->As<FunctionDecl>(), builder, context);
      break;
    }
    case ASTNodeType::StructDecl: {
      ir += GenStructDecl(child->As<StructDecl>(), context);
      break;
    }
    case ASTNodeType::EnumDecl: {
      ir += GenEnumDecl(child->As<EnumDecl>(), context);
      break;
    }
    default: {
      RAISE_ERROR("Unsupported yet");
    }
    }
  }
  ir = GenLiteralStrings(context) + ir;
  return ir;
}
