#include "Intrinsic.h"
#include "../Util/Util.h"
#include "Declaration.h"
#include "Diagnostics/Errors.generated.h"
#include "Expr.h"
#include "ParserError.h"

#include <format>
#include <vector>

void IntrinsicManager::RegisterIntrinsic(IntrinsicDesc desc) {
  ASSERT(!Intrinsics().contains(desc.name),
         "Intrinsic already registered: ", desc.name);
  Intrinsics().insert(std::move(desc));
}

std::vector<ASTPrintField> IntrinsicCall::GetPrintFields() const {
  std::vector<ASTPrintField> print_fields;
  AppendExprPrintFields(print_fields, *this);
  AppendASTPrintField(print_fields, "intrinsic_name",
                      desc ? std::string(desc->name) : "<unknown_intrinsic>");
  for (size_t index = 0; index < args.size(); ++index) {
    if (std::holds_alternative<TypeRef>(args[index])) {
      const TypeRef type = std::get<TypeRef>(args[index]);
      if (type) {
        AppendASTPrintField(print_fields, std::format("arg{}_type", index),
                            type->ToString(), ASTPrintRole::Type);
      }
    }
  }
  return print_fields;
}

std::vector<const ASTNode *> IntrinsicCall::GetPrintChildren() const {
  std::vector<const ASTNode *> print_children;
  for (const auto &arg : args) {
    if (std::holds_alternative<std::unique_ptr<ASTNode>>(arg)) {
      const auto &expr_arg = std::get<std::unique_ptr<ASTNode>>(arg);
      AppendASTPrintChild(print_children, expr_arg.get());
    }
  }
  return print_children;
}

OptUnique<IntrinsicCall> ParseIntrinsicCall(const IntrinsicDesc *desc,
                                            Token name_token, Lexer &lexer,
                                            CompileContext &context) {
  auto call = std::make_unique<IntrinsicCall>();
  call->desc = desc;
  Token next;
  TRY_ASSIGN_NEW(lparen, lexer.PeekNextToken());
  if (lparen.type != TokenType::LParen) {
    ReportParserError(context, lparen.location,
                      errs::ExpectedIntrinsicLParen{});
    return std::nullopt;
  }
  lexer.ConsumeToken(lparen);
  for (size_t i = 0; i < desc->arg_kinds.size(); i++) {
    IntrinsicCallArgKind kind = desc->arg_kinds[i];
    switch (kind) {
    case IntrinsicCallArgKind::Expr: {
      TRY_ASSIGN_NEW(argument, ParseAssignmentExpr(lexer, context));
      call->args.push_back(std::move(argument));
      break;
    }
    case IntrinsicCallArgKind::Type: {
      TRY_ASSIGN_NEW(argument, ParseTypeName(lexer, context));
      call->args.push_back(argument);
      break;
    }
    case IntrinsicCallArgKind::ExprOrType: {
      TRY_ASSIGN(next, lexer.PeekNextToken());
      if (IsDeclarationSpecifier(next, context)) {
        TRY_ASSIGN_NEW(argument, ParseTypeName(lexer, context));
        call->args.push_back(argument);
      } else {
        TRY_ASSIGN_NEW(argument, ParseAssignmentExpr(lexer, context));
        call->args.push_back(std::move(argument));
      }
    } break;
    }
    if (i != desc->arg_kinds.size() - 1) {
      TRY_ASSIGN(next, lexer.PeekNextToken());
      Token comma = next;
      if (comma.type != TokenType::Comma) {
        ReportParserError(context, comma.location,
                          errs::ExpectedIntrinsicComma{});
        return std::nullopt;
      }
      lexer.ConsumeToken(comma);
    }
  }
  TRY_ASSIGN(next, lexer.PeekNextToken());
  Token rparen = next;
  if (rparen.type != TokenType::RParen) {
    ReportParserError(context, rparen.location,
                      errs::ExpectedIntrinsicRParen{});
    return std::nullopt;
  }
  lexer.ConsumeToken(rparen);
  call->location = CombineLocation(name_token.location, rparen.location);
  return call;
}
