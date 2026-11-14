#pragma once

#include "../Exception.h"
#include "../Lex/SourceLocation.h"
#include "../SourceManager.h"

#include <string>
#include <utility>
#include <vector>

struct DiagnosticMessage {
  std::vector<SourceLocation> locations;
  std::string message;
};

class DiagnosticError : public Exception {
public:
  explicit DiagnosticError(std::vector<DiagnosticMessage> messages)
      : Exception(messages.empty() ? "diagnostic error"
                                   : messages.front().message),
        _messages(std::move(messages)) {}

  const std::vector<DiagnosticMessage> &Messages() const { return _messages; }

  std::string ToString(const SourceManager &source_manager) const;

private:
  std::vector<DiagnosticMessage> _messages;
};

class DriverError : public Exception {
public:
  using Exception::Exception;
};

class DiagnosticContext {
public:
  explicit DiagnosticContext(const SourceManager &source_manager)
      : _source_manager(source_manager) {}

  void PushLocation(SourceLocation loc) { _loc_stack.push_back(loc); }

  void PopLocation() {
    if (!_loc_stack.empty()) {
      _loc_stack.pop_back();
    }
  }

  template <typename... MessageChunks>
  void AddError(SourceLocation location, const MessageChunks &...chunks) {
    _messages.push_back(
        {.locations = {location}, .message = _BuildMessage(chunks...)});
  }

  template <typename... MessageChunks>
  [[noreturn]] void RaiseError(SourceLocation location,
                               const MessageChunks &...chunks) {
    AddError(location, chunks...);
    throw DiagnosticError({_messages.back()});
  }

  const std::vector<DiagnosticMessage> &Messages() const { return _messages; }

  std::string Format(const DiagnosticMessage &message) const;

  std::string Format(const std::vector<DiagnosticMessage> &messages) const;

private:
  const SourceManager &_source_manager;

  // Reserved for future macro expansion or contextual locations. Phase-one
  // diagnostics use one explicit primary location instead.
  std::vector<SourceLocation> _loc_stack;

  std::vector<DiagnosticMessage> _messages;
};
