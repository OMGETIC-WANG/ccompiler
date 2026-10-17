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
  TreeNodeFieldAsString(value);

public:
  std::string_view value;
};

class DeclStmt : public ASTNode {
  ASTNodeImpl(DeclStmt);
  TreeNodePrintFields(decls);

public:
  std::vector<std::unique_ptr<ASTNode>> decls;
};

class VarDecl : public ASTNode {
  ASTNodeImpl(VarDecl);
  TreeNodeFieldAsString(name);
  TreeNodePrintFields(type, initializer);

public:
  std::string_view name;
  std::unique_ptr<TypeBase> type;
  std::unique_ptr<Initializer> initializer;
};

class TypedefDecl : public ASTNode {
  ASTNodeImpl(TypedefDecl);
  TreeNodeFieldAsString(name);
  TreeNodePrintFields(name, type);

public:
  std::string_view name;
  std::unique_ptr<TypeBase> type;
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
  TreeNodePrintFields(name, function_type, body);

public:
  std::string_view name;
  std::unique_ptr<TypeBase> function_type;
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
