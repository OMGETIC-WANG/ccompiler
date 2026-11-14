#pragma once

#include "../Exception.h"
#include "Context.h"

// Unwinds to the nearest statement list without ending compilation.
class ParserRecovery : public Exception {
public:
  ParserRecovery() : Exception("recoverable parser error") {}
};

// Record a recoverable syntax error, then leave the current malformed
// construct so the enclosing statement list can synchronize.
template <typename... MessageChunks>
[[noreturn]] void RaiseParserError(CompileContext &context,
                                   SourceLocation location,
                                   const MessageChunks &...chunks) {
  context.Diagnostics().Error(location, chunks...);
  throw ParserRecovery();
}

template <typename... MessageChunks>
[[noreturn]] void RaiseParserError(DiagnosticContext &diagnostics,
                                   SourceLocation location,
                                   const MessageChunks &...chunks) {
  diagnostics.Error(location, chunks...);
  throw ParserRecovery();
}
