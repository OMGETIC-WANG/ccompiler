#include "JITRunner.h"

#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/TargetSelect.h>

#include <string>

static llvm::Error ValidateMain(llvm::Module &module) {
  llvm::Function *main_function = module.getFunction("main");
  if (!main_function) {
    return llvm::make_error<llvm::StringError>(
        "JIT execution requires a main function",
        llvm::inconvertibleErrorCode());
  }
  if (main_function->isDeclaration()) {
    return llvm::make_error<llvm::StringError>(
        "JIT execution requires a definition of main",
        llvm::inconvertibleErrorCode());
  }

  llvm::FunctionType *function_type = main_function->getFunctionType();
  if (!function_type->getReturnType()->isIntegerTy(32)) {
    return llvm::make_error<llvm::StringError>(
        "JIT execution requires main to return int",
        llvm::inconvertibleErrorCode());
  }
  if (function_type->getNumParams() != 0 || function_type->isVarArg()) {
    return llvm::make_error<llvm::StringError>(
        "JIT execution only supports int main(void)",
        llvm::inconvertibleErrorCode());
  }
  return llvm::Error::success();
}

llvm::Expected<int32_t>
RunLLVMModule(std::unique_ptr<llvm::Module> module,
              std::unique_ptr<llvm::LLVMContext> context) {
  if (!module || !context) {
    return llvm::make_error<llvm::StringError>(
        "JIT execution requires a module and LLVM context",
        llvm::inconvertibleErrorCode());
  }

  llvm::orc::ThreadSafeModule thread_safe_module(std::move(module),
                                                 std::move(context));
  if (auto error = thread_safe_module.withModuleDo(
          [](llvm::Module &module) { return ValidateMain(module); })) {
    return std::move(error);
  }

  llvm::DataLayout data_layout = thread_safe_module.withModuleDo(
      [](llvm::Module &module) { return module.getDataLayout(); });

  if (llvm::InitializeNativeTarget() ||
      llvm::InitializeNativeTargetAsmPrinter()) {
    return llvm::make_error<llvm::StringError>(
        "Failed to initialize the native LLVM target for JIT execution",
        llvm::inconvertibleErrorCode());
  }

  auto jit = llvm::orc::LLJITBuilder()
                 .setDataLayout(std::move(data_layout))
                 .setLinkProcessSymbolsByDefault(true)
                 .create();
  if (!jit) {
    return jit.takeError();
  }

  if (auto error = (*jit)->addIRModule(std::move(thread_safe_module))) {
    return std::move(error);
  }

  auto main_address = (*jit)->lookup("main");
  if (!main_address) {
    return main_address.takeError();
  }

  return main_address->toPtr<int32_t()>()();
}
