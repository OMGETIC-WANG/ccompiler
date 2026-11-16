#!/usr/bin/env python3

"""Generate structured C++ diagnostics from a YAML definition file."""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path

import yaml

CPP_TYPES = {
    "string": "std::string",
    "string_view": "std::string_view",
    "char": "char",
    "size_t": "std::size_t",
    "TypeRef": "TypeRef",
    "TokenType": "TokenType",
    "ASTNodeType": "ASTNodeType",
    "BinaryOpType": "BinaryOpType",
    "UnaryOpType": "UnaryOpType",
}

CPP_KEYWORDS = {
    "alignas",
    "alignof",
    "and",
    "and_eq",
    "asm",
    "atomic_cancel",
    "atomic_commit",
    "atomic_noexcept",
    "auto",
    "bitand",
    "bitor",
    "bool",
    "break",
    "case",
    "catch",
    "char",
    "char8_t",
    "char16_t",
    "char32_t",
    "class",
    "compl",
    "concept",
    "const",
    "consteval",
    "constexpr",
    "constinit",
    "const_cast",
    "continue",
    "co_await",
    "co_return",
    "co_yield",
    "decltype",
    "default",
    "delete",
    "do",
    "double",
    "dynamic_cast",
    "else",
    "enum",
    "explicit",
    "export",
    "extern",
    "false",
    "float",
    "for",
    "friend",
    "goto",
    "if",
    "inline",
    "int",
    "long",
    "mutable",
    "namespace",
    "new",
    "noexcept",
    "not",
    "not_eq",
    "nullptr",
    "operator",
    "or",
    "or_eq",
    "private",
    "protected",
    "public",
    "reflexpr",
    "register",
    "reinterpret_cast",
    "requires",
    "return",
    "short",
    "signed",
    "sizeof",
    "static",
    "static_assert",
    "static_cast",
    "struct",
    "switch",
    "synchronized",
    "template",
    "this",
    "thread_local",
    "throw",
    "true",
    "try",
    "typedef",
    "typeid",
    "typename",
    "union",
    "unsigned",
    "using",
    "virtual",
    "void",
    "volatile",
    "wchar_t",
    "while",
    "xor",
    "xor_eq",
}

IDENTIFIER_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
PLACEHOLDER_RE = re.compile(r"%([A-Za-z_][A-Za-z0-9_]*)%")


class GenerationError(Exception):
    pass


class UniqueKeyLoader(yaml.SafeLoader):
    pass


def construct_unique_mapping(loader, node, deep=False):
    loader.flatten_mapping(node)
    mapping = {}
    for key_node, value_node in node.value:
        key = loader.construct_object(key_node, deep=deep)
        if key in mapping:
            raise GenerationError(f"duplicate YAML key: {key!r}")
        mapping[key] = loader.construct_object(value_node, deep=deep)
    return mapping


UniqueKeyLoader.add_constructor(
    yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, construct_unique_mapping
)


@dataclass(frozen=True)
class Field:
    name: str
    type_name: str

    @property
    def cpp_type(self) -> str:
        return CPP_TYPES[self.type_name]


@dataclass(frozen=True)
class ErrorDefinition:
    name: str
    format_string: str
    fields: tuple[Field, ...]

    def cpp_format_string(self) -> str:
        placeholder_index = {
            field.name: index for index, field in enumerate(self.fields)
        }
        used = {match.group(1) for match in PLACEHOLDER_RE.finditer(self.format_string)}
        unknown = sorted(used - set(placeholder_index))
        if unknown:
            if len(unknown) == 1:
                raise GenerationError(
                    f"{self.name}: format references unknown field " f"{unknown[0]!r}"
                )
            raise GenerationError(
                f"{self.name}: format references unknown fields: "
                f"{', '.join(unknown)}"
            )
        unused = sorted(set(placeholder_index) - used)
        if unused:
            raise GenerationError(
                f"{self.name}: fields not used in format: {', '.join(unused)}"
            )

        result = []
        cursor = 0
        for match in PLACEHOLDER_RE.finditer(self.format_string):
            literal = self.format_string[cursor : match.start()]
            if "%" in literal:
                raise GenerationError(f"{self.name}: invalid '%' in format string")
            result.append(literal.replace("{", "{{").replace("}", "}}"))
            field_name = match.group(1)
            if field_name not in placeholder_index:
                raise GenerationError(
                    f"{self.name}: format references unknown field {field_name!r}"
                )
            result.append(f"{{{placeholder_index[field_name]}}}")
            cursor = match.end()

        trailing = self.format_string[cursor:]
        if "%" in trailing:
            raise GenerationError(f"{self.name}: invalid '%' in format string")
        result.append(trailing.replace("{", "{{").replace("}", "}}"))
        return "".join(result)


def validate_identifier(value: str, description: str) -> None:
    if not IDENTIFIER_RE.fullmatch(value):
        raise GenerationError(f"{description} is not a C++ identifier: {value!r}")
    if value in CPP_KEYWORDS:
        raise GenerationError(f"{description} is a C++ keyword: {value!r}")


def load_definitions(path: Path) -> list[ErrorDefinition]:
    try:
        document = yaml.load(path.read_text(encoding="utf-8"), Loader=UniqueKeyLoader)
    except (OSError, yaml.YAMLError) as error:
        raise GenerationError(f"failed to load {path}: {error}") from error

    if not isinstance(document, dict) or not document:
        raise GenerationError("error definitions must be a non-empty mapping")

    definitions = []
    for name, descriptor in document.items():
        if not isinstance(name, str):
            raise GenerationError(f"error name must be a string: {name!r}")
        validate_identifier(name, "error name")
        if not isinstance(descriptor, dict):
            raise GenerationError(f"{name}: descriptor must be a mapping")
        if set(descriptor) != {"format", "fields"}:
            expected = "format, fields"
            actual = ", ".join(str(key) for key in descriptor)
            raise GenerationError(f"{name}: expected keys [{expected}], got [{actual}]")
        format_string = descriptor["format"]
        if not isinstance(format_string, str):
            raise GenerationError(f"{name}: format must be a string")
        if not format_string:
            raise GenerationError(f"{name}: format must not be empty")
        field_descriptors = descriptor["fields"]
        if not isinstance(field_descriptors, list):
            raise GenerationError(f"{name}: fields must be a sequence")

        fields = []
        field_names = set()
        for index, field_descriptor in enumerate(field_descriptors):
            if not isinstance(field_descriptor, dict) or len(field_descriptor) != 1:
                raise GenerationError(
                    f"{name}: field {index} must be a one-entry mapping"
                )
            field_name, type_name = next(iter(field_descriptor.items()))
            if not isinstance(field_name, str):
                raise GenerationError(f"{name}: field name {index} must be a string")
            validate_identifier(field_name, f"{name}.{field_name}")
            if field_name in field_names:
                raise GenerationError(f"{name}: duplicate field {field_name!r}")
            if not isinstance(type_name, str):
                raise GenerationError(
                    f"{name}.{field_name}: type name must be a string"
                )
            if type_name not in CPP_TYPES:
                raise GenerationError(
                    f"{name}.{field_name}: unsupported type {type_name!r}"
                )
            field_names.add(field_name)
            fields.append(Field(field_name, type_name))

        error = ErrorDefinition(name, format_string, tuple(fields))
        error.cpp_format_string()
        definitions.append(error)

    return definitions


def render_header(definitions: list[ErrorDefinition]) -> str:
    lines = [
        "// Generated by tools/generate_errors.py. Do not edit.",
        "#pragma once",
        "",
        "#include <cstddef>",
        "#include <string>",
        "#include <string_view>",
        "",
        '#include "AST/ASTNode.h"',
        '#include "AST/Operations.h"',
        '#include "AST/Type.h"',
        '#include "Lex/Token.h"',
        '#include "Util/Error.h"',
        "",
        "// Verify implementations are generated into a test-only source file.",
        "namespace YAML {",
        "class Node;",
        "} // namespace YAML",
        "",
        "namespace errs {",
        "",
    ]
    for error in definitions:
        lines.append(f"struct {error.name} final : IError {{")
        for field in error.fields:
            lines.append(f"  {field.cpp_type} {field.name};")
        if error.fields:
            lines.append("")
            parameters = ", ".join(
                f"{field.cpp_type} {field.name}_value" for field in error.fields
            )
            initializers = ", ".join(
                f"{field.name}({field.name}_value)" for field in error.fields
            )
            lines.append(f"  explicit {error.name}({parameters})")
            lines.append(f"      : {initializers} {{}}")
            lines.append("")
        lines.extend(
            [
                "  std::string ToString() const override;",
                "  bool Verify(const YAML::Node &node) const;",
                "};",
                "",
            ]
        )
    lines.extend(
        [
            "bool VerifyErrorFields(const IError &error, std::string_view error_name,",
            "                       const YAML::Node &fields);",
            "",
            "} // namespace errs",
            "",
        ]
    )
    return "\n".join(lines)


def render_source(definitions: list[ErrorDefinition]) -> str:
    lines = [
        "// Generated by tools/generate_errors.py. Do not edit.",
        '#include "Diagnostics/Errors.generated.h"',
        "",
        "#include <format>",
        "",
        "namespace errs {",
        "",
        "namespace {",
        "",
        "std::string FormatTypeRef(TypeRef type) {",
        '  return type ? type->ToString() : std::string("<null>");',
        "}",
        "",
        "} // namespace",
        "",
    ]
    for error in definitions:
        format_string = json.dumps(error.cpp_format_string(), ensure_ascii=False)
        arguments = "".join(
            f", {'FormatTypeRef(' if field.type_name == 'TypeRef' else ''}"
            f"{field.name}{')' if field.type_name == 'TypeRef' else ''}"
            for field in error.fields
        )
        lines.extend(
            [
                f"std::string {error.name}::ToString() const {{",
                f"  return std::format({format_string}{arguments});",
                "}",
                "",
            ]
        )
    lines.extend(["}", ""])
    return "\n".join(lines)


def render_verify_source(definitions: list[ErrorDefinition]) -> str:
    lines = [
        "// Generated by tools/generate_errors.py. Do not edit.",
        '#include "Diagnostics/Errors.generated.h"',
        "",
        '#include "Support/ASTVerifier.h"',
        "",
        "#include <format>",
        "#include <string>",
        "#include <string_view>",
        "#include <yaml-cpp/yaml.h>",
        "",
        "namespace {",
        "",
        "bool MatchesScalar(const YAML::Node &node, const auto &value) {",
        '  return node.IsScalar() && node.Scalar() == std::format("{}", value);',
        "}",
        "",
        "bool MatchesTypeRef(const YAML::Node &node, TypeRef value) {",
        "  if (node.IsScalar()) {",
        '    return node.Scalar() == (value ? value->ToString() : "<null>");',
        "  }",
        "  VerifyContext context;",
        "  VerifyTypeRef(value, node, context);",
        "  return context.messages.empty();",
        "}",
        "",
        "bool MatchesField(const YAML::Node &node, const auto &value) {",
        "  return MatchesScalar(node, value);",
        "}",
        "",
        "bool MatchesField(const YAML::Node &node, TypeRef value) {",
        "  return MatchesTypeRef(node, value);",
        "}",
        "",
        "std::string_view FieldName(const YAML::Node &node) {",
        "  if (!node.IsScalar()) {",
        "    return {};",
        "  }",
        "  return node.Scalar();",
        "}",
        "",
        "} // namespace",
        "",
        "namespace errs {",
        "",
    ]
    for error in definitions:
        lines.extend(
            [
                f"bool {error.name}::Verify(const YAML::Node &node) const {{",
                "  if (!node.IsMap()) {",
                "    return false;",
                "  }",
                "  for (const auto &entry : node) {",
                "    const std::string_view field_name = FieldName(entry.first);",
            ]
        )
        if error.fields:
            for index, field in enumerate(error.fields):
                prefix = "    if" if index == 0 else "    else if"
                lines.extend(
                    [
                        f'{prefix} (field_name == "{field.name}") {{',
                        f"      if (!MatchesField(entry.second, {field.name})) {{",
                        "        return false;",
                        "      }",
                        "    }",
                    ]
                )
            lines.extend(
                [
                    "    else {",
                    "      return false;",
                    "    }",
                ]
            )
        else:
            lines.append("    (void)field_name;")
            lines.append("    return false;")
        lines.extend(["  }", "  return true;", "}", ""])

    lines.extend(
        [
            "bool VerifyErrorFields(const IError &error, std::string_view error_name,",
            "                       const YAML::Node &fields) {",
        ]
    )
    for error in definitions:
        lines.extend(
            [
                f'  if (error_name == "{error.name}") {{',
                f"    const auto *typed = dynamic_cast<const {error.name} *>(&error);",
                "    return typed != nullptr && typed->Verify(fields);",
                "  }",
            ]
        )
    lines.extend(["  return false;", "}", "", "} // namespace errs", ""])
    return "\n".join(lines)


def write_output(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--definitions", required=True, type=Path)
    parser.add_argument("--header", required=True, type=Path)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--verify-source", required=True, type=Path)
    args = parser.parse_args()

    try:
        definitions = load_definitions(args.definitions)
        write_output(args.header, render_header(definitions))
        write_output(args.source, render_source(definitions))
        write_output(args.verify_source, render_verify_source(definitions))
    except GenerationError as error:
        print(f"error generation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
