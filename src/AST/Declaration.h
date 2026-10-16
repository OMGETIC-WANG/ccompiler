#pragma once

#include "AST.h"

#include <vector>

struct TypeSpecifier {
  std::string_view type;
  bool is_struct = false;
  bool is_builtin = false;
  bool is_enum = false;
  bool is_static = false;
};

struct DeclarationSpecifier {
  TypeSpecifier type_spec;
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

class DeclBase {
public:
  virtual ~DeclBase() = default;

  virtual DeclType GetType() const = 0;

  void Print(std::ostream &out) const;
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
  size_t pointer_count = 0;
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

class InitDeclarator : public DeclBase {
  DeclBaseImpl(InitDeclarator);

public:
  std::unique_ptr<Declarator> declarator;
  std::unique_ptr<Initializer> initializer;
};

class Declaration : public DeclBase {
  DeclBaseImpl(Declaration);

public:
  DeclarationSpecifier declaration_specifier;
  std::vector<std::unique_ptr<InitDeclarator>> init_declarators;
};

std::unique_ptr<Declaration> ParseDeclaration(FilteredLexer &lexer);
