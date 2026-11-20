#include "Diagnostic.h"

std::string DiagnosticMessage::ToString() const { return formatted_message; }

static std::string
FormatMessages(const std::vector<DiagnosticMessage> &messages,
               const SourceManager &source_manager) {
  std::string result;
  for (const DiagnosticMessage &message : messages) {
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
  return FormatMessages(messages, _source_manager);
}

std::string DiagnosticContext::Format(const DiagnosticMessage &message) const {
  return Format(std::vector<DiagnosticMessage>{message});
}
