#include "Macro.h"
#include "../Util/Diagnostic.h"

static SourceLocation CreateArgLiteralString(std::vector<Token> &tokens,
                                             SourceManager &source_manager) {
  std::string str = "\"";

  for (size_t i = 0; i < tokens.size(); ++i) {
    const Token &tok = tokens[i];

    if (i > 0 && tok.has_leading_space) {
      str += ' ';
    }

    for (char c : source_manager.GetText(tok)) {
      if (c == '\\' || c == '\"') {
        str += '\\';
      }
      str += c;
    }
  }

  str += "\"";

  return source_manager.AddSynthetic(std::move(str));
}

static bool IsMacroArgConcatable(Token t, SourceManager &source_manager) {
  for (char c : source_manager.GetText(t)) {
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '_')) {
      return false;
    }
  }
  return true;
}

static bool IsIdentifierLike(Token t) {
  return t.type == TokenType::Identifier || IsKeyword(t.type);
}

std::optional<std::pair<size_t, size_t>>
MacroScanner::IsArgConcatPrefix() const {
  if (_idx + 2 < _def.body.size()) {
    Token tok0 = _def.body[_idx];
    Token tok1 = _def.body[_idx + 1];
    Token tok2 = _def.body[_idx + 2];
    if (IsIdentifierLike(tok0) && tok1.type == TokenType::SharpSharp &&
        IsIdentifierLike(tok2)) {
      size_t param0_idx = _def.GetParamIndex(_source_manager.GetText(tok0));
      size_t param1_idx = _def.GetParamIndex(_source_manager.GetText(tok2));
      return std::make_pair(param0_idx, param1_idx);
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
    if (_idx + 1 >= _def.body.size()) {
      _diagnostics.Fail(curr_body_tok.location,
                        "Expected macro parameter after '#'");
    }
    Token next_tok = _def.body[_idx + 1];

    size_t param_idx = _def.GetParamIndex(_source_manager.GetText(next_tok));
    if (param_idx == (size_t)-1) {
      _diagnostics.Fail(next_tok.location, "Unknown macro parameter: ",
                        _source_manager.GetText(next_tok));
    }

    Token res{
        TokenType::LiteralString,
        CreateArgLiteralString(_args[param_idx], _source_manager),
        curr_body_tok.has_leading_space,
    };
    // _idx: #
    // _idx+1: param
    _idx += 1;

    _cached_token = res;
    _cache_valid = true;
    return res;
  }
  if (auto pp = IsArgConcatPrefix(); pp.has_value()) {
    auto [param0_idx, param1_idx] = pp.value();
    if (!(param0_idx == (size_t)-1 || _args[param0_idx].size() == 1)) {
      _diagnostics.Fail(_def.body[_idx + 0].location,
                        "Macro argument concatenation only supports single "
                        "token arguments");
    }
    if (!(param1_idx == (size_t)-1 || _args[param1_idx].size() == 1)) {
      _diagnostics.Fail(_def.body[_idx + 2].location,
                        "Macro argument concatenation only supports single "
                        "token arguments");
    }
    Token arg0 =
        param0_idx == (size_t)-1 ? _def.body[_idx + 0] : _args[param0_idx][0];
    Token arg1 =
        param1_idx == (size_t)-1 ? _def.body[_idx + 2] : _args[param1_idx][0];

    if (!IsMacroArgConcatable(arg0, _source_manager) ||
        !IsMacroArgConcatable(arg1, _source_manager)) {
      _diagnostics.Fail(
          curr_body_tok.location,
          "Macro argument concatenation only supports identifiers and "
          "keywords");
    }

    SourceLocation connected = _source_manager.AddSynthetic(std::format(
        "{}{}", _source_manager.GetText(arg0), _source_manager.GetText(arg1)));
    Token res{TokenType::Identifier, connected,
              curr_body_tok.has_leading_space};

    TokenType as_keyword_type =
        TryGetKeywordTokenType(_source_manager.GetText(connected));
    if (as_keyword_type != TokenType::Unknown) {
      res.type = as_keyword_type;
    }

    _idx += 2;

    _cache_valid = true;
    _cached_token = res;
    return res;
  }
  if (curr_body_tok.type == TokenType::Identifier) {
    if (auto param_idx =
            _def.GetParamIndex(_source_manager.GetText(curr_body_tok));
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
