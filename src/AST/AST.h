#pragma once

#include "ASTNode.h"
#include "Declaration.h"
#include "Type.h"

class LiteralString : public ASTNode {
  ASTNodeImpl(LiteralString);
  TreeNodeFieldAsString(value);

public:
  std::string_view value;
};

class LiteralNum : public ASTNode {
  ASTNodeImpl(LiteralNum);

  std::string ToString() const override {
    if (std::holds_alternative<double>(value)) {
      return std::to_string(std::get<double>(value));
    } else if (std::holds_alternative<uint64_t>(value)) {
      return std::to_string(std::get<uint64_t>(value));
    } else if (std::holds_alternative<int64_t>(value)) {
      return std::to_string(std::get<int64_t>(value));
    }
    return {};
  }

public:
  std::variant<double, uint64_t, int64_t> value;
};

class DeclStmt : public ASTNode {
  ASTNodeImpl(DeclStmt);
  TreeNodePrintFields(decls);

public:
  std::vector<std::unique_ptr<ASTNode>> decls;
};

class DeclStmtBuilder {
public:
  DeclStmtBuilder() = default;

  void Add(std::unique_ptr<ASTNode> decl) {
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
      decl_stmt->decls = std::move(decls);
      return decl_stmt;
    }
  }

private:
  std::variant<std::monostate, std::unique_ptr<ASTNode>,
               std::vector<std::unique_ptr<ASTNode>>>
      _decls;
};

class VarDecl : public ASTNode {
  ASTNodeImpl(VarDecl);
  TreeNodeFieldAsString(name);
  TreeNodePrintFields(type, initializer);

public:
  std::string_view name;
  TypeRef type = nullptr;
  std::unique_ptr<Initializer> initializer;
};

class TypedefDecl : public ASTNode {
  ASTNodeImpl(TypedefDecl);
  TreeNodeFieldAsString(name);
  TreeNodePrintFields(name, type);

public:
  std::string_view name;
  TypeRef type = nullptr;
};

class VarRef : public ASTNode {
  ASTNodeImpl(VarRef);
  TreeNodeFieldAsString(name);

public:
  std::string_view name;
};

class CompoundStmt : public ASTNode {
  ASTNodeImpl(CompoundStmt);
  TreeNodePrintFields(statements);

public:
  std::vector<std::unique_ptr<ASTNode>> statements;
};

class StructDecl : public ASTNode {
  ASTNodeImpl(StructDecl);
  TreeNodePrintFields(name, fields);

public:
  std::string_view name;
  TypeRef type = nullptr;
  std::unique_ptr<CompoundStmt> fields;
};

class IfStmt : public ASTNode {
  ASTNodeImpl(IfStmt);
  TreeNodePrintFields(condition, then_branch, else_branch);

public:
  std::unique_ptr<ASTNode> condition;
  std::unique_ptr<ASTNode> then_branch;
  std::unique_ptr<ASTNode> else_branch;
};

class ReturnStmt : public ASTNode {
  ASTNodeImpl(ReturnStmt);
  TreeNodePrintFields(ret_value);

public:
  std::unique_ptr<ASTNode> ret_value;
};

class FunctionDecl : public ASTNode {
  ASTNodeImpl(FunctionDecl);
  TreeNodeFieldAsString(name);

public:
  std::vector<TreeNodePrintField> GetPrintFields() const override {
    std::vector<TreeNodePrintField> print_fields;
    AppendTreeNodePrintField(print_fields, "name", name);
    AppendTreeNodePrintField(print_fields, "function_type", function_type);
    for (std::string_view parameter_name : parameter_names) {
      print_fields.push_back(
          {"parameter_name", parameter_name.empty()
                                 ? "<anonymous>"
                                 : std::string(parameter_name)});
    }
    AppendTreeNodePrintField(print_fields, "body", body);
    return print_fields;
  }

  std::string_view name;
  std::vector<std::string_view> parameter_names;
  TypeRef function_type = nullptr;
  std::unique_ptr<CompoundStmt> body;
};

class EnumElemDecl : public ASTNode {
  ASTNodeImpl(EnumElemDecl);
  TreeNodeFieldAsString(name);
  TreeNodePrintFields(name, value);

public:
  std::string_view name;
  std::unique_ptr<ASTNode> value;
};

class EnumDecl : public ASTNode {
  ASTNodeImpl(EnumDecl);
  TreeNodePrintFields(name, values);

public:
  std::string_view name;
  TypeRef type = nullptr;
  bool is_definition = false;
  std::vector<std::unique_ptr<EnumElemDecl>> values;
};

class LabelStmt : public ASTNode {
  ASTNodeImpl(LabelStmt);
  TreeNodeFieldAsString(name);

public:
  std::string_view name;
};

class GotoStmt : public ASTNode {
  ASTNodeImpl(GotoStmt);
  TreeNodeFieldAsString(target);

public:
  std::string_view target;
};
