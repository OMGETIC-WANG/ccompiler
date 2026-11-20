#pragma once

#include "../Exception.h"
#include "../SourceManager.h"
#include "../Util/Diagnostic.h"
#include "../Util/EnumRefl.h"
#include "TypeContext.h"

#include <memory>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

class EnumDecl;
class EnumElemDecl;
class FunctionDecl;
class NamedDecl;
class ParamDecl;
class StructDecl;
class TypedefDecl;
class UnionDecl;
class ValueDecl;
class VarDecl;

// clang-format off
#define CONTEXT_ITEM_TYPE_VALUES(x)\
  x(Function)\
  x(Typedef)\
  x(Struct)\
  x(Union)\
  x(Variable)\
  x(Parameter)\
  x(Enumerator)\
  x(Enum)\
  x(Unknown)\
  x(Label)
// clang-format on

MAKE_ENUM(ContextItemType, CONTEXT_ITEM_TYPE_VALUES)
REGISTER_ENUM(ContextItemType, CONTEXT_ITEM_TYPE_VALUES)

struct CompileContextSymbol {
  ContextItemType type = ContextItemType::Unknown;
  NamedDecl *decl = nullptr;
};

struct CompileContextLayer {
  std::unordered_map<std::string_view, CompileContextSymbol> ordinary_decls;
  std::unordered_map<std::string_view, CompileContextSymbol> tag_decls;
  std::vector<std::string_view> labels;

  void AddLabel(std::string_view name) { labels.push_back(name); }
};

// Lexical symbol table used while building the AST. The context borrows
// declaration addresses; the translation unit owns all source declarations.
class CompileContext {
public:
  explicit CompileContext(TypeContext &types, SourceManager &source_manager,
                          DiagnosticContext &diagnostics);
  ~CompileContext();

  std::vector<CompileContextLayer> layers;

  void AddFunction(FunctionDecl *decl);
  void AddTypedef(TypedefDecl *decl);
  void AddStruct(StructDecl *decl);
  void AddUnion(UnionDecl *decl);
  void AddVariable(VarDecl *decl);
  void AddParameter(ParamDecl *decl);
  void AddEnumerator(EnumElemDecl *decl);
  void AddEnum(EnumDecl *decl);
  void AddLabel(std::string_view name) { layers.back().AddLabel(name); }
  void AddSyntheticTypedef(std::string_view name, TypeRef type);

  CompileContextLayer &CurrentLayer() { return layers.back(); }
  const CompileContextLayer &CurrentLayer() const { return layers.back(); }

  TypeContext &Types() { return _types; }
  const TypeContext &Types() const { return _types; }

  SourceManager &Sources() { return _source_manager; }
  const SourceManager &Sources() const { return _source_manager; }

  DiagnosticContext &Diagnostics() { return _diagnostics; }
  const DiagnosticContext &Diagnostics() const { return _diagnostics; }

  void RequestParserRecovery() { _parser_recovery_pending = true; }

  bool ConsumeParserRecovery() {
    return std::exchange(_parser_recovery_pending, false);
  }

  ScopeId CurrentScopeId() { return _next_scope_id++; }

  FunctionDecl *FindFunction(std::string_view name) const;
  TypedefDecl *FindTypedef(std::string_view name) const;
  StructDecl *FindStruct(std::string_view name) const;
  UnionDecl *FindUnion(std::string_view name) const;
  VarDecl *FindVariable(std::string_view name) const;
  ParamDecl *FindParameter(std::string_view name) const;
  EnumElemDecl *FindEnumerator(std::string_view name) const;
  EnumDecl *FindEnum(std::string_view name) const;
  ValueDecl *FindValue(std::string_view name) const;

  void PushLayer() { layers.emplace_back(); }
  void PopLayer() {
    ASSERT(!layers.empty(), "No context layer to remove");
    layers.pop_back();
  }

  ContextItemType GetTypeOf(std::string_view name) const;
  bool Is(std::string_view name, ContextItemType type) const;

  // nullopt means a diagnostic already stopped type resolution; a contained
  // null TypeRef means the specifier could not be resolved and the caller must
  // report it.
  std::optional<TypeRef> GetType(const TypeSpecifier &spec) const;

private:
  const CompileContextSymbol *FindOrdinary(std::string_view name) const;
  const CompileContextSymbol *FindTag(std::string_view name) const;

  TypeContext &_types;
  SourceManager &_source_manager;
  DiagnosticContext &_diagnostics;
  ScopeId _next_scope_id;
  bool _parser_recovery_pending = false;
  std::vector<std::unique_ptr<TypedefDecl>> _synthetic_typedefs;
};

class CompileContextLayerGuard {
public:
  CompileContextLayerGuard(CompileContext &context) : _context(context) {
    _context.PushLayer();
  }
  ~CompileContextLayerGuard() { _context.PopLayer(); }

private:
  CompileContext &_context;
};
