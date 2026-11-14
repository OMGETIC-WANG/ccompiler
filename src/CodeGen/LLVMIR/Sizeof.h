#pragma once

#include "../../AST/Intrinsic.h"

RValue GenSizeof(std::vector<IntrinsicCallArg> &args,
                 std::variant<ExprEmitter *, GenContext *> arg);

inline const IntrinsicDesc sizeof_desc = {
    .name = "sizeof",
    .arg_kinds = {IntrinsicCallArgKind::ExprOrType},
    .evaluator = GenSizeof,
    .type_evaluator =
        [](const std::vector<IntrinsicCallArg> &, TypeContext &types) {
          return TypeRef(types.getLongLong());
        },
    .is_constant_evalable = true};
