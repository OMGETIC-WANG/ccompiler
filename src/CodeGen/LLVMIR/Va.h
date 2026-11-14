#pragma once

#include "../../AST/Intrinsic.h"

RValue GenVaStart(std::vector<IntrinsicCallArg> &args,
                  std::variant<ExprEmitter *, GenContext *> arg);

inline const IntrinsicDesc va_start_desc = {
    .name = "__builtin_va_start",
    .arg_kinds = {IntrinsicCallArgKind::Expr, IntrinsicCallArgKind::Expr},
    .evaluator = GenVaStart,
    .type_evaluator = [](const std::vector<IntrinsicCallArg> &,
                         TypeContext &types) {
      return TypeRef(types.getVoid());
    }};

RValue GenVaArg(std::vector<IntrinsicCallArg> &args,
                std::variant<ExprEmitter *, GenContext *> arg);

inline const IntrinsicDesc va_arg_desc = {
    .name = "__builtin_va_arg",
    .arg_kinds = {IntrinsicCallArgKind::Expr, IntrinsicCallArgKind::Type},
    .evaluator = GenVaArg,
    .type_evaluator = [](const std::vector<IntrinsicCallArg> &args,
                         TypeContext &) { return std::get<TypeRef>(args[1]); }};

RValue GenVaEnd(std::vector<IntrinsicCallArg> &args,
                std::variant<ExprEmitter *, GenContext *> arg);

inline const IntrinsicDesc va_end_desc = {
    .name = "__builtin_va_end",
    .arg_kinds = {IntrinsicCallArgKind::Expr},
    .evaluator = GenVaEnd,
    .type_evaluator = [](const std::vector<IntrinsicCallArg> &,
                         TypeContext &types) {
      return TypeRef(types.getVoid());
    }};
