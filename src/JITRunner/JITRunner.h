#pragma once

#include <cstdint>
#include <memory>

#include <llvm/Support/Error.h>

namespace llvm {
class LLVMContext;
class Module;
} // namespace llvm

llvm::Expected<int32_t>
RunLLVMModule(std::unique_ptr<llvm::Module> module,
              std::unique_ptr<llvm::LLVMContext> context);
