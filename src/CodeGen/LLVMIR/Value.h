#pragma once

#include "../../AST/Type.h"

#include <llvm/IR/Value.h>

struct LValue {
  TypeRef c_type = nullptr;
  llvm::Value *address = nullptr;
};

struct RValue {
  TypeRef c_type = nullptr;
  llvm::Value *ir_value = nullptr;
};
