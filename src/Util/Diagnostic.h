#pragma once

#include "../Exception.h"
#include "../Lex/SourceLocation.h"
#include "../SourceManager.h"
#include "Error.h"

#include <concepts>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

struct DiagnosticMessage {
  std::vector<SourceLocation> locations;
  std::shared_ptr<const IError> error;
  std::string formatted_message;

  std::string ToString() const;
};

class DiagnosticError : public Exception {
public:
  explicit DiagnosticError(std::vector<DiagnosticMessage> messages)
      : Exception(messages.empty() ? "diagnostic error"
                                   : messages.back().ToString()),
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
  template <typename ErrorType>
    requires std::derived_from<std::remove_cvref_t<ErrorType>, IError>
  void Error(SourceLocation location, ErrorType &&error) {
    using StoredError = std::remove_cvref_t<ErrorType>;
    auto stored_error =
        std::make_shared<StoredError>(std::forward<ErrorType>(error));
    std::string formatted_message = stored_error->ToString();
    _messages.push_back({
        .locations = {location},
        .error = std::move(stored_error),
        .formatted_message = std::move(formatted_message),
    });
  }

  // Record a fatal error and unwind with all diagnostics collected so far.
  template <typename ErrorType>
    requires std::derived_from<std::remove_cvref_t<ErrorType>, IError>
  [[noreturn]] void Fail(SourceLocation location, ErrorType &&error) {
    Error(location, std::forward<ErrorType>(error));
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
