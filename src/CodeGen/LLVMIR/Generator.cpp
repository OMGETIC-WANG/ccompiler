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
    auto param_type = type->parameters[i];
    if (auto array_param_t = param_type->DynCast<ArrayType>()) {
      param_type = context.Types().Pointer(array_param_t->element_type);
    }
    auto param_type_s = GetTypeIRName(param_type);
    auto param_name = decl->parameter_names[i];
    if (param_name.empty()) {
      res += std::format("{} {}", (i > 0 ? "," : ""), param_type_s);
    } else {
      res += std::format("{} {} %{}", (i > 0 ? "," : ""), param_type_s,
                         param_name);
    }
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
  std::string func_symbol = std::format("@{}", decl->name);
  context.AddFunction(decl->name, IRValue::Constant(func_type, func_symbol));
  GenContextLayerGuard guard(context);

  auto ret_s = GetTypeIRName(func_type->return_type);
  auto params_s = GenFunctionParamsIR(decl, func_type, context);
  const std::string_view linkage =
      decl->storage_class == StorageClass::Static ? "internal " : "";
  if (decl->body) {
    builder.Buffer() += std::format("{}define {} @{}({}){{\n", linkage, ret_s,
                                    decl->name, params_s);
    ExprEmitter emitter(builder, context);
    builder.CreateLabel("entry");

    for (size_t i = 0; i < func_type->parameters.size(); i++) {
      auto param_type = func_type->parameters[i];
      if (auto array_param_t = param_type->DynCast<ArrayType>()) {
        param_type = context.Types().Pointer(array_param_t->element_type);
      }
      auto param_name = decl->parameter_names[i];
      if (param_name.empty())
        continue;

      IRValue param_val = builder.CreateAlloca(param_type);
      context.CurrLayer().variables[param_name] = param_val;
      builder.CreateStore(
          IRValue::LocalReg(param_type, std::format("%{}", param_name)),
          param_val);
    }

    GenFunctionBodyIR(decl->body.get(), emitter);
    builder.Buffer() += "}\n";
  } else {
    builder.Buffer() += std::format("{}declare {} @{}({})\n", linkage, ret_s,
                                    decl->name, params_s);
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
    case ASTNodeType::RawLLVMIR: {
      ir += "\n; raw LLVM IR begin\n";
      ir += child->As<RawLLVMIR>()->ir;
      ir += "\n; raw LLVM IR end\n";
      break;
    }
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
    case ASTNodeType::VarDecl: {
      auto *var_decl = child->As<VarDecl>();
      std::string symbol = std::format("@{}", var_decl->name);
      context.CurrLayer().variables[var_decl->name] =
          IRValue::Global(var_decl->type, symbol);
      ir += std::format("{}=dso_local global {},align {}\n", symbol,
                        GenGlobalVarInit(var_decl->type.get(),
                                         var_decl->initializer.get(), context),
                        var_decl->type->GetAlign());
      break;
    }
    default: {
      RAISE_ERROR("Unsupported yet");
    }
    }
  }

  for (const auto &struct_decl : context.struct_decls) {
    ir += struct_decl;
  }
  ir += GenLiteralStrings(context);
  return ir;
}
