#pragma once

#include "../../AST/TranslationUnit.h"
#include "GenContext.h"
#include "IRBuilder.h"

std::string GenerateLLVMIR(const TranslationUnit *TU, TypeContext &types);
