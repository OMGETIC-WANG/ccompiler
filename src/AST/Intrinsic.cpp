#include "Intrinsic.h"
#include "Declaration.h"
#include "Expr.h"

void IntrinsicManager::RegisterIntrinsic(IntrinsicDesc desc) {
  ASSERT(!Intrinsics().contains(desc.name),
         "Intrinsic already registered: ", desc.name);
  Intrinsics().insert(std::move(desc));
}

std::vector<TreeNodePrintField> IntrinsicCall::GetPrintFields() const {
  std::vector<TreeNodePrintField> print_fields;
  for (const auto &arg : args) {
    if (std::holds_alternative<std::unique_ptr<ASTNode>>(arg)) {
      const auto &expr_arg = std::get<std::unique_ptr<ASTNode>>(arg);
      print_fields.push_back({"arg_expr", expr_arg.get()});
    } else if (std::holds_alternative<TypeRef>(arg)) {
      const auto &type_arg = std::get<TypeRef>(arg);
      print_fields.push_back({"arg_type", type_arg.get()});
    }
  }
  return print_fields;
}

std::unique_ptr<IntrinsicCall> ParseIntrinsicCall(const IntrinsicDesc *desc,
                                                  Token name_token,
                                                  Lexer &lexer,
                                                  CompileContext &context) {
  auto call = std::make_unique<IntrinsicCall>();
  call->desc = desc;
  Token lparen = lexer.PeekNextToken();
  if (lparen.type != TokenType::LParen) {
    context.Diagnostics().RaiseError(lparen.location,
                                     "Expected '(' after intrinsic name");
  }
  lexer.ConsumeToken(lparen);
  for (size_t i = 0; i < desc->arg_kinds.size(); i++) {
    IntrinsicCallArgKind kind = desc->arg_kinds[i];
    switch (kind) {
    case IntrinsicCallArgKind::Expr:
      call->args.push_back(ParseExpr(lexer, context));
      break;
    case IntrinsicCallArgKind::Type:
      call->args.push_back(ParseType(lexer, context));
      break;
    case IntrinsicCallArgKind::ExprOrType: {
      if (IsDeclarationSpecifier(lexer.PeekNextToken(), context)) {
        call->args.push_back(ParseType(lexer, context));
      } else {
        call->args.push_back(ParseExpr(lexer, context));
      }
    } break;
    }
    if (i != desc->arg_kinds.size() - 1) {
      Token comma = lexer.PeekNextToken();
      if (comma.type != TokenType::Comma) {
        context.Diagnostics().RaiseError(
            comma.location, "Expected ',' between intrinsic arguments");
      }
      lexer.ConsumeToken(comma);
    }
  }
  Token rparen = lexer.PeekNextToken();
  if (rparen.type != TokenType::RParen) {
    context.Diagnostics().RaiseError(rparen.location,
                                     "Expected ')' after intrinsic arguments");
  }
  lexer.ConsumeToken(rparen);
  call->location = CombineLocation(name_token.location, rparen.location);
  return call;
}
