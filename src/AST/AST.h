#pragma once

#include "../Util/CowString.h"
#include "ASTNode.h"
#include "Declaration.h"
#include "Type.h"

#include <optional>
#include <variant>

class NamedDecl : public ASTNode {
  ASTNodeImpl(NamedDecl, ASTNode);
  AST_PRINT_ROLE_DECL(Declaration);

public:
  std::string ToString() const override;

  std::string_view name;
};

class ValueDecl : public NamedDecl {
  ASTNodeImpl(ValueDecl, NamedDecl);
};

using NumericValue = std::variant<long double, uint64_t, int64_t>;

// clang-format off
#define VALUE_CATEGORY_VALUES(x)\
  x(LValue)\
  x(RValue)\
  x(FunctionDesignator)
// clang-format on

MAKE_ENUM(ValueCategory, VALUE_CATEGORY_VALUES)
REGISTER_ENUM(ValueCategory, VALUE_CATEGORY_VALUES)

class Expr : public ASTNode {
  ASTNodeImpl(Expr, ASTNode);

public:
  TypeRef type = nullptr;
  ValueCategory value_category = ValueCategory::RValue;
  std::optional<NumericValue> constant_value;
};

#define ExprFieldAsString(field)                                               \
  std::string ToStringImpl() const override { return std::format("{}", field); }

class LiteralString : public Expr {
  ASTNodeImpl(LiteralString, Expr);
  AST_PRINT_FIELDS_DECL();

public:
  CowString value;
};

class LiteralNum : public Expr {
  ASTNodeImpl(LiteralNum, Expr);
  AST_PRINT_FIELDS_DECL();

public:
  NumericValue value;
};

class Stmt : public ASTNode {
  ASTNodeImpl(Stmt, ASTNode);
  AST_PRINT_ROLE_DECL(Statement);
};

class DeclStmt : public Stmt {
  ASTNodeImpl(DeclStmt, Stmt);
  AST_PRINT_CHILDREN_DECL();

public:
  std::vector<std::unique_ptr<ASTNode>> decls;
};

class DeclStmtBuilder {
public:
  DeclStmtBuilder() = default;

  void Add(std::unique_ptr<ASTNode> decl) {
    if (!decl) {
      return;
    }
    _location = CombineLocation(_location, decl->location);
    if (std::holds_alternative<std::monostate>(_decls)) {
      _decls = std::move(decl);
    } else if (std::holds_alternative<std::unique_ptr<ASTNode>>(_decls)) {
      std::vector<std::unique_ptr<ASTNode>> decls;
      decls.push_back(std::move(std::get<std::unique_ptr<ASTNode>>(_decls)));
      decls.push_back(std::move(decl));
      _decls = std::move(decls);
    } else {
      std::get<std::vector<std::unique_ptr<ASTNode>>>(_decls).push_back(
          std::move(decl));
    }
  }

  std::unique_ptr<ASTNode> Build() {
    if (std::holds_alternative<std::monostate>(_decls)) {
      return nullptr;
    } else if (std::holds_alternative<std::unique_ptr<ASTNode>>(_decls)) {
      return std::move(std::get<std::unique_ptr<ASTNode>>(_decls));
    } else {
      auto decls =
          std::move(std::get<std::vector<std::unique_ptr<ASTNode>>>(_decls));
      auto decl_stmt = std::make_unique<DeclStmt>();
      decl_stmt->location = _location;
      decl_stmt->decls = std::move(decls);
      return decl_stmt;
    }
  }

private:
  std::variant<std::monostate, std::unique_ptr<ASTNode>,
               std::vector<std::unique_ptr<ASTNode>>>
      _decls;
  SourceLocation _location;
};

class ParamDecl : public ValueDecl {
  ASTNodeImpl(ParamDecl, ValueDecl);
  AST_PRINT_FIELDS_DECL();

public:
  TypeRef type = nullptr;
};

class VarDecl : public ValueDecl {
  ASTNodeImpl(VarDecl, ValueDecl);
  AST_PRINT_MEMBERS_DECL();

public:
  TypeRef type = nullptr;
  StorageClass storage_class = StorageClass::None;
  std::unique_ptr<Initializer> initializer;
};

class TypedefDecl : public NamedDecl {
  ASTNodeImpl(TypedefDecl, NamedDecl);
  AST_PRINT_FIELDS_DECL();

public:
  TypeRef type = nullptr;
};

class VarRef : public Expr {
  ASTNodeImpl(VarRef, Expr);
  AST_PRINT_FIELDS_DECL();

public:
  explicit VarRef(ValueDecl *decl) : decl(decl) {
    ASSERT(decl != nullptr, "VarRef requires a resolved declaration");
  }

  ValueDecl *const decl;
};

class CompoundStmt : public Stmt {
  ASTNodeImpl(CompoundStmt, Stmt);
  AST_PRINT_CHILDREN_DECL();

public:
  std::vector<std::unique_ptr<ASTNode>> statements;

  void AddStmt(std::unique_ptr<ASTNode> stmt) {
    if (stmt && stmt->GetType() != ASTNodeType::EmptyStmt) {
      statements.push_back(std::move(stmt));
    }
  }
};

class StructDecl : public NamedDecl {
  ASTNodeImpl(StructDecl, NamedDecl);
  AST_PRINT_MEMBERS_DECL();

public:
  TypeRef type = nullptr;
  std::unique_ptr<CompoundStmt> fields;
};

class UnionDecl : public NamedDecl {
  ASTNodeImpl(UnionDecl, NamedDecl);
  AST_PRINT_MEMBERS_DECL();

public:
  TypeRef type = nullptr;
  std::unique_ptr<CompoundStmt> fields;
};

class IfStmt : public Stmt {
  ASTNodeImpl(IfStmt, Stmt);
  AST_PRINT_CHILDREN_DECL();

public:
  std::unique_ptr<ASTNode> condition;
  std::unique_ptr<ASTNode> then_branch;
  std::unique_ptr<ASTNode> else_branch;
};

class WhileStmt : public Stmt {
  ASTNodeImpl(WhileStmt, Stmt);
  AST_PRINT_CHILDREN_DECL();

public:
  std::unique_ptr<ASTNode> condition;
  std::unique_ptr<ASTNode> body;
};

class DoWhileStmt : public Stmt {
  ASTNodeImpl(DoWhileStmt, Stmt);
  AST_PRINT_CHILDREN_DECL();

public:
  std::unique_ptr<ASTNode> condition;
  std::unique_ptr<ASTNode> body;
};

class ForStmt : public Stmt {
  ASTNodeImpl(ForStmt, Stmt);
  AST_PRINT_CHILDREN_DECL();

public:
  std::unique_ptr<ASTNode> init;
  std::unique_ptr<ASTNode> condition;
  std::unique_ptr<ASTNode> increment;
  std::unique_ptr<ASTNode> body;
};

class CaseStmt : public Stmt {
  ASTNodeImpl(CaseStmt, Stmt);
  AST_PRINT_CHILDREN_DECL();

public:
  std::vector<std::unique_ptr<ASTNode>> case_vals;
  std::vector<std::unique_ptr<ASTNode>> body;
};

class DefaultStmt : public Stmt {
  ASTNodeImpl(DefaultStmt, Stmt);
  AST_PRINT_CHILDREN_DECL();

public:
  std::vector<std::unique_ptr<ASTNode>> body;
};

class SwitchStmt : public Stmt {
  ASTNodeImpl(SwitchStmt, Stmt);
  AST_PRINT_CHILDREN_DECL();

  DefaultStmt *DefaultBlock() {
    for (const auto &case_node : cases) {
      if (case_node->Is<DefaultStmt>()) {
        return case_node->As<DefaultStmt>();
      }
    }
    return nullptr;
  }

public:
  std::unique_ptr<ASTNode> value;
  std::vector<std::unique_ptr<ASTNode>> cases;
};

class BreakStmt : public Stmt {
  ASTNodeImpl(BreakStmt, Stmt);
};

class ContinueStmt : public Stmt {
  ASTNodeImpl(ContinueStmt, Stmt);
};

class ReturnStmt : public Stmt {
  ASTNodeImpl(ReturnStmt, Stmt);
  AST_PRINT_CHILDREN_DECL();

public:
  std::unique_ptr<ASTNode> ret_value;
};

class FunctionDecl : public ValueDecl {
  ASTNodeImpl(FunctionDecl, ValueDecl);
  AST_PRINT_MEMBERS_DECL();

public:
  std::vector<std::unique_ptr<ParamDecl>> parameters;
  TypeRef function_type = nullptr;
  StorageClass storage_class = StorageClass::None;
  std::unique_ptr<CompoundStmt> body;
};

class EnumElemDecl : public ValueDecl {
  ASTNodeImpl(EnumElemDecl, ValueDecl);
  AST_PRINT_MEMBERS_DECL();

public:
  TypeRef type = nullptr;
  std::optional<NumericValue> constant_value;
  std::unique_ptr<ASTNode> value;
};

class EnumDecl : public NamedDecl {
  ASTNodeImpl(EnumDecl, NamedDecl);
  AST_PRINT_MEMBERS_DECL();

public:
  TypeRef type = nullptr;
  TypeRef underlying_type = nullptr;
  bool is_definition = false;
  std::vector<std::unique_ptr<EnumElemDecl>> values;
};

class LabelStmt : public Stmt {
  ASTNodeImpl(LabelStmt, Stmt);
  AST_PRINT_FIELDS_DECL();

public:
  std::string ToString() const override;

  std::string_view name;
};

class GotoStmt : public Stmt {
  ASTNodeImpl(GotoStmt, Stmt);
  AST_PRINT_FIELDS_DECL();

public:
  std::string ToString() const override;

  std::string_view target;
};

class EmptyStmt : public Stmt {
  ASTNodeImpl(EmptyStmt, Stmt);
};

class ConditionalExpr : public Expr {
  ASTNodeImpl(ConditionalExpr, Expr);
  AST_PRINT_MEMBERS_DECL();

public:
  std::unique_ptr<ASTNode> condition;
  std::unique_ptr<ASTNode> true_expr;
  std::unique_ptr<ASTNode> false_expr;
};

bool IsConstantFoldable(const ASTNode *node);
