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
template <typename Error>
[[noreturn]] void RaiseParserError(CompileContext &context,
                                   SourceLocation location, Error &&error) {
  context.Diagnostics().Error(location, std::forward<Error>(error));
  throw ParserRecovery();
}

template <typename Error>
[[noreturn]] void RaiseParserError(DiagnosticContext &diagnostics,
                                   SourceLocation location, Error &&error) {
  diagnostics.Error(location, std::forward<Error>(error));
  throw ParserRecovery();
}
