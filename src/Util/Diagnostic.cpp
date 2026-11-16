#include "Diagnostic.h"

std::string DiagnosticMessage::ToString() const { return formatted_message; }

std::string
DiagnosticError::ToString(const SourceManager &source_manager) const {
  std::string result;
  for (const DiagnosticMessage &message : _messages) {
    if (!result.empty()) {
      result += '\n';
    }
    const std::string message_text = message.ToString();
    if (message.locations.empty() || !message.locations.front().IsValid()) {
      result += "ccompiler: error: " + message_text;
      continue;
    }
    result += source_manager.FormatLocation(message.locations.front()) +
              ": error: " + message_text;
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
