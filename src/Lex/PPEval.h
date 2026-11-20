#pragma once

#include "../AST/AST.h"
#include "../Util/Diagnostic.h"

#include <cstdint>
#include <optional>

std::optional<uint64_t> EvalMacroCond(ASTNode *node,
                                      DiagnosticContext &diagnostics);
