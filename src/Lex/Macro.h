#pragma once

#include "../SourceManager.h"
#include "Scanner.h"

#include <memory>
#include <vector>

class MacroDef {
public:
  std::string_view name;
  bool has_arg = false;
  std::vector<std::string_view> params;
  std::vector<Token> body;

  size_t GetParamIndex(std::string_view param) const {
    for (size_t i = 0; i < params.size(); ++i) {
      if (params[i] == param) {
        return i;
      }
    }
    return (size_t)-1;
  }
};

class MacroContext {
public:
  MacroDef *FindMacro(std::string_view name) {
    for (auto it = _macros.rbegin(); it != _macros.rend(); ++it) {
      if ((*it)->name == name) {
        return (*it).get();
      }
    }
    return nullptr;
  }

  void AddMacro(MacroDef macro) {
    if (auto *m = FindMacro(macro.name)) {
      *m = macro;
    } else {
      _macros.push_back(std::make_unique<MacroDef>(std::move(macro)));
    }
  }

  void RemoveMacro(std::string_view name) {
    for (auto it = _macros.rbegin(); it != _macros.rend(); ++it) {
      if ((*it)->name == name) {
        _macros.erase(std::next(it).base());
        return;
      }
    }
  }

public:
  std::vector<std::unique_ptr<MacroDef>> _macros;
};

class MacroScanner : public IScanner {
  IScannerImpl(MacroScanner);

public:
  MacroScanner(MacroDef &def, std::vector<std::vector<Token>> args,
               SourceManager &source_manager, DiagnosticContext &diagnostics)
      : _def(def), _source_manager(source_manager), _diagnostics(diagnostics),
        _args(args) {}

  Token PeekNextToken() override;

  void ConsumeToken(Token token) override;

private:
  std::optional<std::pair<size_t, size_t>> IsArgConcatPrefix() const;

  MacroDef &_def;
  SourceManager &_source_manager;
  DiagnosticContext &_diagnostics;
  size_t _idx = 0;
  bool _is_in_arg_tok = false;
  size_t _arg_idx = 0;
  size_t _arg_tok_idx = 0;
  std::vector<std::vector<Token>> _args;

  Token _cached_token;
  bool _cache_valid = false;
};
