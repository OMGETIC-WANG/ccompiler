#pragma once

#include <optional>
#include <variant>

class DiagnosticContext;
class TranslationUnit;
class TypeContext;

// Type-checks and annotates a parsed translation unit before LLVM lowering.
// The translation unit, type context, and diagnostic context must outlive the
// analysis call.
std::optional<std::monostate>
AnalyzeTranslationUnit(TranslationUnit &translation_unit, TypeContext &types,
                       DiagnosticContext &diagnostics);
