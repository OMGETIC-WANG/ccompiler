#pragma once

#include "../Util/Util.h"

#include <format>
#include <optional>
#include <ostream>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef AST_PRINT_FIELD_NAMES
#define AST_PRINT_FIELD_NAMES 0
#endif

class ASTNode;
class Expr;

enum class ASTPrintRole {
  Default,
  TranslationUnit,
  Expression,
  Statement,
  Declaration,
  Type,
  Address,
  Key,
};

struct ASTPrintField {
  std::string name;
  std::string value;
  ASTPrintRole role = ASTPrintRole::Default;
};

void AppendExprPrintFields(std::vector<ASTPrintField> &fields,
                           const Expr &expr);

struct ASTPrintOptions {
  bool use_color = false;
  bool print_field_names = AST_PRINT_FIELD_NAMES != 0;
};

enum class ASTPrintColorMode {
  Auto,
  Always,
  Never,
};

std::optional<ASTPrintColorMode> ParseASTPrintColorMode(std::string_view value);

bool ShouldEnableASTColor(ASTPrintColorMode mode, bool stdout_is_tty,
                          bool no_color_requested);

inline void AppendASTPrintField(std::vector<ASTPrintField> &fields,
                                std::string_view name, std::string value,
                                ASTPrintRole role = ASTPrintRole::Default) {
  if (!value.empty()) {
    fields.push_back({std::string(name), std::move(value), role});
  }
}

template <typename T>
  requires requires(const T &value) { std::format("{}", value); }
void AppendASTPrintField(std::vector<ASTPrintField> &fields,
                         std::string_view name, const T &value,
                         ASTPrintRole role = ASTPrintRole::Default) {
  AppendASTPrintField(fields, name, std::format("{}", value), role);
}

inline void AppendASTPrintChild(std::vector<const ASTNode *> &children,
                                const ASTNode *child) {
  if (child) {
    children.push_back(child);
  }
}

template <std::ranges::input_range Range>
void AppendASTPrintChildren(std::vector<const ASTNode *> &children,
                            const Range &nodes) {
  for (const auto &node : nodes) {
    AppendASTPrintChild(children, node.get());
  }
}

// Class-definition helpers for the common print method signatures.
#define AST_PRINT_ROLE_DECL(role)                                              \
public:                                                                        \
  ASTPrintRole GetPrintRole() const override { return ASTPrintRole::role; }

#define AST_PRINT_FIELDS_DECL()                                                \
public:                                                                        \
  std::vector<ASTPrintField> GetPrintFields() const override

#define AST_PRINT_CHILDREN_DECL()                                              \
public:                                                                        \
  std::vector<const ASTNode *> GetPrintChildren() const override

#define AST_PRINT_MEMBERS_DECL()                                               \
  AST_PRINT_FIELDS_DECL();                                                     \
  AST_PRINT_CHILDREN_DECL()

// Tags used inside AST_PRINT_*_IMPL. They append fields in declaration order.
#define AST_FIELD(member)                                                      \
  AppendASTPrintField(ast_print_fields, #member, this->member)

#define AST_TYPE_FIELD(member)                                                 \
  if (this->member) {                                                          \
    AppendASTPrintField(ast_print_fields, #member, this->member->ToString(),   \
                        ASTPrintRole::Type);                                   \
  }

#define AST_OPTIONAL_FIELD(member)                                             \
  if (this->member) {                                                          \
    AppendASTPrintField(ast_print_fields, #member, *this->member);             \
  }

#define AST_OPTIONAL_TYPE_FIELD(member)                                        \
  if (this->member && *this->member) {                                         \
    AppendASTPrintField(ast_print_fields, #member,                             \
                        (*this->member)->ToString(), ASTPrintRole::Type);      \
  }

#define AST_TRUE_FIELD(member)                                                 \
  if (this->member) {                                                          \
    AppendASTPrintField(ast_print_fields, #member, "true");                    \
  }

#define AST_CHILD(member)                                                      \
  AppendASTPrintChild(ast_print_children, this->member.get())

#define AST_CHILDREN(member)                                                   \
  AppendASTPrintChildren(ast_print_children, this->member)

#define AST_PRINT_DETAIL_ITEM(item) item;

// Out-of-line implementations for nodes without custom print logic.
#define AST_PRINT_FIELDS_IMPL(Class, ...)                                      \
  std::vector<ASTPrintField> Class::GetPrintFields() const {                   \
    std::vector<ASTPrintField> ast_print_fields;                               \
    FOR_EACH(AST_PRINT_DETAIL_ITEM __VA_OPT__(, ) __VA_ARGS__)                 \
    return ast_print_fields;                                                   \
  }

#define AST_EXPR_PRINT_FIELDS_IMPL(Class, ...)                                 \
  std::vector<ASTPrintField> Class::GetPrintFields() const {                   \
    std::vector<ASTPrintField> ast_print_fields;                               \
    AppendExprPrintFields(ast_print_fields, *this);                            \
    FOR_EACH(AST_PRINT_DETAIL_ITEM __VA_OPT__(, ) __VA_ARGS__)                 \
    return ast_print_fields;                                                   \
  }

#define AST_PRINT_CHILDREN_IMPL(Class, ...)                                    \
  std::vector<const ASTNode *> Class::GetPrintChildren() const {               \
    std::vector<const ASTNode *> ast_print_children;                           \
    FOR_EACH(AST_PRINT_DETAIL_ITEM __VA_OPT__(, ) __VA_ARGS__)                 \
    return ast_print_children;                                                 \
  }

void PrintAST(std::ostream &out, const ASTNode &root,
              ASTPrintOptions options = {});
