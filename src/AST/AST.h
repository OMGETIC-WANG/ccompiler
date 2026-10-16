#pragma once

#include <ostream>
#include <string>

#include "../Lexer.h"

#include "../Util/EnumRefl.h"
#include "../Util/Util.h"

// clang-format off
#define AST_NODE_TYPE_VALUES(x)\
  x(Unknown)\
  x(TranslationUnit)\
  x(FunctionDecl)\
  x(StructDecl)\
  x(LiteralString)\
  x(LiteralNum)\
  x(BinaryExpr)\
  x(UnaryExpr)\
  x(VarDecl)\
  x(VarRef)\
  x(FunctionInvocation)\
  x(SequenceEval)\
  x(CompoundStmt)\
  x(IfStmt)\
  x(WhileStmt)\
  x(EnumDecl)\
  x(EnumElemDecl)
// clang-format on

MAKE_ENUM(ASTNodeType, AST_NODE_TYPE_VALUES)
REGISTER_ENUM(ASTNodeType, AST_NODE_TYPE_VALUES)

template <class Target, class Source> constexpr bool isa(const Source *) {
  return std::is_base_of_v<Target, Source>;
}

class ASTNode {
public:
  virtual ~ASTNode() = default;

  virtual ASTNodeType GetType() const = 0;

  virtual std::string ToString() const { return {}; }

  virtual size_t GetChildCount() const { return 0; }

  virtual ASTNode *GetChild(size_t index) const { return nullptr; }

  // virtual void Parse(FilteredLexer &lexer) = 0;

  void Print(std::ostream &out) const;
};

template <class T> ASTNodeType ASTNodeTypeOf() { return T::__type; }

#define ASTNodeImpl(type_enum)                                                 \
public:                                                                        \
  ASTNodeType GetType() const override { return ASTNodeType::type_enum; }      \
  constexpr static ASTNodeType __type = ASTNodeType::type_enum

#define ASTNodeFieldAsString(field)                                            \
  std::string ToString() const override { return std::string(field); }

#define ASTNodeContainerAsChilds(container)                                    \
  size_t GetChildCount() const override { return container.size(); }           \
  ASTNode *GetChild(size_t index) const override {                             \
    if (index >= container.size())                                             \
      return nullptr;                                                          \
    return container[index].get();                                             \
  }

#define CompoundStmtsAsChilds(stmts)                                           \
  size_t GetChildCount() const override {                                      \
    return stmts ? stmts->GetChildCount() : 0;                                 \
  }                                                                            \
  ASTNode *GetChild(size_t index) const override {                             \
    if (!stmts)                                                                \
      return nullptr;                                                          \
    return stmts->GetChild(index);                                             \
  }

class TypeBase;

class LiteralString : public ASTNode {
  ASTNodeImpl(LiteralString);
  ASTNodeFieldAsString(value);

public:
  std::string_view value;
};

class LiteralNum : public ASTNode {
  ASTNodeImpl(LiteralNum);
  ASTNodeFieldAsString(value);

public:
  std::string_view value;
};

class VarDecl : public ASTNode {
  ASTNodeImpl(VarDecl);
  ASTNodeFieldAsString(name);

public:
  std::string_view name;
  std::shared_ptr<TypeBase> type;
};

class VarRef : public ASTNode {
  ASTNodeImpl(VarRef);
  ASTNodeFieldAsString(name);

public:
  std::string_view name;
};

class CompoundStmt : public ASTNode {
  ASTNodeImpl(CompoundStmt);
  ASTNodeContainerAsChilds(statements);

public:
  std::vector<std::unique_ptr<ASTNode>> statements;
};

class StructDecl : public ASTNode {
  ASTNodeImpl(StructDecl);
  CompoundStmtsAsChilds(fields);

public:
  std::string_view name;
  std::unique_ptr<CompoundStmt> fields;
};

class IfStmt : public ASTNode {
  ASTNodeImpl(IfStmt);

public:
  size_t GetChildCount() const override {
    return condition.size() + (then_branch ? 1 : 0) + (else_branch ? 1 : 0);
  }
  ASTNode *GetChild(size_t index) const override {
    if (index < condition.size())
      return condition[index].get();
    index -= condition.size();

    if (then_branch) {
      if (index == 0)
        return then_branch.get();
      --index;
    }
    if (else_branch && index == 0)
      return else_branch.get();
    return nullptr;
  }

  std::vector<std::unique_ptr<ASTNode>> condition;
  std::unique_ptr<ASTNode> then_branch;
  std::unique_ptr<ASTNode> else_branch;
};

class FunctionDecl : public ASTNode {
  ASTNodeImpl(FunctionDecl);
  ASTNodeFieldAsString(name);

public:
  size_t GetChildCount() const override { return body ? 1 : 0; }
  ASTNode *GetChild(size_t index) const override {
    if (index == 0 && body)
      return body.get();
    return nullptr;
  }

  std::string_view name;
  std::shared_ptr<TypeBase> function_type;
  std::unique_ptr<CompoundStmt> body;
};

class EnumElemDecl : public ASTNode {
  ASTNodeImpl(EnumElemDecl);
  ASTNodeFieldAsString(name);

public:
  std::string_view name;
  std::unique_ptr<ASTNode> value;
};

class EnumDecl : public ASTNode {
  ASTNodeImpl(EnumDecl);
  ASTNodeContainerAsChilds(values);

public:
  std::string_view name;
  std::vector<std::unique_ptr<EnumElemDecl>> values;
};
