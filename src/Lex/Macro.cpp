#include "Macro.h"
#include "../Exception.h"

static std::string_view CreateArgLiteralString(std::vector<Token> &tokens,
                                               SourceManager &source_manager) {
  std::string str = "\"";

  for (size_t i = 0; i < tokens.size(); ++i) {
    const Token &tok = tokens[i];

    if (i > 0 && tok.has_leading_space) {
      str += ' ';
    }

    for (char c : tok.value) {
      if (c == '\\' || c == '\"') {
        str += '\\';
      }
      str += c;
    }
  }

  str += "\"";

  return source_manager.AddString(std::move(str));
}

static bool IsMacroArgConcatable(Token t) {
  for (char c : t.value) {
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '_')) {
      return false;
    }
  }
  return true;
}

std::optional<std::pair<size_t, size_t>>
MacroScanner::IsArgConcatPrefix() const {
  if (_idx + 2 < _def.body.size()) {
    Token tok0 = _def.body[_idx];
    Token tok1 = _def.body[_idx + 1];
    Token tok2 = _def.body[_idx + 2];
    if (tok0.type == TokenType::Identifier &&
        tok1.type == TokenType::SharpSharp &&
        tok2.type == TokenType::Identifier) {
      size_t param0_idx = _def.GetParamIndex(tok0.value);
      size_t param1_idx = _def.GetParamIndex(tok2.value);
      if (param0_idx != (size_t)-1 && param1_idx != (size_t)-1) {
        return std::make_pair(param0_idx, param1_idx);
      }
    }
  }
  return std::nullopt;
}

Token MacroScanner::PeekNextToken() {
  if (_cache_valid) {
    return _cached_token;
  }
  if (_is_in_arg_tok) {
    return _args[_arg_idx][_arg_tok_idx];
  }

  Token curr_body_tok;
  if (_idx < _def.body.size()) {
    curr_body_tok = _def.body[_idx];
  } else {
    return Token{TokenType::EndOfFile};
  }

  if (!_def.has_arg) {
    return curr_body_tok;
  }

  if (curr_body_tok.type == TokenType::Sharp) {
    ASSERT(_idx + 1 < _def.body.size(), "Expected token after # in macro body");
    Token next_tok = _def.body[_idx + 1];

    size_t param_idx = _def.GetParamIndex(next_tok.value);
    ASSERT(param_idx != (size_t)-1,
           "Unknown macro parameter: ", next_tok.value);

    Token res{TokenType::LiteralString,
              CreateArgLiteralString(_args[param_idx], _source_manager),
              curr_body_tok.has_leading_space};
    // _idx: #
    // _idx+1: param
    _idx += 1;

    _cached_token = res;
    _cache_valid = true;
    return res;
  }
  if (auto pp = IsArgConcatPrefix(); pp.has_value()) {
    auto [param0_idx, param1_idx] = pp.value();
    std::vector<Token> &arg0s = _args[param0_idx];
    std::vector<Token> &arg1s = _args[param1_idx];
    ASSERT(arg0s.size() == 1 && arg1s.size() == 1,
           "Macro argument concatenation only supports single token arguments");
    Token arg0 = arg0s[0];
    Token arg1 = arg1s[0];
    ASSERT(
        IsMacroArgConcatable(arg0) && IsMacroArgConcatable(arg1),
        "Macro argument concatenation only supports identifiers and keywords");

    std::string_view connected =
        _source_manager.AddString(std::format("{}{}", arg0.value, arg1.value));
    Token res{TokenType::Identifier, connected,
              curr_body_tok.has_leading_space};

    TokenType as_keyword_type = TryGetKeywordTokenType(connected);
    if (as_keyword_type != TokenType::Unknown) {
      res.type = as_keyword_type;
    }

    _idx += 2;

    _cache_valid = true;
    _cached_token = res;
    return res;
  }
  if (curr_body_tok.type == TokenType::Identifier) {
    if (auto param_idx = _def.GetParamIndex(curr_body_tok.value);
        param_idx != (size_t)-1) {
      auto &arg = _args[param_idx];
      if (arg.empty()) {
        _idx += 1;
        _cache_valid = false;
        return PeekNextToken();
      }
      _is_in_arg_tok = true;
      _arg_idx = param_idx;
      _arg_tok_idx = 0;
      _cache_valid = true;
      _cached_token = _args[param_idx][_arg_tok_idx];
      return _cached_token;
    }
  }
  return curr_body_tok;
}

void MacroScanner::ConsumeToken(Token tok) {
  if (_is_in_arg_tok) {
    _arg_tok_idx++;
    if (_arg_tok_idx >= _args[_arg_idx].size()) {
      _is_in_arg_tok = false;
      _arg_tok_idx = 0;
      _arg_idx = 0;
      _idx++;
    }
  } else {
    _idx++;
  }
  _cache_valid = false;
}
