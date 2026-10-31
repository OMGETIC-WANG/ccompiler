#include "Generator.h"

#include "GenDecl.h"
#include "GenExpr.h"
#include "IRBuilder.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>

#include <string>
#include <vector>

void GenFunctionBodyIR(CompoundStmt *stmts, ExprEmitter &emitter) {
  for (const auto &statement : stmts->statements) {
    emitter.EmitStmt(statement.get());
  }
}

static TypeRef GetParameterType(TypeRef type, TypeContext &types) {
  if (auto *array = type->DynCast<ArrayType>()) {
    return types.Pointer(array->element_type);
  }
  return type;
}

static void FinalizeFunctionBlocks(llvm::Function *function,
                                   GenContext &context, IRBuilder &builder) {
  TypeRef return_type =
      context.current_function_type->As<FunctionType>()->return_type;
  for (auto &block : *function) {
    if (block.hasTerminator()) {
      continue;
    }
    builder.SetInsertPoint(&block);
    if (return_type->Is<BuiltinType>() &&
        return_type->As<BuiltinType>()->Kind() == BuiltinKind::Void) {
      builder.CreateVoidRet();
    } else if (function->getName() == "main" &&
               context.LLVMType(return_type)->isIntegerTy(32)) {
      builder.CreateRet(RValue{
          return_type,
          llvm::ConstantInt::get(context.LLVMType(return_type), 0, true)});
    } else {
      builder.LLVM().CreateUnreachable();
    }
  }
}

void GenFunction(FunctionDecl *decl, IRBuilder &builder, GenContext &context) {
  TypeRef resolved = decl->function_type;
  ASSERT(resolved->Is<FunctionType>());
  auto *function_type = resolved->As<FunctionType>();
  auto *llvm_function_type = context.LLVMFunctionType(resolved);
  auto linkage = decl->storage_class == StorageClass::Static
                     ? llvm::GlobalValue::InternalLinkage
                     : llvm::GlobalValue::ExternalLinkage;

  llvm::Function *function = context.Module().getFunction(decl->name);
  if (function) {
    ASSERT(function->getFunctionType() == llvm_function_type,
           "Conflicting declaration for function ", decl->name);
  } else {
    function = llvm::Function::Create(llvm_function_type, linkage, decl->name,
                                      context.Module());
  }
  function->setLinkage(linkage);
  context.AddFunction(decl->name, FunctionValue{resolved, function});

  if (!decl->body) {
    return;
  }
  ASSERT(function->empty(), "Multiple definitions of function ", decl->name);

  GenContextLayerGuard guard(context);
  context.current_function = function;
  context.current_function_type = resolved;

  auto *entry =
      llvm::BasicBlock::Create(context.LLVMContext(), "entry", function);
  builder.SetInsertPoint(entry);

  for (size_t i = 0; i < function_type->parameters.size(); ++i) {
    std::string_view parameter_name = decl->parameter_names[i];
    if (parameter_name.empty()) {
      continue;
    }
    TypeRef parameter_type =
        GetParameterType(function_type->parameters[i], context.Types());
    LValue slot = builder.CreateAlloca(parameter_type);
    context.CurrLayer().variables[parameter_name] = slot;
    builder.CreateStore(
        RValue{parameter_type, function->getArg(static_cast<unsigned>(i))},
        slot);
  }

  ExprEmitter emitter(builder, context);
  GenFunctionBodyIR(decl->body.get(), emitter);
  FinalizeFunctionBlocks(function, context, builder);
}

std::unique_ptr<llvm::Module>
GenerateLLVMModule(const TranslationUnit *tu, TypeContext &types,
                   llvm::LLVMContext &llvm_context) {
  ASSERT(tu != nullptr && tu->body, "Translation unit must have a body");

  auto module = std::make_unique<llvm::Module>("ccompiler", llvm_context);

  if (llvm::InitializeNativeTarget()) {
    RAISE_ERROR("Failed to initialize the native LLVM target");
  }
  std::string target_error;
  std::string triple_name = llvm::sys::getDefaultTargetTriple();
  llvm::Triple triple(triple_name);
  const llvm::Target *target =
      llvm::TargetRegistry::lookupTarget(triple, target_error);
  ASSERT(target, "Unable to look up LLVM target: ", target_error);
  llvm::TargetOptions target_options;
  std::unique_ptr<llvm::TargetMachine> target_machine(
      target->createTargetMachine(triple, "", "", target_options,
                                  std::nullopt));
  ASSERT(target_machine != nullptr, "Unable to create LLVM target machine");
  module->setTargetTriple(triple);
  module->setDataLayout(target_machine->createDataLayout());

  GenContext context(llvm_context, *module, types);
  context.PushLayer();
  IRBuilder builder(context);

  for (const auto &child : tu->body->statements) {
    switch (child->GetType()) {
    case ASTNodeType::RawLLVMIR:
      RAISE_ERROR("RawLLVMIR is no longer supported by LLVM API code "
                  "generation");
    case ASTNodeType::FunctionDecl:
      GenFunction(child->As<FunctionDecl>(), builder, context);
      break;
    case ASTNodeType::StructDecl:
      GenStructDecl(child->As<StructDecl>(), context);
      break;
    case ASTNodeType::EnumDecl:
      GenEnumDecl(child->As<EnumDecl>(), context);
      break;
    case ASTNodeType::VarDecl:
      GenGlobalVarDecl(child->As<VarDecl>(), context);
      break;
    case ASTNodeType::TypedefDecl:
      break;
    default:
      RAISE_ERROR("Unsupported top-level AST node: ", child->GetType());
    }
  }

  std::string verification_error;
  llvm::raw_string_ostream error_stream(verification_error);
  if (llvm::verifyModule(*module, &error_stream)) {
    error_stream.flush();
    RAISE_ERROR("Invalid LLVM module:\n", verification_error);
  }
  return module;
}
