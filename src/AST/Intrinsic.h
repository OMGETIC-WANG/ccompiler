#pragma once

#include "../CodeGen/LLVMIR/Value.h"
#include "ASTNode.h"
#include "Type.h"
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <variant>
#include <vector>

class GenContext;
class IRBuilder;

using IntrinsicCallArg = std::variant<std::unique_ptr<ASTNode>, TypeRef>;

enum class IntrinsicCallArgKind {
  Expr,
  Type,
  ExprOrType,
};

using IntrinsicEvaluator = RValue (*)(std::vector<IntrinsicCallArg> &args,
                                      IRBuilder &builder, GenContext &context);

struct IntrinsicDesc {
  std::string_view name;
  std::vector<IntrinsicCallArgKind> arg_kinds;
  IntrinsicEvaluator evaluator;

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

class IntrinsicCall : public ASTNode {
  ASTNodeImpl(IntrinsicCall);

  std::string ToString() const override {
    return desc ? std::string(desc->name) : "<unknown_intrinsic>";
  }

public:
  virtual std::vector<TreeNodePrintField> GetPrintFields() const override;

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
                                                  FilteredLexer &lexer,
                                                  CompileContext &context);
