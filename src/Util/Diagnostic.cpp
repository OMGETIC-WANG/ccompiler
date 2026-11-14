#include "Diagnostic.h"

std::string
DiagnosticError::ToString(const SourceManager &source_manager) const {
  std::string result;
  for (const DiagnosticMessage &message : _messages) {
    if (!result.empty()) {
      result += '\n';
    }
    if (message.locations.empty() || !message.locations.front().IsValid()) {
      result += "ccompiler: error: " + message.message;
      continue;
    }
    result += source_manager.FormatLocation(message.locations.front()) +
              ": error: " + message.message;
  }
  return result;
}

std::string DiagnosticContext::Format(
    const std::vector<DiagnosticMessage> &messages) const {
  DiagnosticError error(messages);
  return error.ToString(_source_manager);
}

std::string DiagnosticContext::Format(const DiagnosticMessage &message) const {
  return Format(std::vector<DiagnosticMessage>{message});
}
