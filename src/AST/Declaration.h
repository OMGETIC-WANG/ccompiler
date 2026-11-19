#pragma once

#include "../Lex/Lexer.h"
#include "../Util/Tree.h"
#include "ASTNode.h"
#include "Context.h"
#include "Type.h"
#include <vector>

enum class StorageClass { None, Static, Extern };

struct DeclarationSpecifier {
  TypeSpecifier type_spec;
  StorageClass storage_class = StorageClass::None;
  std::unique_ptr<ASTNode> decl;
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
  x(FieldInitializer)\
  x(ArrayInitializer)
// clang-format on

MAKE_ENUM(DeclType, DECL_TYPE_VALUES)
REGISTER_ENUM(DeclType, DECL_TYPE_VALUES)

class DeclBase : public LocatableNode {
public:
  virtual ~DeclBase() = default;

  virtual DeclType GetType() const = 0;

  std::string_view GetNodeTypeName() const override;

  template <std::derived_from<DeclBase> T> T *As() {
#ifdef ENABLE_AS_TYPE_CHECK
    if (GetType() != T::__type) {
      RAISE_ERROR("Invalid ASTNode type cast from ", GetType(), " to ",
                  T::__type);
    }
#endif
    return static_cast<T *>(this);
  }

  template <std::derived_from<DeclBase> T> const T *As() const {
#ifdef ENABLE_AS_TYPE_CHECK
    if (GetType() != T::__type) {
      RAISE_ERROR("Invalid ASTNode type cast from ", GetType(), " to ",
                  T::__type);
    }
#endif
    return static_cast<const T *>(this);
  }

  template <std::derived_from<DeclBase> T> T *DynCast() {
    if (GetType() != T::__type) {
      return nullptr;
    }
    return static_cast<T *>(this);
  }

  template <std::derived_from<DeclBase> T> const T *DynCast() const {
    if (GetType() != T::__type) {
      return nullptr;
    }
    return static_cast<const T *>(this);
  }

  template <std::derived_from<DeclBase> T> bool Is() const {
    return GetType() == T::__type;
  }
};

#define DeclBaseImpl(type_enum)                                                \
public:                                                                        \
  DeclType GetType() const override { return DeclType::type_enum; }            \
  constexpr static DeclType __type = DeclType::type_enum

class DirectDeclarator : public DeclBase {
  DeclBaseImpl(DirectDeclarator);

public:
  std::unique_ptr<DeclBase> declarator;
  std::vector<std::unique_ptr<DeclBase>> suffixes;
};

class Declarator : public DeclBase {
  DeclBaseImpl(Declarator);

public:
  std::string ToString() const override {
    return "pointers=" + std::to_string(pointers.size());
  }

  struct PointerQualifier {
    bool is_const = false;
    bool is_volatile = false;
    bool is_restrict = false;
  };

  std::vector<PointerQualifier> pointers;
  std::unique_ptr<DirectDeclarator> direct_declarator;
};

class IdentifierDeclarator : public DeclBase {
  DeclBaseImpl(IdentifierDeclarator);

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
  std::vector<ParamListItem> params;
  bool has_va_args = false;
};

class Initializer : public DeclBase {
  DeclBaseImpl(Initializer);
};

class ValueInitializer : public Initializer {
  DeclBaseImpl(ValueInitializer);

public:
  std::unique_ptr<ASTNode> value;
};

class FieldInitializer : public Initializer {
  DeclBaseImpl(FieldInitializer);

public:
  std::vector<std::pair<std::string_view, std::unique_ptr<Initializer>>>
      field_initializers;
};

class ArrayInitializer : public Initializer {
  DeclBaseImpl(ArrayInitializer);

public:
  std::vector<std::unique_ptr<Initializer>> initializers;
};

class InitDeclarator : public DeclBase {
  DeclBaseImpl(InitDeclarator);

public:
  std::unique_ptr<Declarator> declarator;
};

class Declaration : public DeclBase {
  DeclBaseImpl(Declaration);

public:
  std::string ToString() const override {
    return std::string(declaration_specifier.type_spec.type_name);
  }

  DeclarationSpecifier declaration_specifier;
  std::vector<std::unique_ptr<InitDeclarator>> init_declarators;
};

bool IsDeclarationSpecifier(Token tok, const CompileContext &context);

DeclarationSpecifier ParseDeclarationSpecifier(Lexer &lexer,
                                               CompileContext &context);

std::unique_ptr<ASTNode> ParseDeclaration(Lexer &lexer,
                                          CompileContext &context);

TypeRef ParseTypeName(Lexer &lexer, CompileContext &context);

bool IsConstantFoldable(const Initializer *initializer);
