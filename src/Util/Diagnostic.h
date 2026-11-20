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

  // Record a structured error without imposing a control-flow policy.
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

  bool HasErrors() const { return !_messages.empty(); }

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
