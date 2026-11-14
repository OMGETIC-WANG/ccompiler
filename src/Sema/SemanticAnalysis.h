#pragma once

class DiagnosticContext;
class TranslationUnit;
class TypeContext;

// Type-checks and annotates a parsed translation unit before LLVM lowering.
// The translation unit, type context, and diagnostic context must outlive the
// analysis call.
void AnalyzeTranslationUnit(TranslationUnit &translation_unit,
                            TypeContext &types, DiagnosticContext &diagnostics);
