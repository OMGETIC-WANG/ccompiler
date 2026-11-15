#include "ASTPrint.h"

#include "ASTNode.h"

#include <cstdint>
#include <format>

namespace {

struct RGB {
  int red;
  int green;
  int blue;
};

RGB GetRoleColor(ASTPrintRole role) {
  switch (role) {
  case ASTPrintRole::TranslationUnit:
  case ASTPrintRole::Expression:
    return {0x00, 0xcc, 0x00};
  case ASTPrintRole::Statement:
    return {0xcc, 0x00, 0xcc};
  case ASTPrintRole::Declaration:
    return {0xc6, 0x8d, 0xff};
  case ASTPrintRole::Type:
    return {0x33, 0x99, 0xff};
  case ASTPrintRole::Address:
    return {0x00, 0x99, 0x4c};
  case ASTPrintRole::Key:
    return {0x7f, 0x8c, 0x8d};
  case ASTPrintRole::Default:
    return {};
  }
  return {};
}

void WriteColored(std::ostream &out, std::string_view text, ASTPrintRole role,
                  bool use_color) {
  if (!use_color || role == ASTPrintRole::Default) {
    out << text;
    return;
  }

  const RGB color = GetRoleColor(role);
  out << std::format("\x1b[38;2;{};{};{}m", color.red, color.green, color.blue)
      << text << "\x1b[0m";
}

std::string FormatAddress(const ASTNode &node) {
  return std::format("0x{:x}", reinterpret_cast<uintptr_t>(&node));
}

void PrintFieldPrefix(std::ostream &out, std::string_view name,
                      const ASTPrintOptions &options) {
  if (!options.print_field_names) {
    return;
  }
  WriteColored(out, name, ASTPrintRole::Key, options.use_color);
  out << '=';
}

void PrintNodeLine(std::ostream &out, const ASTNode &node,
                   const ASTPrintOptions &options, std::string_view prefix,
                   std::string_view connector) {
  out << prefix << connector;
  WriteColored(out, node.GetNodeTypeName(), node.GetPrintRole(),
               options.use_color);
  out << ' ';

  WriteColored(out, FormatAddress(node), ASTPrintRole::Address,
               options.use_color);

  for (const ASTPrintField &field : node.GetPrintFields()) {
    out << ' ';
    PrintFieldPrefix(out, field.name, options);
    WriteColored(out, field.value, field.role, options.use_color);
  }
  out << '\n';
}

void PrintNode(std::ostream &out, const ASTNode &node,
               const ASTPrintOptions &options, std::string_view prefix,
               bool is_last, bool is_root) {
  PrintNodeLine(out, node, options, prefix,
                is_root ? "" : (is_last ? "`-" : "|-"));

  const std::vector<const ASTNode *> children = node.GetPrintChildren();
  const std::string child_prefix =
      std::string(prefix) + (is_root ? "" : (is_last ? "  " : "| "));
  for (size_t index = 0; index < children.size(); ++index) {
    PrintNode(out, *children[index], options, child_prefix,
              index + 1 == children.size(), false);
  }
}

} // namespace

std::optional<ASTPrintColorMode>
ParseASTPrintColorMode(std::string_view value) {
  if (value == "auto") {
    return ASTPrintColorMode::Auto;
  }
  if (value == "always") {
    return ASTPrintColorMode::Always;
  }
  if (value == "never") {
    return ASTPrintColorMode::Never;
  }
  return std::nullopt;
}

bool ShouldEnableASTColor(ASTPrintColorMode mode, bool stdout_is_tty,
                          bool no_color_requested) {
  switch (mode) {
  case ASTPrintColorMode::Auto:
    return stdout_is_tty && !no_color_requested;
  case ASTPrintColorMode::Always:
    return true;
  case ASTPrintColorMode::Never:
    return false;
  }
  return false;
}

void PrintAST(std::ostream &out, const ASTNode &root, ASTPrintOptions options) {
  PrintNode(out, root, options, {}, true, true);
}
