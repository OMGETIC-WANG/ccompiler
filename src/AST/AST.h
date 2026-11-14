#pragma once

#include "../Util/CowString.h"
#include "ASTNode.h"
#include "Declaration.h"
#include "Type.h"

#include <optional>
#include <variant>

class NamedDecl : public ASTNode {
  TreeNodeFieldAsString(name);

public:
  std::string_view name;
};

class ValueDecl : public NamedDecl {};

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
public:
  std::string ToString() const final {
    std::string to_string_impl = ToStringImpl();
    if (to_string_impl.empty()) {
      return std::string(EnumRefl<ValueCategory>::GetName(value_category));
    }
    return std::format("{} {}",
                       EnumRefl<ValueCategory>::GetName(value_category),
                       to_string_impl);
  }

  virtual std::string ToStringImpl() const { return {}; }

  TypeRef type = nullptr;
  ValueCategory value_category = ValueCategory::RValue;
  std::optional<NumericValue> constant_value;
};

#define ExprFieldAsString(field)                                               \
  std::string ToStringImpl() const override { return std::format("{}", field); }

inline void AppendExprPrintFields(std::vector<TreeNodePrintField> &fields,
                                  const Expr &expr) {
  AppendTreeNodePrintField(fields, "type", expr.type);
}

#define ExprPrintFields(...)                                                   \
public:                                                                        \
  std::vector<TreeNodePrintField> GetPrintFields() const override {            \
    std::vector<TreeNodePrintField> print_fields;                              \
    AppendExprPrintFields(print_fields, *this);                                \
    FOR_EACH(__AddTreeNodePrintField __VA_OPT__(, ) __VA_ARGS__)               \
    return print_fields;                                                       \
  }

class LiteralString : public Expr {
  ASTNodeImpl(LiteralString);
  ExprPrintFields();
  ExprFieldAsString(value);

public:
  CowString value;
};

class LiteralNum : public Expr {
  ASTNodeImpl(LiteralNum);
  ExprPrintFields();

  std::string ToStringImpl() const override {
    std::string value_s =
        std::visit([](auto val) { return std::to_string(val); }, value);
    return std::format("{}{}:{}", type->ToString(),
                       HasTypeTag(type, TypeTag::Unsigned) ? "u" : "", value_s);
  }

public:
  NumericValue value;
};

class Stmt : public ASTNode {
public:
  int GetColor() const override { return 0xcc00cc; }
};

class DeclStmt : public Stmt {
  ASTNodeImpl(DeclStmt);
  TreeNodePrintFields(decls);

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
  ASTNodeImpl(ParamDecl);
  TreeNodePrintFields(type);

public:
  TypeRef type = nullptr;
};

class VarDecl : public ValueDecl {
  ASTNodeImpl(VarDecl);
  TreeNodePrintFields(type, initializer);

public:
  TypeRef type = nullptr;
  StorageClass storage_class = StorageClass::None;
  std::unique_ptr<Initializer> initializer;
};

class TypedefDecl : public NamedDecl {
  ASTNodeImpl(TypedefDecl);
  TreeNodePrintFields(type);

public:
  TypeRef type = nullptr;
};

class VarRef : public Expr {
  ASTNodeImpl(VarRef);
  ExprPrintFields();

public:
  explicit VarRef(ValueDecl *decl) : decl(decl) {
    ASSERT(decl != nullptr, "VarRef requires a resolved declaration");
  }

  std::string ToStringImpl() const override;

  ValueDecl *const decl;
};

class CompoundStmt : public Stmt {
  ASTNodeImpl(CompoundStmt);
  TreeNodePrintFields(statements);

public:
  std::vector<std::unique_ptr<ASTNode>> statements;

  void AddStmt(std::unique_ptr<ASTNode> stmt) {
    if (stmt && stmt->GetType() != ASTNodeType::EmptyStmt) {
      statements.push_back(std::move(stmt));
    }
  }
};

class StructDecl : public NamedDecl {
  ASTNodeImpl(StructDecl);
  TreeNodePrintFields(type, fields);

public:
  TypeRef type = nullptr;
  std::unique_ptr<CompoundStmt> fields;
};

class UnionDecl : public NamedDecl {
  ASTNodeImpl(UnionDecl);
  TreeNodePrintFields(type, fields);

public:
  TypeRef type = nullptr;
  std::unique_ptr<CompoundStmt> fields;
};

class IfStmt : public Stmt {
  ASTNodeImpl(IfStmt);
  TreeNodePrintFields(condition, then_branch, else_branch);

public:
  std::unique_ptr<ASTNode> condition;
  std::unique_ptr<ASTNode> then_branch;
  std::unique_ptr<ASTNode> else_branch;
};

class WhileStmt : public Stmt {
  ASTNodeImpl(WhileStmt);
  TreeNodePrintFields(condition, body);

public:
  std::unique_ptr<ASTNode> condition;
  std::unique_ptr<ASTNode> body;
};

class DoWhileStmt : public Stmt {
  ASTNodeImpl(DoWhileStmt);
  TreeNodePrintFields(condition, body);

public:
  std::unique_ptr<ASTNode> condition;
  std::unique_ptr<ASTNode> body;
};

class ForStmt : public Stmt {
  ASTNodeImpl(ForStmt);
  TreeNodePrintFields(init, condition, increment, body);

public:
  std::unique_ptr<ASTNode> init;
  std::unique_ptr<ASTNode> condition;
  std::unique_ptr<ASTNode> increment;
  std::unique_ptr<ASTNode> body;
};

class CaseStmt : public Stmt {
  ASTNodeImpl(CaseStmt);
  TreeNodePrintFields(case_vals, body);

public:
  std::vector<std::unique_ptr<ASTNode>> case_vals;
  std::vector<std::unique_ptr<ASTNode>> body;
};

class DefaultStmt : public Stmt {
  ASTNodeImpl(DefaultStmt);
  TreeNodePrintFields(body);

public:
  std::vector<std::unique_ptr<ASTNode>> body;
};

class SwitchStmt : public Stmt {
  ASTNodeImpl(SwitchStmt);
  TreeNodePrintFields(value, cases);

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
  ASTNodeImpl(BreakStmt);
};

class ContinueStmt : public Stmt {
  ASTNodeImpl(ContinueStmt);
};

class ReturnStmt : public Stmt {
  ASTNodeImpl(ReturnStmt);
  TreeNodePrintFields(ret_value);

public:
  std::unique_ptr<ASTNode> ret_value;
};

class FunctionDecl : public ValueDecl {
  ASTNodeImpl(FunctionDecl);
  TreeNodePrintFields(parameters, body);

public:
  std::vector<std::unique_ptr<ParamDecl>> parameters;
  TypeRef function_type = nullptr;
  StorageClass storage_class = StorageClass::None;
  std::unique_ptr<CompoundStmt> body;
};

class EnumElemDecl : public ValueDecl {
  ASTNodeImpl(EnumElemDecl);
  TreeNodePrintFields(type, value);

public:
  TypeRef type = nullptr;
  std::optional<NumericValue> constant_value;
  std::unique_ptr<ASTNode> value;
};

class EnumDecl : public NamedDecl {
  ASTNodeImpl(EnumDecl);
  TreeNodePrintFields(type, underlying_type, values);

public:
  TypeRef type = nullptr;
  TypeRef underlying_type = nullptr;
  bool is_definition = false;
  std::vector<std::unique_ptr<EnumElemDecl>> values;
};

class LabelStmt : public Stmt {
  ASTNodeImpl(LabelStmt);
  TreeNodeFieldAsString(name);

public:
  std::string_view name;
};

class GotoStmt : public Stmt {
  ASTNodeImpl(GotoStmt);
  TreeNodeFieldAsString(target);

public:
  std::string_view target;
};

class EmptyStmt : public Stmt {
  ASTNodeImpl(EmptyStmt);
};

class ConditionalExpr : public Expr {
  ASTNodeImpl(ConditionalExpr);
  ExprPrintFields(condition, true_expr, false_expr);

public:
  std::unique_ptr<ASTNode> condition;
  std::unique_ptr<ASTNode> true_expr;
  std::unique_ptr<ASTNode> false_expr;
};

bool IsConstantFoldable(const ASTNode *node);
