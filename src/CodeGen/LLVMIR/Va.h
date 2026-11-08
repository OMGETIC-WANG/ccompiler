#pragma once

#include "../../AST/Intrinsic.h"

RValue GenVaStart(std::vector<IntrinsicCallArg> &args, IRBuilder &builder,
                  GenContext &context);

inline const IntrinsicDesc va_start_desc = {
    .name = "__builtin_va_start",
    .arg_kinds = {IntrinsicCallArgKind::Expr, IntrinsicCallArgKind::Expr},
    .evaluator = GenVaStart};

RValue GenVaArg(std::vector<IntrinsicCallArg> &args, IRBuilder &builder,
                GenContext &context);

inline const IntrinsicDesc va_arg_desc = {
    .name = "__builtin_va_arg",
    .arg_kinds = {IntrinsicCallArgKind::Expr, IntrinsicCallArgKind::Type},
    .evaluator = GenVaArg};

RValue GenVaEnd(std::vector<IntrinsicCallArg> &args, IRBuilder &builder,
                GenContext &context);

inline const IntrinsicDesc va_end_desc = {
    .name = "__builtin_va_end",
    .arg_kinds = {IntrinsicCallArgKind::Expr},
    .evaluator = GenVaEnd};
