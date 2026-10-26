"""GDB pretty-printers for src/Util/TaggedPointer.h.

Load this file in GDB with:

    source /path/to/ccompiler/debug/TaggedPointer.py

The printers decode the stored uintptr_t directly.  They do not call methods
on the value being inspected, so printing a TaggedPointer never runs code in
the inferior process.
"""

import re

import gdb


_BASE_KINDS = frozenset(("TaggedVoidPointer", "TaggedPointerBase"))
_TAGGED_KINDS = frozenset(("TaggedPointer", "TaggerPointer"))
_KIND_RE = re.compile(
    r"^(?:const\s+|volatile\s+)*"
    r"(?:(?:[A-Za-z_]\w*|\(anonymous namespace\))::)*"
    r"(?P<kind>TaggedVoidPointer|TaggedPointerBase|TaggedPointer|TaggerPointer)"
    r"(?:\s*<.*>)?$"
)


def _unqualified_type(value_type):
    try:
        value_type = value_type.unqualified()
    except (AttributeError, gdb.error):
        pass
    try:
        value_type = value_type.strip_typedefs()
    except (AttributeError, gdb.error):
        pass
    return value_type


def _type_name(value_type):
    value_type = _unqualified_type(value_type)
    try:
        return str(value_type)
    except (gdb.error, RuntimeError):
        return ""


def _template_integer(value_type, index):
    """Return an integer template argument, or None if GDB cannot provide it."""
    candidates = (value_type, _unqualified_type(value_type))
    for candidate in candidates:
        try:
            argument = candidate.template_argument(index)
            result = int(argument)
        except (AttributeError, gdb.error, RuntimeError, TypeError, ValueError):
            continue
        if result > 0 and (result & (result - 1)) == 0:
            return result
    return None


def _alignment_from_name(type_name, kind):
    if kind in _BASE_KINDS:
        match = re.search(r"<\s*(\d+)\s*>", type_name)
    else:
        match = re.search(r",\s*(\d+)\s*>\s*$", type_name)
    if match is None:
        return None
    result = int(match.group(1))
    if result > 0 and (result & (result - 1)) == 0:
        return result
    return None


def _alignment(value_type, kind):
    # TaggedVoidPointer<T>: the alignment is the only template argument.
    # TaggedPointer<T, alignment>: alignment is the second argument.
    argument_index = 0 if kind in _BASE_KINDS else 1
    result = _template_integer(value_type, argument_index)
    if result is not None:
        return result

    result = _alignment_from_name(_type_name(value_type), kind)
    if result is not None:
        return result

    # GDB normally exposes default template arguments.  Keep the printer
    # usable for older GDB/DWARF combinations where the alignment was elided.
    return 1 << 3


def _template_type(value_type, index):
    for candidate in (value_type, _unqualified_type(value_type)):
        try:
            return candidate.template_argument(index)
        except (AttributeError, gdb.error, RuntimeError):
            continue
    return None


def _unwrap_reference(value):
    type_code = value.type.code
    if type_code in (gdb.TYPE_CODE_REF, gdb.TYPE_CODE_RVALUE_REF):
        return value.referenced_value()
    return value


def _stored_pointer(value, alignment):
    value = _unwrap_reference(value)
    try:
        field = value["_ptr"]
        if field.is_optimized_out:
            return None
        return int(field)
    except (gdb.error, KeyError, RuntimeError, TypeError, ValueError):
        pass

    # Some GDB versions do not expose a field inherited through a private
    # base when indexing the derived value directly.  Cast to the base and
    # try once more.
    for base_name in (
        "TaggedVoidPointer<{}>".format(alignment),
        "TaggedPointerBase<{}>".format(alignment),
    ):
        try:
            base_type = gdb.lookup_type(base_name)
            field = value.cast(base_type)["_ptr"]
            if not field.is_optimized_out:
                return int(field)
        except (gdb.error, KeyError, RuntimeError, TypeError, ValueError):
            continue
    return None


class _TaggedPointerPrinter:
    def __init__(self, value, kind):
        self.value = value
        self.kind = kind
        self.alignment = _alignment(value.type, kind)
        self.raw = _stored_pointer(value, self.alignment)

        if self.raw is None:
            self.pointer = None
            self.tag = None
        else:
            self.pointer = self.raw & ~(self.alignment - 1)
            self.tag = self.raw & (self.alignment - 1)

    def to_string(self):
        type_name = _type_name(self.value.type)
        if self.raw is None:
            return "{} = <unavailable>".format(type_name)
        return "{}(pointer={:#x}, tag={:#x})".format(
            type_name, self.pointer, self.tag
        )

    def _pointer_value(self):
        if self.pointer is None:
            return gdb.Value(0)

        element_type = None
        if self.kind in _TAGGED_KINDS:
            element_type = _template_type(self.value.type, 0)
        if element_type is None:
            try:
                element_type = gdb.lookup_type("void")
            except gdb.error:
                return gdb.Value(self.pointer)

        try:
            return gdb.Value(self.pointer).cast(element_type.pointer())
        except (gdb.error, RuntimeError):
            return gdb.Value(self.pointer)

    def children(self):
        if self.raw is None:
            return
        yield "pointer", self._pointer_value()
        yield "tag", gdb.Value(self.tag)


class TaggedPointerBasePrinter(_TaggedPointerPrinter):
    """Printer for TaggedVoidPointer and TaggedPointerBase."""


class TaggedPointerPrinter(_TaggedPointerPrinter):
    """Printer for TaggedPointer<T, alignment> and TaggerPointer<T>."""


class TaggedPointerPrinterLookup:
    def __call__(self, value):
        match = _KIND_RE.match(_type_name(value.type))
        if match is None:
            return None

        kind = match.group("kind")
        if kind in _BASE_KINDS:
            return TaggedPointerBasePrinter(value, kind)
        return TaggedPointerPrinter(value, kind)


_PRINTER_LOOKUP = TaggedPointerPrinterLookup()
_PRINTER_LOOKUP._ccompiler_tagged_pointer_printer = True


def register_pretty_printers(obj=None):
    if obj is None:
        obj = gdb

    # Re-sourcing this file should not accumulate duplicate printers.
    obj.pretty_printers[:] = [
        printer
        for printer in obj.pretty_printers
        if not getattr(printer, "_ccompiler_tagged_pointer_printer", False)
    ]
    obj.pretty_printers.append(_PRINTER_LOOKUP)


register_pretty_printers()
