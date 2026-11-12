#pragma once

#include <concepts>
#include <string_view>

#include "../Exception.h"
#include "../Lex/Token.h"
#include "../Util/EnumRefl.h"
#include "../Util/Tree.h"

// clang-format off
#define AST_NODE_TYPE_VALUES(x)\
  x(Unknown)\
  x(RawLLVMIR)\
  x(TranslationUnit)\
  x(FunctionDecl)\
  x(StructDecl)\
  x(UnionDecl)\
  x(LiteralString)\
  x(LiteralNum)\
  x(BinaryExpr)\
  x(UnaryExpr)\
  x(DeclStmt)\
  x(VarDecl)\
  x(TypedefDecl)\
  x(VarRef)\
  x(FunctionInvocation)\
  x(SequenceEval)\
  x(CompoundStmt)\
  x(IfStmt)\
  x(ReturnStmt)\
  x(WhileStmt)\
  x(DoWhileStmt)\
  x(ForStmt)\
  x(SwitchStmt)\
  x(CaseStmt)\
  x(DefaultStmt)\
  x(BreakStmt)\
  x(ContinueStmt)\
  x(EnumDecl)\
  x(EnumElemDecl)\
  x(LabelStmt)\
  x(GotoStmt)\
  x(TypeCast)\
  x(GetField)\
  x(PtrGetField)\
  x(IntrinsicCall)\
  x(EmptyStmt)\
  x(ConditionalExpr)
// clang-format on

MAKE_ENUM(ASTNodeType, AST_NODE_TYPE_VALUES)
REGISTER_ENUM(ASTNodeType, AST_NODE_TYPE_VALUES)

class LocatableNode : public TreeNode {
public:
  SourceLocation location;
};

class ASTNode : public LocatableNode {
public:
  virtual ~ASTNode() = default;

  virtual ASTNodeType GetType() const = 0;

  std::string_view GetNodeTypeName() const override;

  template <std::derived_from<ASTNode> T> T *As() {
#ifdef ENABLE_AS_TYPE_CHECK
    if (GetType() != T::__type) {
      RAISE_ERROR("Invalid ASTNode type cast from ", GetType(), " to ",
                  T::__type);
    }
#endif
    return static_cast<T *>(this);
  }

  template <std::derived_from<ASTNode> T> const T *As() const {
#ifdef ENABLE_AS_TYPE_CHECK
    if (GetType() != T::__type) {
      RAISE_ERROR("Invalid ASTNode type cast from ", GetType(), " to ",
                  T::__type);
    }
#endif
    return static_cast<const T *>(this);
  }

  template <std::derived_from<ASTNode> T> T *DynCast() {
    if (GetType() != T::__type) {
      return nullptr;
    }
    return static_cast<T *>(this);
  }

  template <std::derived_from<ASTNode> T> const T *DynCast() const {
    if (GetType() != T::__type) {
      return nullptr;
    }
    return static_cast<const T *>(this);
  }

  template <std::derived_from<ASTNode> T> bool Is() const {
    return GetType() == T::__type;
  }
};

template <class T> ASTNodeType ASTNodeTypeOf() { return T::__type; }

#define ASTNodeImpl(type_enum)                                                 \
public:                                                                        \
  ASTNodeType GetType() const override { return ASTNodeType::type_enum; }      \
  constexpr static ASTNodeType __type = ASTNodeType::type_enum
