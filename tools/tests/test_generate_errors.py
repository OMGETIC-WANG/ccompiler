from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from tools import generate_errors

VALID_DEFINITION = """
UnknownVar:
  format: "Unknown var: %var_name%"
  fields:
    - var_name: string_view

TypeMismatch:
  format: "Type mismatch: expected %expected_type%, got %actual_type%"
  fields:
    - expected_type: TypeRef
    - actual_type: TypeRef
"""


class GenerateErrorsTest(unittest.TestCase):
    def load(self, source: str):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "errors.yaml"
            path.write_text(source, encoding="utf-8")
            return generate_errors.load_definitions(path)

    def test_loads_definitions_and_generates_outputs(self):
        definitions = self.load(VALID_DEFINITION)
        self.assertEqual(
            [error.name for error in definitions], ["UnknownVar", "TypeMismatch"]
        )

        header = generate_errors.render_header(definitions)
        source = generate_errors.render_source(definitions)
        verifier = generate_errors.render_verify_source(definitions)
        self.assertNotIn("yaml-cpp/", header)
        self.assertIn("class Node;", header)
        self.assertIn("struct UnknownVar final : IError", header)
        self.assertIn("FormatTypeRef(expected_type)", source)
        self.assertIn("VerifyErrorFields", verifier)
        self.assertIn("MatchesTypeRef", verifier)

    def test_rejects_duplicate_yaml_keys(self):
        with self.assertRaisesRegex(
            generate_errors.GenerationError, "duplicate YAML key"
        ):
            self.load(
                VALID_DEFINITION + "\nUnknownVar:\n  format: duplicate\n"
                "  fields: []\n"
            )

    def test_rejects_unknown_placeholder(self):
        with self.assertRaisesRegex(
            generate_errors.GenerationError, "unknown field 'missing'"
        ):
            self.load("""
BadMessage:
  format: "value=%missing%"
  fields:
    - value: string
""")

    def test_rejects_unused_field(self):
        with self.assertRaisesRegex(
            generate_errors.GenerationError, "fields not used in format"
        ):
            self.load("""
BadMessage:
  format: "constant"
  fields:
    - value: string
""")

    def test_rejects_unknown_field_type_and_keyword(self):
        with self.assertRaisesRegex(
            generate_errors.GenerationError, "unsupported type"
        ):
            self.load("""
BadMessage:
  format: "%value%"
  fields:
    - value: pointer
""")
        with self.assertRaisesRegex(generate_errors.GenerationError, "C\\+\\+ keyword"):
            self.load("""
class:
  format: "bad"
  fields: []
""")

    def test_rejects_duplicate_fields(self):
        with self.assertRaisesRegex(generate_errors.GenerationError, "duplicate field"):
            self.load("""
BadMessage:
  format: "%value% %value%"
  fields:
    - value: string
    - value: string
""")

    def test_escapes_literal_cpp_format_braces(self):
        definitions = self.load("""
Braces:
  format: "literal {braces}: %value%"
  fields:
    - value: size_t
""")
        source = generate_errors.render_source(definitions)
        self.assertIn('std::format("literal {{braces}}: {0}", value)', source)
        self.assertEqual(source, generate_errors.render_source(definitions))

    def test_placeholders_are_independent_of_field_order(self):
        definitions = self.load("""
Ordered:
  format: "%second% then %first%"
  fields:
    - first: string
    - second: string
""")
        source = generate_errors.render_source(definitions)
        self.assertIn('std::format("{1} then {0}", first, second)', source)

    def test_rejects_empty_format(self):
        with self.assertRaisesRegex(
            generate_errors.GenerationError, "format must not be empty"
        ):
            self.load("""
BadMessage:
  format: ""
  fields: []
""")


if __name__ == "__main__":
    unittest.main()
