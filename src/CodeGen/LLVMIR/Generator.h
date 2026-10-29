#pragma once

#include "../../AST/TranslationUnit.h"

#include <memory>

namespace llvm {
class LLVMContext;
class Module;
} // namespace llvm

std::unique_ptr<llvm::Module> GenerateLLVMModule(const TranslationUnit *tu,
                                                 TypeContext &types,
                                                 llvm::LLVMContext &context);
