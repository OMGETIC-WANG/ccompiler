#pragma once

#include "../CodeGen/LLVMIR/Value.h"
#include "AST.h"
#include "Type.h"
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <variant>
#include <vector>

class ExprEmitter;
class GenContext;

using IntrinsicCallArg = std::variant<std::unique_ptr<ASTNode>, TypeRef>;

enum class IntrinsicCallArgKind {
  Expr,
  Type,
  ExprOrType,
};

// ExprEmitter* for non-constant-evalable intrinsics,
// GenContext* for constant-evalable intrinsics
using IntrinsicEvaluator =
    RValue (*)(std::vector<IntrinsicCallArg> &args,
               std::variant<ExprEmitter *, GenContext *> arg);
using IntrinsicTypeEvaluator =
    TypeRef (*)(const std::vector<IntrinsicCallArg> &args, TypeContext &types);

struct IntrinsicDesc {
  std::string_view name;
  std::vector<IntrinsicCallArgKind> arg_kinds;
  IntrinsicEvaluator evaluator;
  IntrinsicTypeEvaluator type_evaluator;
  bool is_constant_evalable = false;

  struct Hash {
    using is_transparent = void;
    size_t operator()(const IntrinsicDesc &desc) const {
      return std::hash<std::string_view>{}(desc.name);
    }
    size_t operator()(std::string_view name) const {
      return std::hash<std::string_view>{}(name);
    }
  };

  struct KeyEqual {
    using is_transparent = void;
    bool operator()(const IntrinsicDesc &l, const IntrinsicDesc &r) const {
      return l.name == r.name;
    }
    bool operator()(const IntrinsicDesc &l, std::string_view r) const {
      return l.name == r;
    }
    bool operator()(std::string_view l, const IntrinsicDesc &r) const {
      return l == r.name;
    }
  };
};

class IntrinsicCall : public Expr {
  ASTNodeImpl(IntrinsicCall, Expr);
  AST_PRINT_MEMBERS_DECL();

  RValue Eval(ExprEmitter *emitter) { return desc->evaluator(args, emitter); }
  RValue Eval(GenContext *context) { return desc->evaluator(args, context); }

public:
  const IntrinsicDesc *desc = nullptr;
  std::vector<IntrinsicCallArg> args;
};

class IntrinsicManager {
public:
  static void RegisterIntrinsic(IntrinsicDesc desc);

  static bool IsIntrinsicName(std::string_view name) {
    return Intrinsics().contains(name);
  }

  static const IntrinsicDesc *GetIntrinsicDesc(std::string_view name) {
    auto it = Intrinsics().find(name);
    if (it != Intrinsics().end()) {
      return &(*it);
    } else {
      return nullptr;
    }
  }

private:
  static std::unordered_set<IntrinsicDesc, IntrinsicDesc::Hash,
                            IntrinsicDesc::KeyEqual> &
  Intrinsics() {
    static std::unordered_set<IntrinsicDesc, IntrinsicDesc::Hash,
                              IntrinsicDesc::KeyEqual>
        intrinsics;
    return intrinsics;
  }
};

std::unique_ptr<IntrinsicCall> ParseIntrinsicCall(const IntrinsicDesc *desc,
                                                  Token name_token,
                                                  Lexer &lexer,
                                                  CompileContext &context);
