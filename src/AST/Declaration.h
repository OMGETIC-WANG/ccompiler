#pragma once

#include "../Lexer.h"
#include "../Util/Tree.h"
#include "ASTNode.h"
#include "Context.h"
#include "Type.h"
#include <vector>

enum class StorageClass { None, Static };

struct DeclarationSpecifier {
  TypeSpecifier type_spec;
  StorageClass storage_class = StorageClass::None;
  std::unique_ptr<ASTNode> anoymous_decl;
};

// clang-format off
#define DECL_TYPE_VALUES(x)\
  x(Declaration)\
  x(InitDeclarator)\
  x(Declarator)\
  x(DirectDeclarator)\
  x(Pointer)\
  x(IdentifierDeclarator)\
  x(ArraySuffix)\
  x(ParamList)\
  x(Initializer)\
  x(ValueInitializer)\
  x(FieldInitializer)
// clang-format on

MAKE_ENUM(DeclType, DECL_TYPE_VALUES)
REGISTER_ENUM(DeclType, DECL_TYPE_VALUES)

class DeclBase : public TreeNode {
public:
  virtual ~DeclBase() = default;

  virtual DeclType GetType() const = 0;

  std::string_view GetNodeTypeName() const override;

  template <std::derived_from<DeclBase> T> const T *As() const {
#ifdef ENABLE_AS_TYPE_CHECK
    if (GetType() != T::__type) {
      RAISE_ERROR("Invalid ASTNode type cast from ", GetType(), " to ",
                  T::__type);
    }
#endif
    return static_cast<const T *>(this);
  }

  template <std::derived_from<DeclBase> T> T *As() {
    return const_cast<T *>(std::as_const(*this).template As<T>());
  }

  template <std::derived_from<DeclBase> T> const T *DynCast() const {
    if (GetType() != T::__type) {
      return nullptr;
    }
    return static_cast<const T *>(this);
  }

  template <std::derived_from<DeclBase> T> T *DynCast() {
    return const_cast<T *>(std::as_const(*this).template DynCast<T>());
  }
};

#define DeclBaseImpl(type_enum)                                                \
public:                                                                        \
  DeclType GetType() const override { return DeclType::type_enum; }            \
  constexpr static DeclType __type = DeclType::type_enum

class DirectDeclarator : public DeclBase {
  DeclBaseImpl(DirectDeclarator);
  TreeNodePrintFields(declarator, suffixes);

public:
  std::unique_ptr<DeclBase> declarator;
  std::vector<std::unique_ptr<DeclBase>> suffixes;
};

class Declarator : public DeclBase {
  DeclBaseImpl(Declarator);
  TreeNodePrintFields(direct_declarator);

public:
  std::string ToString() const override {
    return "pointers=" + std::to_string(pointers.size());
  }

  struct PointerQualifier {
    bool is_const = false;
  };

  std::vector<PointerQualifier> pointers;
  std::unique_ptr<DirectDeclarator> direct_declarator;
};

class IdentifierDeclarator : public DeclBase {
  DeclBaseImpl(IdentifierDeclarator);
  TreeNodeFieldAsString(identifier);

public:
  IdentifierDeclarator(std::string_view id = {}) : identifier(id) {}

  std::string_view identifier;
};

class ArraySuffix : public DeclBase {
  DeclBaseImpl(ArraySuffix);

public:
  std::string ToString() const override {
    return size == (size_t)-1 ? "[]" : "[" + std::to_string(size) + "]";
  }

  size_t size = (size_t)-1; // default undecleared
};

class ParamListItem {
public:
  DeclarationSpecifier specifier;
  std::unique_ptr<Declarator> declarator;
};

class ParamList : public DeclBase {
  DeclBaseImpl(ParamList);

public:
  std::vector<TreeNodePrintField> GetPrintFields() const override {
    std::vector<TreeNodePrintField> print_fields;
    for (const auto &param : params) {
      if (param.declarator) {
        AppendTreeNodePrintField(print_fields, "declarator", param.declarator);
      } else {
        AppendTreeNodePrintField(print_fields, "parameter",
                                 param.specifier.type_spec.type_name);
      }
    }
    return print_fields;
  }

  std::vector<ParamListItem> params;
};

class Initializer : public DeclBase {
  DeclBaseImpl(Initializer);
};

class ValueInitializer : public Initializer {
  DeclBaseImpl(ValueInitializer);
  TreeNodePrintFields(value);

public:
  std::unique_ptr<ASTNode> value;
};

class FieldInitializer : public Initializer {
  DeclBaseImpl(FieldInitializer);

public:
  std::vector<TreeNodePrintField> GetPrintFields() const override {
    std::vector<TreeNodePrintField> print_fields;
    for (const auto &[name, initializer] : field_initializers) {
      AppendTreeNodePrintField(print_fields, name, initializer);
    }
    return print_fields;
  }

  std::vector<std::pair<std::string_view, std::unique_ptr<Initializer>>>
      field_initializers;
};

class InitDeclarator : public DeclBase {
  DeclBaseImpl(InitDeclarator);
  TreeNodePrintFields(declarator, initializer);

public:
  std::unique_ptr<Declarator> declarator;
  std::unique_ptr<Initializer> initializer;
};

class Declaration : public DeclBase {
  DeclBaseImpl(Declaration);
  TreeNodePrintFields(init_declarators);

public:
  std::string ToString() const override {
    return std::string(declaration_specifier.type_spec.type_name);
  }

  DeclarationSpecifier declaration_specifier;
  std::vector<std::unique_ptr<InitDeclarator>> init_declarators;
};

bool IsDeclarationSpecifier(Token tok, const CompileContext &context);

DeclarationSpecifier ParseDeclarationSpecifier(FilteredLexer &lexer,
                                               CompileContext &context);

std::unique_ptr<Declaration> ParseDeclaration(FilteredLexer &lexer,
                                              CompileContext &context);
