#include "ASTVerifier.h"

#include "AST/AST.h"
#include "AST/Intrinsic.h"
#include "AST/Operations.h"
#include "AST/TranslationUnit.h"
#include "AST/Type.h"

#include <algorithm>
#include <format>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>

namespace {

std::string
JoinFieldNames(std::initializer_list<std::string_view> allowed_fields) {
  std::string result;
  for (std::string_view field : allowed_fields) {
    if (!result.empty()) {
      result += ", ";
    }
    result += field;
  }
  return result;
}

template <typename Range> std::string JoinValues(const Range &values) {
  std::string result;
  for (std::string_view value : values) {
    if (!result.empty()) {
      result += ", ";
    }
    result += value;
  }
  return result;
}

std::vector<std::string_view> TypeTagNames(TypeRef::TagType tags) {
  std::vector<std::string_view> names;
  if ((tags & static_cast<TypeRef::TagType>(TypeTag::Const)) != 0) {
    names.emplace_back("const");
  }
  if ((tags & static_cast<TypeRef::TagType>(TypeTag::Unsigned)) != 0) {
    names.emplace_back("unsigned");
  }
  return names;
}

void VerifyTypeTags(TypeRef value, const YAML::Node &tags,
                    VerifyContext &context) {
  if (!tags) {
    return;
  }

  VerifyContextGuard guard(context, "tags");
  if (!tags.IsSequence()) {
    context.AddMessage("expected a sequence");
    return;
  }

  TypeRef::TagType expected_tags = 0;
  std::vector<std::string_view> expected_names;
  for (size_t i = 0; i < tags.size(); ++i) {
    VerifyContextGuard item_guard(context, std::to_string(i));
    YAML::Node tag = tags[i];
    if (!tag.IsScalar()) {
      context.AddMessage("expected a scalar type tag");
      continue;
    }

    const std::string name = tag.Scalar();
    TypeTag expected_tag;
    if (name == "const") {
      expected_tag = TypeTag::Const;
    } else if (name == "unsigned") {
      expected_tag = TypeTag::Unsigned;
    } else {
      context.AddMessage(std::format(
          "unknown type tag '{}'; allowed tags: const, unsigned", name));
      continue;
    }

    const TypeRef::TagType bit = TypeTagValue(expected_tag);
    if ((expected_tags & bit) != 0) {
      context.AddMessage(std::format("duplicate type tag '{}'", name));
      continue;
    }
    expected_tags |= bit;
    expected_names.push_back(name == "const" ? "const" : "unsigned");
  }

  const TypeRef::TagType actual_tags = value.get_tag() & SupportedTypeTags;
  if (actual_tags != expected_tags) {
    context.AddMessage(std::format("expected type tags [{}], got [{}]",
                                   JoinValues(expected_names),
                                   JoinValues(TypeTagNames(actual_tags))));
  }
}

using ContextVerifier = void (*)(const TreeNode *tree_node,
                                 const YAML::Node &expected,
                                 VerifyContext &context);

struct StringHash {
  using is_transparent = void;

  size_t operator()(const std::string &value) const {
    return std::hash<std::string_view>{}(value);
  }

  size_t operator()(std::string_view value) const {
    return std::hash<std::string_view>{}(value);
  }
};

struct StringEqual {
  using is_transparent = void;

  bool operator()(const std::string &left, const std::string &right) const {
    return left == right;
  }

  bool operator()(std::string_view left, std::string_view right) const {
    return left == right;
  }
};

using ContextVerifierMap =
    std::unordered_map<std::string, ContextVerifier, StringHash, StringEqual>;

ContextVerifierMap &ContextVerifiers() {
  static ContextVerifierMap verifiers;
  return verifiers;
}

std::string NormalizeParameterName(std::string_view name) {
  if (name == "<noname>" || name == "<anonymous>") {
    return {};
  }
  return std::string(name);
}

void VerifyParameterNames(const FunctionDecl &function,
                          const YAML::Node &expected, VerifyContext &context) {
  YAML::Node names = expected["parameter_names"];
  if (!names) {
    return;
  }

  VerifyContextGuard guard(context, "parameter_names");
  if (!names.IsSequence()) {
    context.AddMessage("expected a sequence");
    return;
  }
  if (names.size() != function.parameter_names.size()) {
    context.AddMessage(std::format("expected {} names, got {}",
                                   function.parameter_names.size(),
                                   names.size()));
    return;
  }

  for (size_t i = 0; i < names.size(); ++i) {
    VerifyContextGuard item_guard(context, std::to_string(i));
    YAML::Node expected_name = names[i];
    if (!expected_name.IsScalar()) {
      context.AddMessage("expected a scalar");
      continue;
    }

    const std::string expected_value =
        NormalizeParameterName(expected_name.Scalar());
    const std::string actual_value(function.parameter_names[i]);
    if (actual_value != expected_value) {
      context.AddMessage(
          std::format("expected '{}', got '{}'", expected_value, actual_value));
    }
  }
}

void VerifyStorageClass(const VarDecl &decl, const YAML::Node &expected,
                        VerifyContext &context) {
  YAML::Node storage_class = expected["storage_class"];
  if (!storage_class) {
    return;
  }

  VerifyContextGuard guard(context, "storage_class");
  if (!storage_class.IsScalar()) {
    context.AddMessage("expected a scalar");
    return;
  }

  std::string_view actual =
      decl.storage_class == StorageClass::Static ? "Static" : "None";
  if (storage_class.Scalar() != actual) {
    context.AddMessage(
        std::format("expected '{}', got '{}'", actual, storage_class.Scalar()));
  }
}

void VerifyFunctionStorageClass(const FunctionDecl &decl,
                                const YAML::Node &expected,
                                VerifyContext &context) {
  YAML::Node storage_class = expected["storage_class"];
  if (!storage_class) {
    return;
  }

  VerifyContextGuard guard(context, "storage_class");
  if (!storage_class.IsScalar()) {
    context.AddMessage("expected a scalar");
    return;
  }

  std::string_view actual =
      decl.storage_class == StorageClass::Static ? "Static" : "None";
  if (storage_class.Scalar() != actual) {
    context.AddMessage(
        std::format("expected '{}', got '{}'", actual, storage_class.Scalar()));
  }
}

void VerifyLiteralValue(
    const std::variant<long double, uint64_t, int64_t> &value,
    const YAML::Node &expected, VerifyContext &context) {
  if (!expected) {
    return;
  }

  VerifyContextGuard guard(context, "value");
  if (!expected.IsScalar()) {
    context.AddMessage("expected a scalar");
    return;
  }

  bool value_matches = false;
  try {
    std::visit(
        [&](auto actual_value) {
          using ValueType = std::decay_t<decltype(actual_value)>;
          value_matches = expected.as<ValueType>() == actual_value;
        },
        value);
  } catch (const YAML::BadConversion &) {
    value_matches = false;
  }
  if (!value_matches) {
    context.AddMessage("literal value does not match");
  }
}

template <typename Field>
void VerifyNamedTypeFields(const std::vector<Field> &fields,
                           const YAML::Node &expected, VerifyContext &context) {
  YAML::Node expected_fields = expected["fields"];
  if (!expected_fields) {
    return;
  }

  VerifyContextGuard guard(context, "fields");
  if (!expected_fields.IsSequence()) {
    context.AddMessage("expected a sequence");
    return;
  }
  if (expected_fields.size() != fields.size()) {
    context.AddMessage(std::format("expected {} fields, got {}", fields.size(),
                                   expected_fields.size()));
    return;
  }

  for (size_t i = 0; i < fields.size(); ++i) {
    VerifyContextGuard item_guard(context, std::to_string(i));
    YAML::Node expected_field = expected_fields[i];
    if (!expected_field.IsMap()) {
      context.AddMessage("expected a field descriptor mapping");
      continue;
    }
    VerifyFields(expected_field, {"name", "type"}, context);
    VerifyField("name", std::string_view(fields[i].name), expected_field,
                context);
    VerifyField("type", fields[i].type, expected_field, context);
  }
}

void VerifyEnumValues(const std::vector<EnumValue> &values,
                      const YAML::Node &expected, VerifyContext &context) {
  YAML::Node expected_values = expected["enumerators"];
  if (!expected_values) {
    return;
  }

  VerifyContextGuard guard(context, "enumerators");
  if (!expected_values.IsSequence()) {
    context.AddMessage("expected a sequence");
    return;
  }
  if (expected_values.size() != values.size()) {
    context.AddMessage(std::format("expected {} enumerators, got {}",
                                   values.size(), expected_values.size()));
    return;
  }

  for (size_t i = 0; i < values.size(); ++i) {
    VerifyContextGuard item_guard(context, std::to_string(i));
    YAML::Node expected_value = expected_values[i];
    if (!expected_value.IsMap()) {
      context.AddMessage("expected an enumerator descriptor mapping");
      continue;
    }
    VerifyFields(expected_value, {"name", "value"}, context);
    VerifyField("name", std::string_view(values[i].name), expected_value,
                context);

    YAML::Node value = expected_value["value"];
    if (!value) {
      continue;
    }
    VerifyContextGuard value_guard(context, "value");
    if (!value.IsScalar()) {
      context.AddMessage("expected a scalar");
      continue;
    }
    bool matches = false;
    try {
      std::visit(
          [&](auto actual) {
            using ValueType = std::decay_t<decltype(actual)>;
            matches = value.as<ValueType>() == actual;
          },
          values[i].value);
    } catch (const YAML::BadConversion &) {
      matches = false;
    }
    if (!matches) {
      context.AddMessage("enumerator value does not match");
    }
  }
}

void VerifyFieldInitializers(
    const std::vector<std::pair<std::string_view, std::unique_ptr<Initializer>>>
        &fields,
    const YAML::Node &expected, VerifyContext &context) {
  YAML::Node expected_fields = expected["field_initializers"];
  if (!expected_fields) {
    return;
  }

  VerifyContextGuard guard(context, "field_initializers");
  if (!expected_fields.IsSequence()) {
    context.AddMessage("expected a sequence");
    return;
  }
  if (expected_fields.size() != fields.size()) {
    context.AddMessage(std::format("expected {} initializers, got {}",
                                   fields.size(), expected_fields.size()));
    return;
  }

  for (size_t i = 0; i < fields.size(); ++i) {
    VerifyContextGuard item_guard(context, std::to_string(i));
    YAML::Node expected_field = expected_fields[i];
    if (!expected_field.IsMap()) {
      context.AddMessage("expected a field initializer mapping");
      continue;
    }
    VerifyFields(expected_field, {"name", "value"}, context);
    VerifyField("name", fields[i].first, expected_field, context);
    VerifyField("value", fields[i].second, expected_field, context);
  }
}

void VerifyIntrinsicArgs(const IntrinsicCall &call, const YAML::Node &expected,
                         VerifyContext &context) {
  YAML::Node args = expected["args"];
  if (!args) {
    return;
  }

  VerifyContextGuard guard(context, "args");
  if (!args.IsSequence()) {
    context.AddMessage("expected a sequence");
    return;
  }
  if (args.size() != call.args.size()) {
    context.AddMessage(std::format("expected {} arguments, got {}",
                                   call.args.size(), args.size()));
    return;
  }

  for (size_t i = 0; i < call.args.size(); ++i) {
    VerifyContextGuard item_guard(context, std::to_string(i));
    YAML::Node expected_arg = args[i];
    if (!expected_arg.IsMap() || expected_arg.size() != 1) {
      context.AddMessage("expected a single intrinsic argument descriptor");
      continue;
    }

    if (expected_arg.contains("Expr")) {
      VerifyContextGuard kind_guard(context, "Expr");
      const auto *arg = std::get_if<std::unique_ptr<ASTNode>>(&call.args[i]);
      if (!arg) {
        context.AddMessage("expected an expression argument");
        continue;
      }
      VerifyTreeNode(arg->get(), expected_arg["Expr"], context);
    } else if (expected_arg.contains("Type")) {
      VerifyContextGuard kind_guard(context, "Type");
      const auto *arg = std::get_if<TypeRef>(&call.args[i]);
      if (!arg) {
        context.AddMessage("expected a type argument");
        continue;
      }
      VerifyTypeRef(*arg, expected_arg["Type"], context);
    } else {
      context.AddMessage(
          "unknown intrinsic argument kind; allowed kinds: Expr, Type");
    }
  }
}

BEGIN_VERIFY_NODE(TranslationUnit)
VERIFY_FIELDS("decls")
VERIFY_FIELD(decls)
END_VERIFY_NODE()

BEGIN_VERIFY_NODE(RawLLVMIR)
VERIFY_FIELDS("ir")
VERIFY_FIELD(ir)
END_VERIFY_NODE()

BEGIN_VERIFY_NODE(DeclStmt)
VERIFY_FIELDS("decls")
VERIFY_FIELD(decls)
END_VERIFY_NODE()

BEGIN_VERIFY_NODE(CompoundStmt)
VERIFY_FIELDS("statements")
VERIFY_FIELD(statements)
END_VERIFY_NODE()

BEGIN_VERIFY_NODE(FunctionDecl)
VERIFY_FIELDS("name", "function_type", "parameter_names", "body",
              "storage_class")
VERIFY_FIELD(name)
VERIFY_FIELD(function_type)
VerifyParameterNames(*node, expected, context);
VerifyFunctionStorageClass(*node, expected, context);
VERIFY_FIELD(body)
END_VERIFY_NODE()

BEGIN_VERIFY_NODE(FunctionType)
VERIFY_FIELDS("return_type", "parameters", "has_va_args", "tags")
VERIFY_FIELD(return_type)
VERIFY_FIELD(parameters)
VERIFY_FIELD(has_va_args)
END_VERIFY_NODE()

BEGIN_VERIFY_NODE(VarDecl)
VERIFY_FIELDS("name", "type", "storage_class", "initializer")
VERIFY_FIELD(name)
VERIFY_FIELD(type)
VerifyStorageClass(*node, expected, context);
VERIFY_FIELD(initializer)
END_VERIFY_NODE()

BEGIN_VERIFY_NODE(TypedefDecl)
VERIFY_FIELDS("name", "type")
VERIFY_FIELD(name)
VERIFY_FIELD(type)
END_VERIFY_NODE()

BEGIN_VERIFY_NODE(ValueInitializer)
VERIFY_FIELDS("value")
VERIFY_FIELD(value)
END_VERIFY_NODE()

BEGIN_VERIFY_NODE(ArrayInitializer)
VERIFY_FIELDS("initializers")
VERIFY_FIELD(initializers)
END_VERIFY_NODE()

BEGIN_VERIFY_NODE(FieldInitializer)
VERIFY_FIELDS("field_initializers")
VerifyFieldInitializers(node->field_initializers, expected, context);
END_VERIFY_NODE()

BEGIN_VERIFY_NODE(LiteralString)
VERIFY_FIELDS("value")
VERIFY_FIELD(value)
END_VERIFY_NODE()

BEGIN_VERIFY_NODE(LiteralNum)
VERIFY_FIELDS("type", "value")
VERIFY_FIELD(type)
VerifyLiteralValue(node->value, expected["value"], context);
END_VERIFY_NODE()

BEGIN_VERIFY_NODE(VarRef)
VERIFY_FIELDS("name")
VERIFY_FIELD(name)
END_VERIFY_NODE()

BEGIN_VERIFY_NODE(ReturnStmt)
VERIFY_FIELDS("ret_value")
VERIFY_FIELD(ret_value)
END_VERIFY_NODE()

BEGIN_VERIFY_NODE(BinaryOp)
VERIFY_FIELDS("op_type", "left", "right")
VERIFY_FIELD(op_type)
VERIFY_FIELD(left)
VERIFY_FIELD(right)
END_VERIFY_NODE()

BEGIN_VERIFY_NODE(UnaryOp)
VERIFY_FIELDS("op_type", "operand")
VERIFY_FIELD(op_type)
VERIFY_FIELD(operand)
END_VERIFY_NODE()

BEGIN_VERIFY_NODE(TypeCast)
VERIFY_FIELDS("op_type", "target_type", "operand")
VERIFY_FIELD(op_type)
VERIFY_FIELD(target_type)
VERIFY_FIELD(operand)
END_VERIFY_NODE()

BEGIN_VERIFY_NODE(GetField)
VERIFY_FIELDS("op_type", "field_name", "operand")
VERIFY_FIELD(op_type)
VERIFY_FIELD(field_name)
VERIFY_FIELD(operand)
END_VERIFY_NODE()

BEGIN_VERIFY_NODE(PtrGetField)
VERIFY_FIELDS("op_type", "field_name", "operand")
VERIFY_FIELD(op_type)
VERIFY_FIELD(field_name)
VERIFY_FIELD(operand)
END_VERIFY_NODE()

BEGIN_VERIFY_NODE(FunctionInvocation)
VERIFY_FIELDS("function", "arguments")
VERIFY_FIELD(function)
VERIFY_FIELD(arguments)
END_VERIFY_NODE()

BEGIN_VERIFY_NODE(SequenceEval)
VERIFY_FIELDS("expressions")
VERIFY_FIELD(expressions)
END_VERIFY_NODE()

BEGIN_VERIFY_NODE(IntrinsicCall)
VERIFY_FIELDS("name", "args")
if (node->desc) {
  VerifyField("name", std::string_view(node->desc->name), expected, context);
}
VerifyIntrinsicArgs(*node, expected, context);
END_VERIFY_NODE()

BEGIN_VERIFY_NODE(StructDecl)
VERIFY_FIELDS("name", "type", "fields")
VERIFY_FIELD(name)
VERIFY_FIELD(type)
VERIFY_FIELD(fields)
END_VERIFY_NODE()

BEGIN_VERIFY_NODE(UnionDecl)
VERIFY_FIELDS("name", "type", "fields")
VERIFY_FIELD(name)
VERIFY_FIELD(type)
VERIFY_FIELD(fields)
END_VERIFY_NODE()

BEGIN_VERIFY_NODE(IfStmt)
VERIFY_FIELDS("condition", "then_branch", "else_branch")
VERIFY_FIELD(condition)
VERIFY_FIELD(then_branch)
VERIFY_FIELD(else_branch)
END_VERIFY_NODE()

BEGIN_VERIFY_NODE(WhileStmt)
VERIFY_FIELDS("condition", "body")
VERIFY_FIELD(condition)
VERIFY_FIELD(body)
END_VERIFY_NODE()

BEGIN_VERIFY_NODE(DoWhileStmt)
VERIFY_FIELDS("condition", "body")
VERIFY_FIELD(condition)
VERIFY_FIELD(body)
END_VERIFY_NODE()

BEGIN_VERIFY_NODE(ForStmt)
VERIFY_FIELDS("init", "condition", "increment", "body")
VERIFY_FIELD(init)
VERIFY_FIELD(condition)
VERIFY_FIELD(increment)
VERIFY_FIELD(body)
END_VERIFY_NODE()

BEGIN_VERIFY_NODE(SwitchStmt)
VERIFY_FIELDS("value", "cases")
VERIFY_FIELD(value)
VERIFY_FIELD(cases)
END_VERIFY_NODE()

BEGIN_VERIFY_NODE(CaseStmt)
VERIFY_FIELDS("case_vals", "body")
VERIFY_FIELD(case_vals)
VERIFY_FIELD(body)
END_VERIFY_NODE()

BEGIN_VERIFY_NODE(DefaultStmt)
VERIFY_FIELDS("body")
VERIFY_FIELD(body)
END_VERIFY_NODE()

BEGIN_VERIFY_NODE(BreakStmt)
VERIFY_FIELDS()
END_VERIFY_NODE()

BEGIN_VERIFY_NODE(ContinueStmt)
VERIFY_FIELDS()
END_VERIFY_NODE()

BEGIN_VERIFY_NODE(EnumDecl)
VERIFY_FIELDS("name", "type", "underlying_type", "values", "is_definition")
VERIFY_FIELD(name)
VERIFY_FIELD(type)
VERIFY_FIELD(underlying_type)
VERIFY_FIELD(values)
VERIFY_FIELD(is_definition)
END_VERIFY_NODE()

BEGIN_VERIFY_NODE(EnumElemDecl)
VERIFY_FIELDS("name", "value")
VERIFY_FIELD(name)
VERIFY_FIELD(value)
END_VERIFY_NODE()

BEGIN_VERIFY_NODE(LabelStmt)
VERIFY_FIELDS("name")
VERIFY_FIELD(name)
END_VERIFY_NODE()

BEGIN_VERIFY_NODE(GotoStmt)
VERIFY_FIELDS("target")
VERIFY_FIELD(target)
END_VERIFY_NODE()

BEGIN_VERIFY_NODE(EmptyStmt)
VERIFY_FIELDS()
END_VERIFY_NODE()

BEGIN_VERIFY_NODE(ConditionalExpr)
VERIFY_FIELDS("condition", "true_expr", "false_expr")
VERIFY_FIELD(condition)
VERIFY_FIELD(true_expr)
VERIFY_FIELD(false_expr)
END_VERIFY_NODE()

BEGIN_VERIFY_NODE(BuiltinType)
VERIFY_FIELDS("type_name", "tags")
VERIFY_FIELD(type_name)
END_VERIFY_NODE()

BEGIN_VERIFY_NODE(PointerType)
VERIFY_FIELDS("pointee_type", "tags")
VERIFY_FIELD(pointee_type)
END_VERIFY_NODE()

BEGIN_VERIFY_NODE(ArrayType)
VERIFY_FIELDS("element_type", "size", "tags")
VERIFY_FIELD(element_type)
VERIFY_FIELD(size)
END_VERIFY_NODE()

BEGIN_VERIFY_NODE(StructType)
VERIFY_FIELDS("type_name", "scope_id", "is_complete", "fields", "tags")
VERIFY_FIELD(type_name)
VERIFY_FIELD(scope_id)
VERIFY_FIELD(is_complete)
VerifyNamedTypeFields(node->fields, expected, context);
END_VERIFY_NODE()

BEGIN_VERIFY_NODE(UnionType)
VERIFY_FIELDS("type_name", "scope_id", "is_complete", "fields", "tags")
VERIFY_FIELD(type_name)
VERIFY_FIELD(scope_id)
VERIFY_FIELD(is_complete)
VerifyNamedTypeFields(node->fields, expected, context);
END_VERIFY_NODE()

BEGIN_VERIFY_NODE(EnumType)
VERIFY_FIELDS("enum_name", "scope_id", "underlying_type", "is_complete",
              "enumerators", "tags")
VERIFY_FIELD(enum_name)
VERIFY_FIELD(scope_id)
VERIFY_FIELD(underlying_type)
VERIFY_FIELD(is_complete)
VerifyEnumValues(node->enumerators, expected, context);
END_VERIFY_NODE()

} // namespace

void VerifyTypeRef(TypeRef value, const YAML::Node &expected,
                   VerifyContext &context) {
  if (!expected || expected.IsNull()) {
    if (value) {
      context.AddMessage("expected null");
    }
    return;
  }
  if (!value) {
    context.AddMessage("AST type is missing");
    return;
  }
  if (!expected.IsMap()) {
    context.AddMessage("expected a type descriptor mapping");
    return;
  }

  const std::string node_type(value->GetNodeTypeName());
  VerifyTreeNode(value.get(), expected, context);
  if (expected.size() == 1 && expected[node_type].IsMap()) {
    VerifyTypeTags(value, expected[node_type]["tags"], context);
  }
}

void VerifyFields(const YAML::Node &expected,
                  std::initializer_list<std::string_view> allowed_fields,
                  VerifyContext &context) {
  if (!expected.IsMap()) {
    context.AddMessage("expected a field descriptor mapping");
    return;
  }

  for (const auto &field : expected) {
    YAML::Node field_name_node = field.first;
    if (!field_name_node.IsScalar()) {
      context.AddMessage(
          std::format("expected a scalar field name; allowed fields: {}",
                      JoinFieldNames(allowed_fields)));
      continue;
    }

    const std::string field_name = field_name_node.Scalar();
    if (std::ranges::find(allowed_fields, field_name) != allowed_fields.end()) {
      continue;
    }

    context.AddMessage(std::format("unknown field '{}'; allowed fields: {}",
                                   field_name, JoinFieldNames(allowed_fields)));
  }
}

void VerifyTreeNode(const TreeNode *tree_node, const YAML::Node &expected,
                    VerifyContext &context) {
  if (!tree_node) {
    context.AddMessage("AST node is missing");
    return;
  }
  if (!expected || !expected.IsMap()) {
    context.AddMessage("expected a node descriptor mapping");
    return;
  }

  const std::string_view node_type = tree_node->GetNodeTypeName();
  if (expected.size() != 1 || !expected.contains(std::string(node_type))) {
    context.AddMessage(
        std::format("expected a single '{}' node descriptor", node_type));
    return;
  }

  auto verifier = ContextVerifiers().find(node_type);
  if (verifier == ContextVerifiers().end()) {
    context.AddMessage(
        std::format("verifier for '{}' is not registered", node_type));
    return;
  }
  verifier->second(tree_node, expected[std::string(node_type)], context);
}

std::vector<std::string> FindMissingVerifierNames() {
  std::vector<std::string> missing;
  for (const auto &[value, name] : EnumRefl<ASTNodeType>::GetValues()) {
    if (value != ASTNodeType::Unknown && !ContextVerifiers().contains(name)) {
      missing.emplace_back(name);
    }
  }
  for (const auto &[value, name] : EnumRefl<TypeBaseType>::GetValues()) {
    (void)value;
    if (!ContextVerifiers().contains(name)) {
      missing.emplace_back(name);
    }
  }
  return missing;
}

namespace {

const bool context_verifiers_registered = [] {
#define REGISTER_CONTEXT_VERIFIER(NODE)                                        \
  ContextVerifiers().emplace(#NODE, Verify##NODE)
  REGISTER_CONTEXT_VERIFIER(TranslationUnit);
  REGISTER_CONTEXT_VERIFIER(RawLLVMIR);
  REGISTER_CONTEXT_VERIFIER(DeclStmt);
  REGISTER_CONTEXT_VERIFIER(CompoundStmt);
  REGISTER_CONTEXT_VERIFIER(FunctionDecl);
  REGISTER_CONTEXT_VERIFIER(VarDecl);
  REGISTER_CONTEXT_VERIFIER(TypedefDecl);
  REGISTER_CONTEXT_VERIFIER(ValueInitializer);
  REGISTER_CONTEXT_VERIFIER(ArrayInitializer);
  REGISTER_CONTEXT_VERIFIER(FieldInitializer);
  REGISTER_CONTEXT_VERIFIER(LiteralString);
  REGISTER_CONTEXT_VERIFIER(LiteralNum);
  REGISTER_CONTEXT_VERIFIER(VarRef);
  REGISTER_CONTEXT_VERIFIER(ReturnStmt);
  ContextVerifiers().emplace("BinaryExpr", VerifyBinaryOp);
  ContextVerifiers().emplace("UnaryExpr", VerifyUnaryOp);
  REGISTER_CONTEXT_VERIFIER(TypeCast);
  REGISTER_CONTEXT_VERIFIER(GetField);
  REGISTER_CONTEXT_VERIFIER(PtrGetField);
  REGISTER_CONTEXT_VERIFIER(FunctionInvocation);
  REGISTER_CONTEXT_VERIFIER(SequenceEval);
  REGISTER_CONTEXT_VERIFIER(IntrinsicCall);
  REGISTER_CONTEXT_VERIFIER(StructDecl);
  REGISTER_CONTEXT_VERIFIER(UnionDecl);
  REGISTER_CONTEXT_VERIFIER(IfStmt);
  REGISTER_CONTEXT_VERIFIER(WhileStmt);
  REGISTER_CONTEXT_VERIFIER(DoWhileStmt);
  REGISTER_CONTEXT_VERIFIER(ForStmt);
  REGISTER_CONTEXT_VERIFIER(SwitchStmt);
  REGISTER_CONTEXT_VERIFIER(CaseStmt);
  REGISTER_CONTEXT_VERIFIER(DefaultStmt);
  REGISTER_CONTEXT_VERIFIER(BreakStmt);
  REGISTER_CONTEXT_VERIFIER(ContinueStmt);
  REGISTER_CONTEXT_VERIFIER(EnumDecl);
  REGISTER_CONTEXT_VERIFIER(EnumElemDecl);
  REGISTER_CONTEXT_VERIFIER(LabelStmt);
  REGISTER_CONTEXT_VERIFIER(GotoStmt);
  REGISTER_CONTEXT_VERIFIER(EmptyStmt);
  REGISTER_CONTEXT_VERIFIER(ConditionalExpr);
  REGISTER_CONTEXT_VERIFIER(BuiltinType);
  REGISTER_CONTEXT_VERIFIER(PointerType);
  REGISTER_CONTEXT_VERIFIER(ArrayType);
  REGISTER_CONTEXT_VERIFIER(StructType);
  REGISTER_CONTEXT_VERIFIER(UnionType);
  REGISTER_CONTEXT_VERIFIER(EnumType);
  REGISTER_CONTEXT_VERIFIER(FunctionType);
#undef REGISTER_CONTEXT_VERIFIER
  return true;
}();

} // namespace
