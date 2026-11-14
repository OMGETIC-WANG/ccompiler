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
                                   : messages.back().message),
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

  // Record an error that a caller can recover from without unwinding.
  template <typename... MessageChunks>
  void Error(SourceLocation location, const MessageChunks &...chunks) {
    _messages.push_back(
        {.locations = {location}, .message = _BuildMessage(chunks...)});
  }

  // Record a fatal error and unwind with all diagnostics collected so far.
  template <typename... MessageChunks>
  [[noreturn]] void Fail(SourceLocation location,
                         const MessageChunks &...chunks) {
    Error(location, chunks...);
    throw DiagnosticError(_messages);
  }

  bool HasErrors() const { return !_messages.empty(); }

  void ThrowIfErrors() const {
    if (HasErrors()) {
      throw DiagnosticError(_messages);
    }
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
