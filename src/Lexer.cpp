#include "Lexer.h"
#include "Util/File.h"
#include "Util/StringUtil.h"
#include <algorithm>
#include <cassert>

static bool IsBlank(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static bool IsIdentifierBreak(char c) {
  std::string_view breaks = " \t\n\r\"\'()[]{};:,+-*/=&|!<>^%~#";
  return breaks.find(c) != std::string_view::npos;
}

// skip blank and comments
static size_t TrimBlank(std::string_view s, size_t start = 0) {
  size_t pos = start;
  while (pos < s.size()) {
    if (IsBlank(s[pos])) {
      pos++;
    } else if (s[pos] == '/') {
      if (pos + 1 < s.size()) {
        if (s[pos + 1] == '/') {
          pos += 2;
          while (pos < s.size() && s[pos] != '\n') {
            pos++;
          }
        } else if (s[pos + 1] == '*') {
          pos += 2;
          while (pos + 1 < s.size() && !(s[pos] == '*' && s[pos + 1] == '/')) {
            pos++;
          }
          if (pos + 1 < s.size()) {
            pos += 2;
          }
        } else {
          break;
        }
      }
    } else {
      break;
    }
  }
  return pos;
}

static size_t GotoNextLine(std::string_view s, size_t start = 0) {
  size_t nextline = s.find('\n', start);
  return nextline == std::string_view::npos ? s.size() : nextline + 1;
}

static bool IsIdentifierStart(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool IsIdentifierPart(char c) {
  return IsIdentifierStart(c) || (c >= '0' && c <= '9');
}

static Token TryPeekAsIdentifier(std::string_view s) {
  if (s.empty() || !IsIdentifierStart(s[0])) {
    return Token{TokenType::Unknown};
  }
  size_t pos = 0;
  while (pos < s.size() && IsIdentifierPart(s[pos])) {
    pos++;
  }
  return Token{TokenType::Identifier, s.substr(0, pos)};
}

template <auto... valid_ranges>
static size_t AdvancePureNum(std::string_view s, size_t start) {
  size_t pos = start;
  while (pos < s.size()) {
    char c = s[pos];
    if (pos > start && c == '\'') {
      pos++;
      continue;
    }
    if ((... || (c >= valid_ranges.first && c <= valid_ranges.second))) {
      pos++;
    } else {
      break;
    }
  }
  return pos;
}

constexpr auto AdvancePureHexNum =
    AdvancePureNum<std::pair{'0', '9'}, std::pair{'a', 'f'},
                   std::pair{'A', 'F'}>;

constexpr auto AdvancePureDecNum = AdvancePureNum<std::pair{'0', '9'}>;

constexpr auto AdvancePureBinNum = AdvancePureNum<std::pair{'0', '1'}>;

static size_t AdvanceFloatNum(std::string_view s,
                              size_t (*advancer)(std::string_view, size_t),
                              size_t start) {
  size_t pos = advancer(s, start);
  if (pos < s.size() && s[pos] == '.') {
    pos++;
    pos = advancer(s, pos);
  }
  return pos;
}

static std::optional<std::string_view> TryPeekAsLiteralHex(std::string_view s) {
  if (s.starts_with("0x") || s.starts_with("0X")) {
    size_t pos = 2;
    pos = AdvanceFloatNum(s, AdvancePureHexNum, pos);
    if (pos == 2) {
      return std::nullopt;
    }
    if (pos < s.size() && (s[pos] == 'p' || s[pos] == 'P')) {
      pos++;
      if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) {
        pos++;
      }
      size_t advanced = AdvancePureDecNum(s, pos);
      if (advanced == pos) {
        RAISE_ERROR("Expected dec exponent after 'p' or 'P'");
      }
      pos = advanced;
    }
    return s.substr(0, pos);
  }
  return std::nullopt;
}

static std::optional<std::string_view> TryPeekAsLiteralDec(std::string_view s) {
  size_t pos = AdvanceFloatNum(s, AdvancePureDecNum, 0);
  if (pos == 0) {
    return std::nullopt;
  }
  if (pos < s.size() && (s[pos] == 'e' || s[pos] == 'E')) {
    pos++;
    if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) {
      pos++;
    }
    size_t advanced = AdvancePureDecNum(s, pos);
    if (advanced == pos) {
      RAISE_ERROR("Expected decimal exponent after 'e' or 'E'");
    }
    pos = advanced;
  }
  return s.substr(0, pos);
}

static std::optional<std::string_view> TryPeekAsLiteralBin(std::string_view s) {
  if (s.starts_with("0b") || s.starts_with("0B")) {
    size_t pos = 2;
    pos = AdvanceFloatNum(s, AdvancePureBinNum, pos);
    if (pos == 2) {
      return std::nullopt;
    }
    return s.substr(0, pos);
  }
  return std::nullopt;
}

static std::string_view TryAddPostfix(std::string_view s,
                                      std::string_view literal) {
  static std::string_view postfixes[] = {
      "ull", "ul", "ll", "l", "u", "f",
  };
  size_t pos = literal.size();
  std::string_view rest = s.substr(pos);
  for (const auto &postfix : postfixes) {
    if (StartsWithInsensitive(rest, postfix)) {
      return s.substr(0, pos + postfix.size());
    }
  }
  return literal;
}

static Token TryPeekAsLiteralNum(std::string_view s) {
  if (auto hex = TryPeekAsLiteralHex(s)) {
    return Token{TokenType::LiteralNum, TryAddPostfix(s, hex.value())};
  }
  if (auto bin = TryPeekAsLiteralBin(s)) {
    return Token{TokenType::LiteralNum, TryAddPostfix(s, bin.value())};
  }
  // Treat oct as dec in lexer
  if (auto dec = TryPeekAsLiteralDec(s)) {
    return Token{TokenType::LiteralNum, TryAddPostfix(s, dec.value())};
  }
  return Token{TokenType::Unknown};
}

static Token TryPeekAsLiteralChar(std::string_view s) {
  if (s.size() >= 3) {
    if (s[0] == '\'' && s[2] == '\'') {
      return Token{TokenType::LiteralChar, s.substr(1, 1)};
    }
  }
  return Token{TokenType::Unknown};
}

static bool IsSimpleEscape(char c) {
  switch (c) {
  case '0':
  case '\'':
  case '\"':
  case '?':
  case '\\':
  case 'a':
  case 'b':
  case 'f':
  case 'n':
  case 'r':
  case 't':
  case 'v':
    return true;
  default:
    return false;
  }
}

static size_t AdvanceEscapeSeq(std::string_view s, size_t start) {
  if (start < s.size()) {
    char next = s[start];
    if (IsSimpleEscape(next)) {
      return start + 1;
    } else if (next == 'x') {
      if (start + 2 < s.size()) { // x <num1> <num2>
        return start + 3;
      }
    } else if (next >= '0' && next <= '7') {
      if (start + 2 < s.size()) { // <oct0> <oct1> <oct2>
        return start + 3;
      }
    }
  }
  return (size_t)-1;
}

// Get the end of literal string, contains the quote, return -1 if quote not
// closed
static size_t GetLiteralStringEnd(std::string_view s) {
  assert(s[0] == '\"');
  size_t pos = 1;
  while (pos < s.size()) {
    if (s[pos] == '"') {
      return pos + 1;
    }
    if (s[pos] == '\\') {
      pos = AdvanceEscapeSeq(s, pos);
      if (pos == (size_t)-1) {
        return (size_t)-1;
      }
    } else {
      pos++;
    }
  }
  return (size_t)-1;
}

static Token TryPeekAsLiteralString(std::string_view s) {
  if (s.empty())
    return Token{TokenType::Unknown};
  if (s[0] != '"')
    return Token{TokenType::Unknown};
  size_t end = GetLiteralStringEnd(s);
  if (end == (size_t)-1)
    return Token{TokenType::Unknown};
  return Token{TokenType::LiteralString, s.substr(0, end)};
}

static Token TryPeekAsKeyword(std::string_view s) {
  static const std::pair<std::string_view, TokenType> keywords[] = {
      {"if", TokenType::KwIf},
      {"else", TokenType::KwElse},
      {"while", TokenType::KwWhile},
      {"for", TokenType::KwFor},
      {"return", TokenType::KwReturn},
      {"continue", TokenType::KwContinue},
      {"void", TokenType::KwVoid},
      {"static", TokenType::KwStatic},
      {"const", TokenType::KwConst},
      {"struct", TokenType::KwStruct},
      {"union", TokenType::KwUnion},
      {"typedef", TokenType::KwTypedef},
      {"enum", TokenType::KwEnum},
      {"switch", TokenType::KwSwitch},
      {"case", TokenType::KwCase},
      {"break", TokenType::KwBreak},
      {"default", TokenType::KwDefault},
      {"goto", TokenType::KwGoto},
      {"int", TokenType::KwInt},
      {"char", TokenType::KwChar},
      {"short", TokenType::KwShort},
      {"float", TokenType::KwFloat},
      {"double", TokenType::KwDouble},
      {"signed", TokenType::KwSigned},
      {"unsigned", TokenType::KwUnsigned},
      {"long", TokenType::KwLong},
      {"volatile", TokenType::KwVolatile},
      {"extern", TokenType::KwExtern},
      {"register", TokenType::KwRegister},
  };
  for (const auto &[keyword, type] : keywords) {
    if (s.starts_with(keyword)) {
      if (s.size() > keyword.size()) {
        char next = s[keyword.size()];
        if (IsIdentifierPart(next)) {
          continue;
        }
      }
      return Token{type, keyword};
    }
  }
  return Token{TokenType::Unknown};
}

static std::string_view GetMacroDecl(std::string_view line) {
  size_t pos = 0;
  pos = TrimBlank(line);
  while (pos < line.size() && !IsBlank(line[pos]) && line[pos] != '(') {
    pos++;
  }
  if (pos < line.size() && line[pos] == '(') {
    size_t decl_end = line.find(')', pos);
    assert(decl_end != std::string_view::npos);
    return line.substr(0, decl_end + 1);
  } else {
    return line.substr(0, pos);
  }
}

static std::string_view GetMacroContent(std::string_view line) {
  size_t pos = 0;
  while (pos < line.size()) {
    if (line[pos] == '\n') {
      break;
    }
    if (line[pos] == '/' && pos + 1 < line.size() &&
        (line[pos + 1] == '/' || line[pos + 1] == '*')) {
      break;
    }
    pos++;
  }
  return line.substr(0, pos);
}

static size_t FindPairRParen(std::string_view s, size_t lparen_pos = 0) {
  assert(s[lparen_pos] == '(');
  size_t pos = lparen_pos + 1;
  int depth = 1;
  while (pos < s.size()) {
    if (s[pos] == '(') {
      depth++;
    } else if (s[pos] == ')') {
      depth--;
      if (depth == 0) {
        return pos;
      }
    }
    pos++;
  }
  return std::string_view::npos;
}

std::pair<size_t, std::vector<std::string_view>>
SplitMacroArgs(std::string_view s) {
  std::vector<std::string_view> args;
  size_t pos = 0;
  size_t curr_start = 0;
  bool inside_line_comment = false;
  bool inside_block_comment = false;
  while (pos < s.size()) {
    if (!inside_line_comment && !inside_block_comment) {
      if (s[pos] == '/' && pos + 1 < s.size() && s[pos + 1] == '/') {
        inside_line_comment = true;
        pos += 2;
        continue;
      }
      if (s[pos] == '/' && pos + 1 < s.size() && s[pos + 1] == '*') {
        inside_block_comment = true;
        pos += 2;
        continue;
      }
    } else {
      if (inside_block_comment && s[pos] == '\n') {
        inside_line_comment = false;
        pos++;
        continue;
      }
      if (inside_block_comment && s[pos] == '*' && pos + 1 < s.size() &&
          s[pos + 1] == '/') {
        inside_block_comment = false;
        pos += 2;
        continue;
      }
    }

    if (inside_line_comment || inside_block_comment) {
      pos++;
      continue;
    }

    if (s[pos] == '(') {
      pos = FindPairRParen(s, pos);
      if (pos == std::string_view::npos) {
        break;
      }
      pos++;
    } else if (s[pos] == '\"') {
      pos = GetLiteralStringEnd(s.substr(pos));
    } else if (s[pos] == '\'') {
      pos += 3;
    } else if (s[pos] == ',') {
      args.push_back(s.substr(curr_start, pos - curr_start));
      pos++;
      curr_start = pos;
    } else if (s[pos] == ')') {
      args.push_back(s.substr(curr_start, pos - curr_start));
      pos++;
      break;
    } else {
      pos++;
    }
  }
  return {pos, args};
}

static std::string_view GetIncludeFile(std::string_view s) {
  if (s[0] == '<') {
    size_t end = s.find('>');
    assert(end != std::string_view::npos);
    return s.substr(0, end + 1);
  } else if (s[0] == '\"') {
    size_t end = s.find('\"', 1);
    assert(end != std::string_view::npos);
    return s.substr(0, end + 1);
  } else {
    assert(false && "Invalid include file");
    return {};
  }
}

void Lexer::InitMacroExpansionLexer(Token macro_identifier,
                                    MacroRecord &record) {
  _pos += macro_identifier.value.size();

  std::vector<std::string_view> args;

  if (record.arg_count) {
    size_t next_pos = TrimBlank(Rest());
    if (next_pos < Rest().size() && Rest()[next_pos] == '(') {
      _pos++;
      auto pair = SplitMacroArgs(Rest());
      auto end = pair.first;
      args = pair.second;
      _pos += end;
    }
  }

  _sub_lexer_source = record.Expand(args);

  if (!_sub_lexer) {
    _sub_lexer = std::make_unique<Lexer>(_sub_lexer_source, _macros);
  }

  _stage = LexerStage::InsideMacro;
}

static std::array<TokenType, 256> BuildSingleCharTokenMap() {
  std::array<TokenType, 256> map;
  map.fill(TokenType::Unknown);
  map['#'] = TokenType::PreprocessorSharp;
  map[';'] = TokenType::Semicolon;
  map[':'] = TokenType::Colon;
  map['('] = TokenType::LParen;
  map[')'] = TokenType::RParen;
  map['['] = TokenType::LBracket;
  map[']'] = TokenType::RBracket;
  map['{'] = TokenType::LBrace;
  map['}'] = TokenType::RBrace;
  map['*'] = TokenType::Star;
  map[','] = TokenType::Comma;
  map['='] = TokenType::Equal;
  map['&'] = TokenType::Et;
  map['.'] = TokenType::Dot;

  map['+'] = TokenType::Add;
  map['-'] = TokenType::Sub;
  map['/'] = TokenType::Div;
  map['%'] = TokenType::Mod;

  map['|'] = TokenType::BitOr;
  map['^'] = TokenType::BitXor;
  map['~'] = TokenType::BitNot;

  map['<'] = TokenType::Lt;
  map['>'] = TokenType::Gt;

  map['!'] = TokenType::Not;

  map['?'] = TokenType::Question;

  return map;
}

static Token TryPeekAsSingleCharToken(std::string_view s) {
  static std::array<TokenType, 256> single_char_tokens =
      BuildSingleCharTokenMap();

  if (s.empty()) {
    return Token{TokenType::Unknown};
  }
  if (single_char_tokens[s[0]] != TokenType::Unknown) {
    return Token{single_char_tokens[s[0]], s.substr(0, 1)};
  }
  return Token{TokenType::Unknown};
}

static Token TryPeekAsPrefixToken(std::string_view s) {
  static std::pair<std::string_view, TokenType> prefix_tokens[] = {
      {"==", TokenType::Eq},           {"!=", TokenType::Ne},
      {"<=", TokenType::Le},           {">=", TokenType::Ge},
      {"<<", TokenType::LShift},       {">>", TokenType::RShift},
      {"&&", TokenType::And},          {"||", TokenType::Or},
      {"++", TokenType::AddAdd},       {"--", TokenType::SubSub},
      {"...", TokenType::DotDotDot},   {"+=", TokenType::AddEqual},
      {"-=", TokenType::SubEqual},     {"*=", TokenType::MulEqual},
      {"/=", TokenType::DivEqual},     {"%=", TokenType::ModEqual},
      {"&=", TokenType::BitAndEqual},  {"|=", TokenType::BitOrEqual},
      {"^=", TokenType::BitXorEqual},  {"<<=", TokenType::LShiftEqual},
      {">>=", TokenType::RShiftEqual}, {"->", TokenType::RightArrow}};

  Token result{TokenType::Unknown};
  for (const auto &p : prefix_tokens) {
    if (s.starts_with(p.first) && p.first.size() > result.value.size()) {
      result = Token{p.second, p.first};
    }
  }
  return result;
}

Token Lexer::PeekNextToken() {
  if (_stage == LexerStage::InsideMacro) {
    Token tok = _sub_lexer->PeekNextToken();
    if (tok.type == TokenType::EndOfFile) {
      _stage = LexerStage::Normal;
    } else {
      return tok;
    }
  } else if (_stage == LexerStage::InsideIncludeFile) {
    if (_header_tokens) {
      Token tok = (*_header_tokens)[_header_token_index];
      if (tok.type != TokenType::EndOfFile) {
        return tok;
      } else {
        _header_tokens = nullptr;
        _stage = LexerStage::IncludeFileDone;
      }
    } else {
      Token tok = _sub_lexer->PeekNextToken();
      if (tok.type != TokenType::EndOfFile) {
        if (!_header_cached) {
          _header_tokens_cache.push_back(tok);
          _headers()[_header_path] = std::move(_header_tokens_cache);
          _header_tokens_cache.clear();
        }
        return tok;
      } else {
        _stage = LexerStage::IncludeFileDone;
      }
    }
  }

  _pos += TrimBlank(Rest());
  if (_pos >= _source.size()) {
    return Token{TokenType::EndOfFile};
  }

  std::string_view rest = Rest();

  if (_prev_token.type == TokenType::PreprocessorSharp) {
    Token tok = TryPeekAsIdentifier(rest);
    if (tok.type != TokenType::Unknown) {
      return Token{TokenType::Preprocessor, tok.value};
    } else {
      return tok;
    }
  }

  if (_prev_token.type == TokenType::Preprocessor) {
    if (_prev_token.value == "define") {
      Token tok = Token{TokenType::MacroDeclaration, GetMacroDecl(rest)};
      return tok;
    }
    if (_prev_token.value == "include") {
      Token tok = Token{TokenType::IncludeFile, GetIncludeFile(rest)};
      return tok;
    }
  }
  if (_prev_token.type == TokenType::MacroDeclaration) {
    Token macro_content_tok =
        Token{TokenType::MacroContent, GetMacroContent(rest)};

    auto [name, record] =
        MacroRecord::Parse(_prev_token.value, macro_content_tok.value);
    assert(_macros->find(name) == _macros->end());
    _macros->operator[](name) = record;

    return macro_content_tok;
  }
  if (_prev_token.type == TokenType::IncludeFile &&
      _stage != LexerStage::IncludeFileDone) {
    std::string_view filename =
        _prev_token.value.substr(1, _prev_token.value.size() - 2);
    auto it = _headers().find(std::string(filename));
    if (it != _headers().end()) {
      _header_cached = true;
      _header_tokens = &it->second;
      _header_token_index = 0;
      if ((*_header_tokens)[0].type == TokenType::EndOfFile) {
        _header_tokens = nullptr;
        _stage = LexerStage::IncludeFileDone;
        return PeekNextToken();
      } else {
        _stage = LexerStage::InsideIncludeFile;
        return (*_header_tokens)[0];
      }
    } else {
      _header_cached = false;
      _header_path = std::string(filename);
      _sub_lexer_source = ReadFileContent(filename);
      if (!_sub_lexer) {
        _sub_lexer = std::make_unique<Lexer>(_sub_lexer_source, _macros);
      } else {
        _sub_lexer->_source = _sub_lexer_source;
        _sub_lexer->_pos = 0;
      }
      _stage = LexerStage::InsideIncludeFile;
      return _sub_lexer->PeekNextToken();
    }
  }
  if (_stage == LexerStage::IncludeFileDone) {
    _stage = LexerStage::Normal;
  }

  Token tok;
  tok = TryPeekAsPrefixToken(rest);
  if (tok.type != TokenType::Unknown)
    return tok;

  tok = TryPeekAsLiteralNum(rest);
  if (tok.type != TokenType::Unknown)
    return tok;

  tok = TryPeekAsSingleCharToken(rest);
  if (tok.type != TokenType::Unknown)
    return tok;

  tok = TryPeekAsKeyword(rest);
  if (tok.type != TokenType::Unknown)
    return tok;

  tok = TryPeekAsIdentifier(rest);
  if (tok.type != TokenType::Unknown) {
    if (tok.value == begin_raw_ir_label) {
      size_t end = rest.find(end_raw_ir_label, tok.value.size());
      if (end != rest.npos) {
        std::string_view ir = rest.substr(0, end + end_raw_ir_label.size());
        return Token{TokenType::RawLLVMIR, ir};
      }
    }

    auto macro_it = _macros->find(tok.value);
    if (macro_it != _macros->end()) {
      InitMacroExpansionLexer(tok, macro_it->second);
      return _sub_lexer->PeekNextToken();
    } else {
      return tok;
    }
  }

  tok = TryPeekAsLiteralChar(rest);
  if (tok.type != TokenType::Unknown)
    return tok;

  tok = TryPeekAsLiteralString(rest);
  if (tok.type != TokenType::Unknown)
    return tok;

  return Token{TokenType::Unknown};
}

Token Lexer::ConsumeNextToken() {
  Token token = PeekNextToken();
  if (token.type == TokenType::EndOfFile) {
    if (_stage == LexerStage::InsideMacro) {
      _stage = LexerStage::Normal;
      return ConsumeNextToken();
    } else {
      return token;
    }
  }
  ConsumeToken(token);
  return token;
}

void Lexer::ConsumeToken(Token token) {
  if (_stage == LexerStage::Normal) {
    _pos += token.value.size();
  } else if (_stage == LexerStage::InsideIncludeFile) {
    if (_header_tokens) {
      _header_token_index++;
    } else {
      _sub_lexer->ConsumeToken(token);
      if (!_header_cached) {
        _header_tokens_cache.push_back(token);
      }
    }
  } else {
    _sub_lexer->ConsumeToken(token);
  }
  _prev_token = token;
}

static std::pair<std::string_view, std::vector<std::string_view>>
ParseMacroDecl(std::string_view decl) {
  // macro_decl := identifier lparen identifier* rparen

  Lexer lexer(decl);
  Token macro_name_tok = lexer.ConsumeNextToken();
  assert(macro_name_tok.type == TokenType::Identifier);
  std::string_view name = macro_name_tok.value;

  if (lexer.PeekNextToken().type != TokenType::LParen) {
    return {name, {}};
  } else {
    lexer.ConsumeNextToken();
  }

  std::vector<std::string_view> args;

  while (true) {
    Token tok = lexer.ConsumeNextToken();
    if (tok.type == TokenType::RParen) {
      break;
    }
    if (args.size()) {
      assert(tok.type == TokenType::Comma);
      tok = lexer.ConsumeNextToken();
    }
    assert(tok.type == TokenType::Identifier);
    args.push_back(tok.value);
  }

  assert(lexer.PeekNextToken().type == TokenType::EndOfFile);

  return {name, args};
}

MacroRecord ParseMacroContent(const std::vector<std::string_view> &args,
                              std::string_view content) {
  if (args.empty()) {
    return MacroRecord{.arg_count = 0, .chunks = {content}};
  }

  std::vector<std::pair<int, int>> arg_poses; // [arg_index, content_index]
  for (size_t i = 0; i < args.size(); i++) {
    size_t p = 0;
    while (p < content.size() && p != std::string_view::npos) {
      p = content.find(args[i], p);
      if (p != std::string_view::npos) {
        arg_poses.push_back({(int)i, (int)p});
        p += args[i].size();
      }
    }
  }
  std::ranges::sort(arg_poses, std::less{}, &std::pair<int, int>::second);

  std::vector<std::variant<std::string_view, MacroArgInsertion>> chunks;

  std::string_view last_post;
  for (int i = 0; i < arg_poses.size(); i++) {
    int arg_index = arg_poses[i].first;
    int content_index = arg_poses[i].second;

    bool valid_insert = true;
    std::string_view pre;
    if (i == 0) {
      pre = content.substr(0, arg_poses[0].second);
    } else {
      size_t pre_start =
          arg_poses[i - 1].second + args[arg_poses[i - 1].first].size();
      pre = content.substr(pre_start, content_index - pre_start);
    }
    MacroArgInsertType pre_insert_type = MacroArgInsertType::Unknown;
    if (pre.ends_with("##")) {
      pre = pre.substr(0, pre.size() - 2);
      pre_insert_type = MacroArgInsertType::Direct;
    } else if (pre.back() == '#') {
      pre = pre.substr(0, pre.size() - 1);
      pre_insert_type = MacroArgInsertType::AsString;
    } else if (!pre.empty() && !IsIdentifierBreak(pre.back())) {
      valid_insert = false;
    }

    std::string_view post =
        content.substr(content_index + args[arg_index].size());
    if (post.starts_with("##")) {
      post = post.substr(2);
    } else if (!post.empty() && !IsIdentifierBreak(post.front())) {
      valid_insert = false;
    }

    last_post = post;

    if (!valid_insert) {
      if (pre.size())
        chunks.push_back(pre);
      chunks.push_back(args[arg_index]);
    } else {
      if (pre.size())
        chunks.push_back(pre);
      chunks.push_back(MacroArgInsertion{.arg_index = arg_index,
                                         .insert_type = pre_insert_type});
    }
  }
  if (last_post.size()) {
    chunks.push_back(last_post);
  }

  return MacroRecord{.arg_count = args.size(), .chunks = chunks};
}

std::pair<std::string_view, MacroRecord>
MacroRecord::Parse(std::string_view decl, std::string_view content) {
  auto [name, args] = ParseMacroDecl(decl);
  MacroRecord record = ParseMacroContent(args, content);
  return {name, record};
}

std::string
MacroRecord::Expand(const std::vector<std::string_view> &args) const {
  std::string result;
  for (const auto &chunk : chunks) {
    if (std::holds_alternative<std::string_view>(chunk)) {
      result += std::get<std::string_view>(chunk);
    } else {
      const MacroArgInsertion &insertion = std::get<MacroArgInsertion>(chunk);
      std::string_view arg_value = args[insertion.arg_index];
      if (insertion.insert_type == MacroArgInsertType::AsString) {
        result += '"';
        result += arg_value;
        result += '"';
      } else {
        result += arg_value;
      }
    }
  }
  return result;
}

std::unordered_map<std::string, std::vector<Token>> &Lexer::_headers() {
  static std::unordered_map<std::string, std::vector<Token>> headers;
  return headers;
}
