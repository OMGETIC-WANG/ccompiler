#pragma once

#include "Context.h"

// Record a recoverable syntax error. The caller returns std::nullopt so the
// enclosing statement list can synchronize without unwinding.
template <typename Error>
void ReportParserError(CompileContext &context, SourceLocation location,
                       Error &&error) {
  context.Diagnostics().Error(location, std::forward<Error>(error));
  context.RequestParserRecovery();
}
